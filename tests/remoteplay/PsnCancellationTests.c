// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Exercise the reviewed dependency's actual private teardown, without PSN,
// DNS, HTTP, real console traffic, credentials, or a mock transport library.
#include <stdlib.h>
#include <chiaki/thread.h>
static ChiakiErrorCode scheduled_mutex_unlock(ChiakiMutex *mutex);
static void notification_free(void *pointer);
// Deterministic scheduling only: all production locks and queue operations run.
// A guarded allocation makes a read after notification ownership transfer fail.
#define chiaki_mutex_unlock scheduled_mutex_unlock
#define free notification_free
#include VEYRA_CHIAKI_HOLEPUNCH_SOURCE
#undef chiaki_mutex_unlock
#undef free
#include <chiaki/remote/rudp.h>
#include <chiaki/takion.h>

static DWORD publishing_thread;
static ChiakiMutex *publishing_mutex;
static HANDLE published_event, consumed_event;
static void *guarded_notification;
static volatile LONG guarded_freed;

static ChiakiErrorCode scheduled_mutex_unlock(ChiakiMutex *mutex)
{
    const ChiakiErrorCode result = chiaki_mutex_unlock(mutex);
    if(GetCurrentThreadId() == publishing_thread && mutex == publishing_mutex) {
        publishing_mutex = NULL;
        SetEvent(published_event);
        if(WaitForSingleObject(consumed_event, 1000) != WAIT_OBJECT_0) ExitProcess(80);
    }
    return result;
}

static void notification_free(void *pointer)
{
    if(pointer && pointer == guarded_notification) {
        DWORD previous;
        if(!VirtualProtect(pointer, 4096, PAGE_NOACCESS, &previous)) ExitProcess(81);
        InterlockedExchange(&guarded_freed, 1);
    } else free(pointer);
}

typedef struct worker_fixture_t { volatile LONG finished; } WorkerFixture;

static DWORD WINAPI delayed_worker(LPVOID user)
{
    WorkerFixture *fixture = (WorkerFixture *)user;
    Sleep(25);
    InterlockedExchange(&fixture->finished, 1);
    return 0;
}

static int teardown_case(bool opened, bool cancel)
{
    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
    if(!session) return 1;
    // A pending notification is released only after the worker has joined.
    Notification *notification = calloc(1, sizeof(*notification));
    if(!notification) { chiaki_holepunch_session_fini(session); return 2; }
    notification->json = json_object_new_object();
    enqueueNq(session->ws_notification_queue, notification);
    WorkerFixture fixture = {0};
    // Use a real native worker with independently owned arguments. A regression
    // can then be reported safely even if fini mistakenly returns before join.
    HANDLE thread = CreateThread(NULL, 0, delayed_worker, &fixture, 0, NULL);
    if(!thread) { chiaki_holepunch_session_fini(session); return 3; }
    // The fixture keeps an independent handle for observing thread exit. The
    // production finalizer owns and closes the duplicate stored in Session.
    if(!DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(),
        &session->ws_thread.thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        WaitForSingleObject(thread, 1000);CloseHandle(thread);
        chiaki_holepunch_session_fini(session);return 3;
    }
    session->ws_thread_started = true;
    session->ws_open = opened;
    // session_id remains empty: no session was created and no DELETE is sent.
    bool cancellation_observed = true;
    if(cancel) {
        chiaki_holepunch_main_thread_cancel(session, true);
        cancellation_observed = curl_session_canceled(session, 0, 0, 0, 0) != 0;
    }
    chiaki_holepunch_session_fini(session);
    const bool joined = WaitForSingleObject(thread, 0) == WAIT_OBJECT_0;
    // Keep worker arguments alive even if a regression returned before join.
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
    return joined && cancellation_observed && InterlockedCompareExchange(&fixture.finished, 0, 0) ? 0 : 5;
}

typedef struct stun_fixture_t {
    ChiakiLog log;
    ChiakiStopPipe stop;
    StunServer server;
    chiaki_socket_t client;
    bool result;
} StunFixture;

static DWORD WINAPI stun_worker(LPVOID user)
{
    StunFixture *fixture = (StunFixture *)user;
    char address[INET6_ADDRSTRLEN]; uint16_t port = 0;
    fixture->result = stun_get_external_address_from_server(&fixture->log,
        &fixture->server, address, &port, &fixture->client, true, &fixture->stop);
    return 0;
}

