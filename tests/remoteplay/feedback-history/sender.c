// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the real Chiaki sender, history encoder, threads and clock. The Takion
// boundary captures packets instead of accessing a console/network. These are
// protocol regressions, not a substitute for an actual PS5/UFC 5 test.
#include <chiaki/feedbacksender.h>

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct Packet
{
    uint16_t seq;
    uint8_t events;
    size_t size;
    uint8_t payload[0x300];
} Packet;

static Packet packets[512];
static size_t packet_count;
static CRITICAL_SECTION capture_lock;
static int failures;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
            failures++; \
        } \
    } while (0)

static size_t event_size(const uint8_t *event)
{
    if (event[0] == 0xc0 || event[0] == 0xd0)
        return 5;
    return event[1] >= 0x8c ? 2 : 3;
}

ChiakiErrorCode chiaki_takion_send_feedback_state(ChiakiTakion *takion,
        ChiakiSeqNum16 seq, ChiakiFeedbackState *state)
{
    (void)takion;
    (void)seq;
    (void)state;
    return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_takion_send_feedback_history(ChiakiTakion *takion,
        ChiakiSeqNum16 seq,
#ifndef LEGACY_CHIAKI
        uint8_t events,
#endif
        uint8_t *payload, size_t size)
{
    (void)takion;
    EnterCriticalSection(&capture_lock);
    if (packet_count < sizeof(packets) / sizeof(packets[0]) && size <= sizeof(packets[0].payload)) {
        Packet *packet = &packets[packet_count++];
        packet->seq = seq;
        packet->size = size;
        memcpy(packet->payload, payload, size);
#ifdef LEGACY_CHIAKI
        // Original Takion hardcodes the count-minus-one header to zero.
        packet->events = 1;
#else
        packet->events = events;
#endif
    }
    LeaveCriticalSection(&capture_lock);
    return CHIAKI_ERR_SUCCESS;
}

static void run_pair(uint32_t buttons, uint8_t triggers, uint16_t start, const char *name)
{
    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    ChiakiTakion takion = {0};
    takion.log = &log;
    ChiakiFeedbackSender sender;
    packet_count = 0;
    ChiakiErrorCode result = chiaki_feedback_sender_init(&sender, &takion);
    CHECK(result == CHIAKI_ERR_SUCCESS, "sender initializes");
    if (result != CHIAKI_ERR_SUCCESS)
        return;
    chiaki_mutex_lock(&sender.state_mutex);
    sender.history_seq_num = start;
    chiaki_mutex_unlock(&sender.state_mutex);

    ChiakiControllerState state;
    chiaki_controller_state_set_idle(&state);
    state.buttons = buttons;
    state.l2_state = state.r2_state = triggers;
    CHECK(chiaki_feedback_sender_set_controller_state(&sender, &state) == CHIAKI_ERR_SUCCESS,
          "press submitted");
    // Both snapshots may be queued before the worker runs: every edge must survive.
    chiaki_controller_state_set_idle(&state);
    CHECK(chiaki_feedback_sender_set_controller_state(&sender, &state) == CHIAKI_ERR_SUCCESS,
          "release submitted");
    Sleep(350);
    chiaki_feedback_sender_fini(&sender);

    size_t per_state = triggers ? 2 : 0;
    for (unsigned i = 0; i < CHIAKI_CONTROLLER_BUTTONS_COUNT; ++i) {
        if (buttons & (1u << i))
            per_state++;
    }
    const size_t expected = 2 * per_state;
    CHECK(packet_count == expected + 4, "one packet per edge plus four bounded tail repeats");
    if (packet_count < expected + 4) {
        printf("%s: %zu packets; expected %zu\n", name, packet_count, expected + 4);
        return;
    }
    for (size_t i = 0; i < expected; ++i) {
        CHECK(packets[i].seq == (uint16_t)(start + i),
              "sequence advances once per event, including wrap");
        size_t pos = 0, events = 0;
        while (pos + 2 <= packets[i].size) {
            pos += event_size(packets[i].payload + pos);
            events++;
        }
        CHECK(pos == packets[i].size && events == packets[i].events,
              "event count matches encoded payload");
        CHECK(events <= 9, "history remains bounded");
    }
    const Packet *last = &packets[expected - 1];
    for (size_t i = expected; i < packet_count; ++i) {
        CHECK(packets[i].seq == last->seq, "tail resend preserves sequence");
        CHECK(packets[i].size == last->size &&
              memcmp(packets[i].payload, last->payload, last->size) == 0,
              "tail resend preserves state");
    }
    // Receiver model follows upstream's documented rule: accept only new event
    // numbers, read newest-first history, then apply accepted events oldest-first.
    // Drop the final release packet and accept a tail resend instead.
    uint8_t received[0x100] = {0};
    uint16_t previous = start - 1;
    unsigned applied = 0;
    for (size_t i = 0; i < packet_count; ++i) {
        if (i == expected - 1)
            continue;
        const Packet *packet = &packets[i];
        uint16_t advance = packet->seq - previous;
        if (advance == 0 || advance >= 0x8000)
            continue;
        size_t offsets[16], pos = 0, count = 0;
        while (pos + 2 <= packet->size && count < packet->events && count < advance &&
               count < sizeof(offsets) / sizeof(offsets[0])) {
            size_t length = event_size(packet->payload + pos);
            CHECK(pos + length <= packet->size, "complete event in history payload");
            if (pos + length > packet->size)
                break;
            offsets[count++] = pos;
            pos += length;
        }
        while (count) {
            const uint8_t *event = packet->payload + offsets[--count];
            CHECK(event[0] == 0x80, "digital or trigger event");
            if (event[1] < 0x8c)
                received[event[1]] = event[2];
            else
                received[event[1] & ~0x20] = (event[1] & 0x20) != 0;
            applied++;
        }
        previous = packet->seq;
    }
    CHECK(applied == expected, "loss recovery applies each edge exactly once");
    for (size_t i = 0; i < sizeof(received); ++i)
        CHECK(received[i] == 0, "all buttons released after packet loss");
    printf("%s: %zu events, %zu packets checked\n", name, expected, packet_count);
}

int main(void)
{
    InitializeCriticalSection(&capture_lock);
    run_pair(CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON, 0, 0, "A+B press/release");
    run_pair(CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON |
             CHIAKI_CONTROLLER_BUTTON_L1 | CHIAKI_CONTROLLER_BUTTON_R1, 255, 0, "strikes + modifiers");
    run_pair(CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON, 0, 65534, "16-bit sequence wrap");
    run_pair((1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1, 255, 0, "all buttons + triggers");
    DeleteCriticalSection(&capture_lock);
    printf("%s: %d failed assertions\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
