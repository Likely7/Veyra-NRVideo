// SPDX-License-Identifier: GPL-3.0-or-later
// Real Takion socket setup on 127.0.0.1 only. No console or pairing keys.
#include <chiaki/takion.h>

#include <windows.h>
#include <stdio.h>

// An assertion failure ends this short-lived process; Windows releases its
// sockets and threads. The successful path finalizes every resource explicitly.
#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL line %d: %s (WSA=%d)\n", \
                    __LINE__, #condition, WSAGetLastError()); \
            return 1; \
        } \
    } while (0)

static void event_callback(ChiakiTakionEvent *event, void *user)
{
    (void)event;
    (void)user;
}

int main(void)
{
    WSADATA winsock;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(server != INVALID_SOCKET);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0);
    int address_size = sizeof(address);
    REQUIRE(getsockname(server, (struct sockaddr *)&address, &address_size) == 0);

    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    for (int existing = 0; existing < 2; ++existing) {
        ChiakiTakion takion = {0};
        ChiakiTakionConnectInfo info = {0};
        info.log = &log;
        info.sa = (struct sockaddr *)&address;
        info.sa_len = sizeof(address);
        info.protocol_version = 12;
        info.close_socket = true;
        info.cb = event_callback;

        // The second case exercises the existing-socket branch used by PSN.
        // The destination remains our bound loopback socket in both cases.
        chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
        if (existing) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            REQUIRE(!CHIAKI_SOCKET_IS_INVALID(sock));
            REQUIRE(connect(sock, info.sa, (int)info.sa_len) == 0);
        }
        REQUIRE(chiaki_takion_connect(&takion, &info, existing ? &sock : NULL)
                == CHIAKI_ERR_SUCCESS);
        int actual = 0;
        int length = sizeof(actual);
        REQUIRE(getsockopt(takion.sock, SOL_SOCKET, SO_RCVBUF,
                    (char *)&actual, &length) == 0);
        // Read back the OS allocation, not just the requested constant.
        REQUIRE(actual >= 4 * 1024 * 1024);
        REQUIRE(takion.a_rwnd == 0x19000);
        printf("PASS %s socket: SO_RCVBUF=%d protocol_window=%u\n",
                existing ? "existing" : "new", actual, takion.a_rwnd);
        chiaki_takion_close(&takion);
    }
    closesocket(server);
    WSACleanup();
    return 0;
}