static int stun_receive_cancel(void)
{
    // A silent UDP listener on numeric loopback proves cancellation interrupts
    // an already-sent request. No DNS or external address is used by the worker.
    StunFixture fixture = {0};
    chiaki_log_init(&fixture.log, 0, NULL, NULL);
    if(chiaki_stop_pipe_init(&fixture.stop) != CHIAKI_ERR_SUCCESS) return 11;
    SOCKET listener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    fixture.client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in local = {0};local.sin_family = AF_INET;local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int size = sizeof(local);int timeout = 1000;int result = 12;
    if(CHIAKI_SOCKET_IS_INVALID(listener) || CHIAKI_SOCKET_IS_INVALID(fixture.client)) goto cleanup;
    if(bind(listener, (struct sockaddr *)&local, sizeof(local)) || getsockname(listener, (struct sockaddr *)&local, &size)) goto cleanup;
    if(setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout))) goto cleanup;
    fixture.server.host = "127.0.0.1";fixture.server.port = ntohs(local.sin_port);
    HANDLE thread = CreateThread(NULL, 0, stun_worker, &fixture, 0, NULL);
    if(!thread) goto cleanup;
    char request[256];
    const int received = recvfrom(listener, request, sizeof(request), 0, NULL, NULL);
    chiaki_stop_pipe_stop(&fixture.stop);
    const bool joined = WaitForSingleObject(thread, 1000) == WAIT_OBJECT_0;
    // Keep the fixture alive even when testing a regression to the old 5s wait.
    WaitForSingleObject(thread, 6000);CloseHandle(thread);
    result = received == STUN_HEADER_SIZE && joined && !fixture.result ? 0 : 13;
cleanup:
    if(!CHIAKI_SOCKET_IS_INVALID(listener))closesocket(listener);
    if(!CHIAKI_SOCKET_IS_INVALID(fixture.client))closesocket(fixture.client);
    chiaki_stop_pipe_fini(&fixture.stop);
    return result;
}

static bool socket_open(chiaki_socket_t socket)
{
    int type = 0;int size = sizeof(type);
    return getsockopt(socket, SOL_SOCKET, SO_TYPE, (char *)&type, &size) == 0;
}

static void ignore_takion_event(ChiakiTakionEvent *event, void *user) {(void)event;(void)user;}

static int socket_ownership(void)
{
    ChiakiLog log;chiaki_log_init(&log, 0, NULL, NULL);
    Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
    if(!session)return 20;
    const SOCKET control = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET data = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    session->ctrl_sock = control;session->data_sock = data;
    chiaki_holepunch_session_fini(session);
    if(socket_open(control) || socket_open(data))return 21;

    session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
    if(!session)return 22;
    const SOCKET owned = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);session->ctrl_sock = owned;
    ChiakiRudp rudp = chiaki_rudp_init(&session->ctrl_sock, &log);
    if(!rudp) {chiaki_holepunch_session_fini(session);return 23;}
    const bool transferred = CHIAKI_SOCKET_IS_INVALID(session->ctrl_sock);
    chiaki_holepunch_session_fini(session);
    const bool owner_alive = socket_open(owned);
    chiaki_rudp_fini(rudp);
    if(!transferred || !owner_alive || socket_open(owned))return 24;

    // Senkusha borrows the data socket; the streaming Takion takes ownership.
    // Exercise both production paths with a silent numeric-loopback peer.
    for(int take = 0; take != 2; ++take) {
        session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
        if(!session)return 25;
        SOCKET listener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        SOCKET transport = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in local = {0};local.sin_family = AF_INET;local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int size = sizeof(local);
        if(bind(listener, (struct sockaddr *)&local, sizeof(local)) || getsockname(listener, (struct sockaddr *)&local, &size)
            || connect(transport, (struct sockaddr *)&local, sizeof(local))) {
            closesocket(listener);closesocket(transport);chiaki_holepunch_session_fini(session);return 26;
        }
        session->data_sock = transport;
        ChiakiTakion takion = {0};ChiakiTakionConnectInfo info = {0};
        info.log = &log;info.protocol_version = 12;info.close_socket = take != 0;info.cb = ignore_takion_event;
        if(chiaki_takion_connect(&takion, &info, &session->data_sock) != CHIAKI_ERR_SUCCESS) {
            chiaki_holepunch_session_fini(session);closesocket(listener);return 27;
        }
        const bool slot_valid = !CHIAKI_SOCKET_IS_INVALID(session->data_sock);
        if(take)chiaki_holepunch_session_fini(session);
        chiaki_takion_close(&takion);CloseHandle(takion.thread.thread);
        const bool transport_open = socket_open(transport);
        if(!take)chiaki_holepunch_session_fini(session);
        closesocket(listener);
        if(slot_valid == (take != 0) || transport_open == (take != 0) || socket_open(transport))return 28;
    }
    return 0;
}

