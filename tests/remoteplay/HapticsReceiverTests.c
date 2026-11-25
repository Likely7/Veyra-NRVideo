// SPDX-License-Identifier: GPL-3.0-or-later
#include <chiaki/session.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned count;
    int events[16];
} Capture;

static void frame(uint8_t *pcm, size_t bytes, bool gap_before, void *user)
{
    Capture *capture = user;
    if (gap_before && capture->count < 16)
        capture->events[capture->count++] = -1;
    if (capture->count < 16)
        capture->events[capture->count] = pcm && bytes == 4 ? pcm[0] : -2;
    ++capture->count;
}

static void packet(ChiakiAudioReceiver *receiver, uint16_t index, uint8_t sample, bool with_fec)
{
    // Takion stores the source unit first, followed by the previous unit if present.
    uint8_t pcm[8] = {sample, 0, 0, 0, (uint8_t)(sample - 1), 0, 0, 0};
    ChiakiTakionAVPacket packet = {0};
    packet.codec = 5;
    packet.is_haptics = true;
    packet.frame_index = index;
    packet.units_in_frame_fec = (4u << 8) | (with_fec ? 0x11 : 0x01);
    packet.units_in_frame_total = with_fec ? 2 : 1;
    packet.data = pcm;
    packet.data_size = packet.units_in_frame_total * 4;
    chiaki_audio_receiver_av_packet(receiver, &packet);
}

static void physical_frame(uint8_t *pcm, size_t bytes, void *user)
{
    frame(pcm, bytes, false, user);
}

int main(void)
{
    Capture capture = {0};
    ChiakiSession session = {0};
    session.haptics_sink.user = &capture;
    session.veyra_haptics_frame_cb = frame;
    ChiakiAudioReceiver receiver;
    if (chiaki_audio_receiver_init(&receiver, &session, NULL) != CHIAKI_ERR_SUCCESS)
        return 1;
    packet(&receiver, 65533, 1, false); // Joining mid-stream is not evidence of loss.
    packet(&receiver, 65534, 2, false);
    packet(&receiver, 0, 4, true);      // FEC recovers 65535, then source 0 wraps.
    packet(&receiver, 0, 4, false);     // Duplicate.
    packet(&receiver, 65535, 3, false); // Late retransmission.
    packet(&receiver, 3, 7, true);      // Missing 1, recovered 2, source 3.
    packet(&receiver, 4, 8, false);
    chiaki_audio_receiver_fini(&receiver);
    const int expected[] = {1, 2, 3, 4, -1, 6, 7, 8};
    if (capture.count != sizeof(expected)/sizeof(*expected) ||
        memcmp(capture.events, expected, sizeof(expected))) {
        fprintf(stderr, "Expected PCM 1 2 3 4, loss, PCM 6 7 8. Received");
        for (unsigned i = 0; i < capture.count && i < 16; ++i)
            fprintf(stderr, " %d", capture.events[i]);
        fputc('\n', stderr);
        return 1;
    }
    memset(&capture, 0, sizeof(capture));
    session.veyra_haptics_frame_cb = NULL;
    session.haptics_sink.frame_cb = physical_frame;
    if (chiaki_audio_receiver_init(&receiver, &session, NULL) != CHIAKI_ERR_SUCCESS)
        return 1;
    packet(&receiver, 1, 11, false);
    packet(&receiver, 3, 13, true);
    chiaki_audio_receiver_fini(&receiver);
    if (capture.count != 2 || capture.events[0] != 11 || capture.events[1] != 13) {
        fputs("The original physical sink changed its PCM delivery order.\n", stderr);
        return 1;
    }
    puts("The new sink reported one gap before the next PCM unit. The original sink delivered PCM 11 and 13.");
    return 0;
}
