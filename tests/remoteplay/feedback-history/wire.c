// SPDX-License-Identifier: GPL-3.0-or-later
// No test doubles: real Chiaki sender, Takion serializer, crypto and UDP socket.
// Synthetic keys and 127.0.0.1 only. This does not emulate a complete PS5 session.
#include <chiaki/feedbacksender.h>

#include <windows.h>
#include <stdio.h>
#include <string.h>

// A failed assertion exits this short-lived process; Windows releases its
// sockets and threads. The successful path explicitly finalizes each resource.
#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    WSADATA winsock;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET transmitter = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(receiver != INVALID_SOCKET && transmitter != INVALID_SOCKET);
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(receiver, (struct sockaddr *)&local, sizeof(local)) == 0);
    int address_size = sizeof(local);
    REQUIRE(getsockname(receiver, (struct sockaddr *)&local, &address_size) == 0);
    REQUIRE(connect(transmitter, (struct sockaddr *)&local, sizeof(local)) == 0);
    DWORD timeout = 300;
    REQUIRE(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == 0);

    ChiakiLog log;
    chiaki_log_init(&log, 0, NULL, NULL);
    uint8_t handshake[16] = {0}, secret[32] = {0};
    ChiakiGKCrypt crypt;
    REQUIRE(chiaki_gkcrypt_init(&crypt, &log, 0, 2, handshake, secret) == CHIAKI_ERR_SUCCESS);
    ChiakiTakion takion = {0};
    takion.log = &log;
    takion.sock = transmitter;
    takion.version = 12;
    takion.gkcrypt_local = &crypt;
    REQUIRE(chiaki_mutex_init(&takion.gkcrypt_local_mutex, true) == CHIAKI_ERR_SUCCESS);

    ChiakiFeedbackSender sender;
    REQUIRE(chiaki_feedback_sender_init(&sender, &takion) == CHIAKI_ERR_SUCCESS);
    ChiakiControllerState state;
    chiaki_controller_state_set_idle(&state);
    state.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON;
    REQUIRE(chiaki_feedback_sender_set_controller_state(&sender, &state) == CHIAKI_ERR_SUCCESS);
    chiaki_controller_state_set_idle(&state);
    REQUIRE(chiaki_feedback_sender_set_controller_state(&sender, &state) == CHIAKI_ERR_SUCCESS);
    Sleep(350);
    chiaki_feedback_sender_fini(&sender);

    unsigned histories = 0, states = 0;
    const uint8_t expected_newest[][3] = {
        {0x80, 0x88, 255}, {0x80, 0x89, 255},
        {0x80, 0x88, 0}, {0x80, 0x89, 0}
    };
    for (;;) {
        uint8_t packet[1024];
        int size = recv(receiver, (char *)packet, sizeof(packet), 0);
        if (size == SOCKET_ERROR) {
            REQUIRE(WSAGetLastError() == WSAETIMEDOUT);
            break;
        }
        REQUIRE(size >= 12);
        uint16_t seq;
        uint32_t key_pos;
        memcpy(&seq, packet + 1, sizeof(seq));
        seq = ntohs(seq);
        memcpy(&key_pos, packet + 4, sizeof(key_pos));
        key_pos = ntohl(key_pos);
        REQUIRE(chiaki_gkcrypt_decrypt(&crypt, (uint64_t)key_pos + CHIAKI_GKCRYPT_BLOCK_SIZE,
                    packet + 12, size - 12) == CHIAKI_ERR_SUCCESS);
        if (packet[0] == 6) {
            REQUIRE(packet[3] == 0); // state count minus one
            states++;
        } else {
            REQUIRE(packet[0] == 1);
            unsigned newest = histories < 4 ? histories : 3;
            REQUIRE(seq == newest);
            REQUIRE(packet[3] == newest); // history contains newest+1 events
            REQUIRE(size == 12 + 3 * (int)(newest + 1));
            REQUIRE(memcmp(packet + 12, expected_newest[newest], 3) == 0);
            histories++;
        }
    }
    REQUIRE(histories == 8); // four edges and four identical-sequence tail resends
    REQUIRE(states >= 1);
    chiaki_mutex_fini(&takion.gkcrypt_local_mutex);
    chiaki_gkcrypt_fini(&crypt);
    closesocket(transmitter);
    closesocket(receiver);
    WSACleanup();
    printf("PASS: %u encrypted history packets and %u state packets over loopback\n", histories, states);
    return 0;
}