// The production notification handlers are used by both PSN create/start loops.
// Probe their real locks from another thread: Windows mutexes are recursive, so
// a same-thread trylock would miss a leaked lock and the subsequent join hang.
typedef struct notification_lock_probe_t {
    Session *session;
    bool state_unlocked;
    bool stop_unlocked;
} NotificationLockProbe;

static DWORD WINAPI notification_lock_probe(LPVOID user)
{
    NotificationLockProbe *probe = (NotificationLockProbe *)user;
    probe->state_unlocked = chiaki_mutex_trylock(&probe->session->state_mutex) == CHIAKI_ERR_SUCCESS;
    if(probe->state_unlocked) chiaki_mutex_unlock(&probe->session->state_mutex);
    probe->stop_unlocked = chiaki_mutex_trylock(&probe->session->stop_mutex) == CHIAKI_ERR_SUCCESS;
    if(probe->stop_unlocked) chiaki_mutex_unlock(&probe->session->stop_mutex);
    return 0;
}

static int notification_case(bool start, NotificationType type, const char *json,
    ChiakiHolepunchConsoleType console, bool canceled, bool expected_success,
    unsigned prior_state, bool expected_finished)
{
    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
    if(!session) return 21;
    Notification *notification = calloc(1, sizeof(*notification));
    if(!notification) { chiaki_holepunch_session_fini(session); return 22; }
    notification->type = type;
    notification->json = json_tokener_parse(json);
    if(!notification->json) { free(notification); chiaki_holepunch_session_fini(session); return 22; }
    enqueueNq(session->ws_notification_queue, notification);
    session->state |= prior_state;
    if(canceled) chiaki_holepunch_main_thread_cancel(session, true);
    bool finished = true;
    const ChiakiErrorCode result = start
        ? process_start_notification(session, notification, console, &finished)
        : process_create_notification(session, notification, &finished);
    NotificationLockProbe probe = {session, false, false};
    const bool balanced = session->state_mutex.cs.RecursionCount == 0
        && session->stop_mutex.cs.RecursionCount == 0;
    HANDLE thread = CreateThread(NULL, 0, notification_lock_probe, &probe, 0, NULL);
    if(!thread) return 23;
    if(WaitForSingleObject(thread, 1000) != WAIT_OBJECT_0) ExitProcess(24);
    CloseHandle(thread);
    if(!balanced || !probe.state_unlocked || !probe.stop_unlocked)
        return 25; // Do not finalize a corrupted/held lock; process exits on failure.
    const bool correct_result = canceled ? result == CHIAKI_ERR_CANCELED
        : expected_success ? result == CHIAKI_ERR_SUCCESS : result != CHIAKI_ERR_SUCCESS;
    const bool correct_state = !start || console != CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4
        || !expected_success || session->console_uid[0] == 0x11;
    const bool cleared = clear_notification(session, notification) == CHIAKI_ERR_SUCCESS
        && session->ws_notification_queue->front == NULL;
    // Empty session_id ensures teardown performs no HTTP request.
    chiaki_holepunch_session_fini(session);
    return correct_result && finished == expected_finished && correct_state && cleared ? 0 : 26;
}

typedef struct notification_wait_fixture_t {
    Session *session;
    HANDLE entered;
    ChiakiErrorCode result;
} NotificationWaitFixture;

static DWORD WINAPI notification_wait_worker(LPVOID user)
{
    NotificationWaitFixture *fixture = (NotificationWaitFixture *)user;
    Notification *notification = NULL;
    SetEvent(fixture->entered);
    fixture->result = wait_for_notification(fixture->session, &notification,
        NOTIFICATION_TYPE_SESSION_CREATED, 30000);
    return 0;
}

static int notification_wait_cancel(bool before_wait, bool sticky_only)
{
    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
    if(!session) return 27;
    NotificationWaitFixture fixture = {session, CreateEvent(NULL, FALSE, FALSE, NULL), CHIAKI_ERR_SUCCESS};
    if(!fixture.entered) { chiaki_holepunch_session_fini(session); return 28; }
    if(before_wait) {
        chiaki_holepunch_main_thread_cancel(session, true);
        if(sticky_only) session->main_should_stop = false; // A preceding operation consumed main cancellation.
    }
    HANDLE thread = CreateThread(NULL, 0, notification_wait_worker, &fixture, 0, NULL);
    if(!thread) { CloseHandle(fixture.entered); chiaki_holepunch_session_fini(session); return 28; }
    if(WaitForSingleObject(fixture.entered, 1000) != WAIT_OBJECT_0) ExitProcess(29);
    if(!before_wait) { Sleep(25); chiaki_holepunch_main_thread_cancel(session, true); }
    // A failed test terminates before stack/session storage can be freed while
    // the old 30-second blocking waiter still uses it.
    if(WaitForSingleObject(thread, 1000) != WAIT_OBJECT_0) ExitProcess(30);
    CloseHandle(thread);
    CloseHandle(fixture.entered);
    const bool canceled = fixture.result == CHIAKI_ERR_CANCELED;
    chiaki_holepunch_session_fini(session);
    return canceled ? 0 : 31;
}

static int notification_lock_cases(void)
{
    const char *member_zero = "{\"body\":{\"data\":{\"members\":[{\"deviceUniqueId\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}}}";
    const char *member_other = "{\"body\":{\"data\":{\"members\":[{\"deviceUniqueId\":\"1111111111111111111111111111111111111111111111111111111111111111\"}]}}}";
    const char *member_badhex = "{\"body\":{\"data\":{\"members\":[{\"deviceUniqueId\":\"gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg\"}]}}}";
    const char *custom_valid = "{\"body\":{\"data\":{\"customData1\":\"QUFBQUFBQUFBQUFBQUFBQUFBQUFBQT09\"}}}";
    struct notification_case_t {
        bool start;
        NotificationType type;
        const char *json;
        ChiakiHolepunchConsoleType console;
        bool canceled, success;
        unsigned prior;
        bool finished;
    } cases[] = {
        {false, NOTIFICATION_TYPE_SESSION_CREATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {false, NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {false, NOTIFICATION_TYPE_MEMBER_CREATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, true, false, 0, false},
        {false, NOTIFICATION_TYPE_SESSION_CREATED, "{\"to\":{\"onlineId\":\"offline\"}}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, true, 0, false},
        {false, NOTIFICATION_TYPE_SESSION_CREATED, "{\"to\":{\"onlineId\":\"offline\"}}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, true, SESSION_STATE_CLIENT_JOINED, true},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, "{\"body\":{\"data\":{\"members\":[{\"deviceUniqueId\":\"0\"}]}}}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, member_badhex, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, member_other, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED, "{\"body\":{\"data\":{\"customData1\":\"0\"}}}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED, "{\"body\":{\"data\":{\"customData1\":\"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\"}}}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_SESSION_CREATED, "{}", CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, false, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, member_zero, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, true, false, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, member_zero, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, true, 0, false},
        {true, NOTIFICATION_TYPE_MEMBER_CREATED, member_other, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4, false, true, SESSION_STATE_CUSTOMDATA1_RECEIVED, true},
        {true, NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED, custom_valid, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, false, true, SESSION_STATE_CONSOLE_JOINED, true},
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct notification_case_t *c = &cases[i];
        const int result = notification_case(c->start, c->type, c->json, c->console,
            c->canceled, c->success, c->prior, c->finished);
        if(result) { fprintf(stderr, "PSN notification lock regression case=%u code=%d\n", i, result); return result; }
    }
    for(unsigned i = 0; i < 3; ++i) {
        const int result = notification_wait_cancel(i != 0, i == 2);
        if(result) { fprintf(stderr, "PSN notification cancel regression case=%u code=%d\n", i, result); return result; }
    }
    puts("PSN_NOTIFICATION_LOCKS_PASS parse_cases=17 cancel_wait_cases=3 real_handlers=1 cross_thread_lock_probe=1 external_network=0");
    return 0;
}

static DWORD WINAPI consume_published_notification(LPVOID user)
{
    Session *session = (Session *)user;
    if(WaitForSingleObject(published_event, 1000) != WAIT_OBJECT_0) ExitProcess(82);
    Notification *notification = NULL;
    if(wait_for_notification(session, &notification, 0xffff, 1000) != CHIAKI_ERR_SUCCESS
        || clear_notification(session, notification) != CHIAKI_ERR_SUCCESS) ExitProcess(83);
    SetEvent(consumed_event);
    return 0;
}

static int notification_publish_cases(void)
{
    struct { NotificationType type; const char *json; bool closes; } cases[] = {
        {NOTIFICATION_TYPE_SESSION_DELETED, "{\"body\":{\"data\":{\"sessionId\":\"current-offline-session\"}}}", true},
        {NOTIFICATION_TYPE_SESSION_DELETED, "{\"body\":{\"data\":{\"sessionId\":\"unrelated-offline-session\"}}}", false},
        {NOTIFICATION_TYPE_SESSION_DELETED, "{}", true},
        {NOTIFICATION_TYPE_MEMBER_CREATED, "{}", false},
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ChiakiLog log;chiaki_log_init(&log, 0, NULL, NULL);
        Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
        if(!session) return 84;
        strcpy(session->session_id, "current-offline-session");
        Notification *notification = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if(!notification) return 85;
        notification->type = cases[i].type;notification->json = json_tokener_parse(cases[i].json);
        guarded_notification = notification;guarded_freed = 0;
        published_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        consumed_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        publishing_thread = GetCurrentThreadId();publishing_mutex = &session->notif_mutex;
        HANDLE worker = CreateThread(NULL, 0, consume_published_notification, session, 0, NULL);
        if(!worker || !published_event || !consumed_event) ExitProcess(86);
        // The scheduling hook makes the real consumer release and protect the
        // notification before publish_notification returns to its WS caller.
        const bool closes = publish_notification(session, notification);
        if(WaitForSingleObject(worker, 1000) != WAIT_OBJECT_0) ExitProcess(87);
        CloseHandle(worker);CloseHandle(published_event);CloseHandle(consumed_event);
        publishing_thread = 0;publishing_mutex = NULL;
        const bool freed = InterlockedCompareExchange(&guarded_freed, 0, 0) != 0;
        guarded_notification = NULL;VirtualFree(notification, 0, MEM_RELEASE);
        session->session_id[0] = '\0'; // No real session exists; do not issue DELETE.
        chiaki_holepunch_session_fini(session);
        if(!freed || closes != cases[i].closes) return 88;
    }
    return 0;
}

static int notification_channel_cases(void)
{
    for(unsigned mode = 0; mode < 4; ++mode) {
        ChiakiLog log;chiaki_log_init(&log, 0, NULL, NULL);
        Session *session = chiaki_holepunch_session_init("synthetic-offline-token", &log);
        if(!session) return 89;
        NotificationWaitFixture fixture = {session, CreateEvent(NULL, FALSE, FALSE, NULL), CHIAKI_ERR_SUCCESS};
        if(mode != 0) websocket_finished(session);
        if(mode == 2) chiaki_holepunch_main_thread_cancel(session, true);
        if(mode == 3) {
            Notification *pending = calloc(1, sizeof(*pending));
            if(!pending) return 90;
            pending->type = NOTIFICATION_TYPE_SESSION_CREATED;pending->json = json_object_new_object();
            publish_notification(session, pending);
        }
        HANDLE worker = CreateThread(NULL, 0, notification_wait_worker, &fixture, 0, NULL);
        if(!worker || !fixture.entered) ExitProcess(91);
        if(WaitForSingleObject(fixture.entered, 1000) != WAIT_OBJECT_0) ExitProcess(92);
        if(mode == 0) { Sleep(25);websocket_finished(session); }
        if(WaitForSingleObject(worker, 1000) != WAIT_OBJECT_0) ExitProcess(93);
        CloseHandle(worker);CloseHandle(fixture.entered);
        const ChiakiErrorCode expected = mode == 2 ? CHIAKI_ERR_CANCELED
            : mode == 3 ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_NETWORK;
        const bool correct = fixture.result == expected;
        chiaki_holepunch_session_fini(session);
        if(!correct) return 94;
    }
    puts("PSN_NOTIFICATION_LIFECYCLE_PASS guarded_publish_cases=4 closed_channel_cases=4 external_network=0");
    return 0;
}

int main(void)
{
    if(chiaki_lib_init() != CHIAKI_ERR_SUCCESS) return 10;
    const int published = notification_publish_cases();
    if(published) return published;
    const int channel = notification_channel_cases();
    if(channel) return channel;
    const int notifications = notification_lock_cases();
    if(notifications) return notifications;
    for(unsigned i = 0; i < 24; ++i) {
        const int result = teardown_case((i & 1) != 0, (i & 2) != 0);
        if(result) { fprintf(stderr, "PSN teardown regression case=%u code=%d\n", i, result); return result; }
    }
    const int stun = stun_receive_cancel();
    if(stun) { fprintf(stderr, "STUN cancel regression code=%d\n", stun); return stun; }
    const int sockets = socket_ownership();
    if(sockets) { fprintf(stderr, "PSN socket ownership regression code=%d\n", sockets); return sockets; }
    puts("PSN_CANCELLATION_PASS cleanup_joined=24 unopened_and_opened=1 canceled_and_failed=1 stun_receive_canceled=1 socket_ownership=1 loopback_only=1 external_network=0");
    return 0;
}
