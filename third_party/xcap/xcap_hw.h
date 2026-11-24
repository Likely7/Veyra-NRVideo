// XCAP hardware contract shared by the FPGA, the Windows driver and the host library.
// Keep in sync with fpga/rtl/xcap_regs.sv and fpga/rtl/xcap_framer.sv.
#pragma once
#ifndef _NTDDK_
#include <stdint.h>
#else
typedef unsigned char uint8_t; typedef unsigned short uint16_t;
typedef unsigned int uint32_t; typedef unsigned __int64 uint64_t;
#endif

#define XCAP_VENDOR_ID   0x10EE
#define XCAP_DEVICE_ID   0xA15C

// ---- BAR0: user registers (AXI-Lite, 32-bit) ----
#define XCAP_REG_ID            0x000  // 0x58434150 'XCAP'
#define XCAP_REG_VERSION       0x004
#define XCAP_REG_CTRL          0x008
#define XCAP_REG_STATUS        0x00C
#define XCAP_REG_DATA_BEATS    0x010  // pixel bytes / 16 of one frame
#define XCAP_REG_PAD_BEATS     0x014  // zero beats after the pixels (page rounding)
#define XCAP_REG_GEOM          0x018  // width | height << 16   (copied to the frame header)
#define XCAP_REG_STRIDE        0x01C  // bytes per line          (copied to the frame header)
#define XCAP_REG_PIXFMT        0x020  // XCAP_PIXFMT_*           (packer + frame header)
#define XCAP_REG_SLICE_BEATS   0x024  // beats per CRC band
#define XCAP_REG_TS_LO         0x030  // 250 MHz counter; reading LO latches HI
#define XCAP_REG_TS_HI         0x034
#define XCAP_REG_FRAMES_IN     0x038  // frames whose first pixel reached the FIFO writer
#define XCAP_REG_FRAMES_OUT    0x03C  // frames completely handed to the DMA
#define XCAP_REG_FRAMES_DROP   0x040  // input frames skipped because the previous one was not out
#define XCAP_REG_FRAMES_BAD    0x044  // frames padded after overflow / short input
#define XCAP_REG_FIFO_MAX      0x048  // highest FIFO fill (16-byte words); write clears
#define XCAP_REG_STALL_MAX     0x04C  // longest DMA back-pressure in 4 ns ticks; write clears
#define XCAP_REG_FLUSH          0x050  // write 1 video / 2 audio: send one 16-byte packet (only while stopped); lets the DMA engine stop.
                                      // hw >= 00030301: the packet is kept until it can go out (frame under way, DMA not ready), and
                                      // bits 2 (video) / 3 (audio) abort: drop what the stopped stream still holds and return to idle
#define XCAP_FLUSH_VIDEO        0x1u
#define XCAP_FLUSH_AUDIO        0x2u
#define XCAP_FLUSH_ABORT_VIDEO  0x4u   // hw >= 00030301; acts only while XCAP_CTRL_STREAM_EN is clear
#define XCAP_FLUSH_ABORT_AUDIO  0x8u   // hw >= 00030301; acts only while XCAP_CTRL_AUDIO_EN is clear
                                      // (without an abort, a stopped stream whose data the DMA does not take gives up by itself after 67 ms)
#define XCAP_REG_PT_DROPS       0x054  // read only, hw >= 00030301: loop-out video frames cut short | loop-out sound subframes dropped << 16 (both wrap)
#define XCAP_REG_HOST_CTRL      0x058  // bit 0: hold HDMI hot plug low, bit 4: built-in EDID with bitstream audio, bit 5: use the host EDID, 15:8 its number (1..255)
#define XCAP_REG_TX_CTRL       0x05C  // HDMI OUT (hw >= 00030200): bit 0 test pattern on, 3:1 mode, 7:4 audio, 8 second HDR set, 9 ignore hot plug (no sink), 31:16 restart number;
                                      // with bit 0 clear the output repeats HDMI IN (loop-out, hw >= 00030300, firmware >= 00050000) unless bit 10 is set
#define XCAP_TX_FORCE_CONNECTED 0x200u
#define XCAP_TX_LOOPOUT_OFF     0x400u
#define XCAP_REG_TPG_MODE      0x060  // XCAP_TPG_*
#define XCAP_REG_TPG_REPEAT    0x064  // identical frames per picture (2 = 30 fps in 60 Hz)
#define XCAP_REG_TPG_H         0x068  // active pixel pairs | total pixel pairs << 16
#define XCAP_REG_TPG_V         0x06C  // active lines | total lines << 16
#define XCAP_REG_VCLK_HZ        0x070  // video clock (0 = 297 MHz, old firmware)
#define XCAP_REG_HDMI0          0x0C0  // 16 words written by the HDMI firmware (read only)
#define XCAP_REG_TPG_AUDIO      0x074  // test audio words per second (96000 stereo, >= 300000 eight channels)
#define XCAP_REG_AUD_PACKETS    0x078
#define XCAP_REG_AUD_OVERFLOW   0x07C
#define XCAP_REG_EDID          0x800  // 64 words, write only: EDID the firmware shows when HOST_CTRL bit 5 is set (hw >= 00030100)
#define XCAP_REG_META0         0x080  // 16 words of signal metadata, copied to the frame header
#define XCAP_META_WORDS        16

#define XCAP_ID_MAGIC          0x58434150u
#define XCAP_CTRL_STREAM_EN    0x00000001u
#define XCAP_CTRL_SRC_HDMI     0x00000002u
#define XCAP_CTRL_TPG_EN       0x00000004u
#define XCAP_CTRL_AUDIO_EN     0x00000008u
#define XCAP_STATUS_BUSY       0x00000001u  // framer is inside a frame
#define XCAP_STATUS_VID_LOCK   0x00000002u  // selected video clock is running
#define XCAP_STATUS_OVERFLOW   0x00000004u  // sticky, cleared by writing 1

#define XCAP_PIXFMT_V210       0u  // 4:2:2 10-bit, 16 bytes per 6 pixels
#define XCAP_PIXFMT_BGRX8      1u  // 4 bytes per pixel, B G R X

#define XCAP_TPG_VERIFY        0u  // Y=(x+3y+7n) C=(x^y^n) pattern, host-checkable
#define XCAP_TPG_BARS          1u  // colour bars + moving box

// ---- Frame layout in host memory ----
// [4096 header][pixels, rounded up to 4096][4096 trailer]
#define XCAP_HDR_BYTES   4096u
#define XCAP_TRL_BYTES   4096u
#define XCAP_HDR_MAGIC   0x48464358u  // 'XCFH'
#define XCAP_TRL_MAGIC   0x54464358u  // 'XCFT'
#define XCAP_MAX_BANDS   60

#define XCAP_FRAME_BAD         0x1u   // pixels padded: overflow or short input
#define XCAP_FRAME_REPEAT      0x2u   // pixel data identical to the previous frame
#define XCAP_FRAME_DISCONT     0x4u   // input frames were dropped before this one

// ---- Audio packets (second DMA channel), fixed size ----
#define XCAP_AUD_PACKET        2048u
#define XCAP_AUD_WORDS         508u
#define XCAP_AUD_MAGIC         0x55414358u // 'XCAU'
#define XCAP_AUD_SAMPLE(w)     ((w) & 0xFFFFFFu)
#define XCAP_AUD_CHANNEL(w)    (((w) >> 24) & 7u)
#define XCAP_AUD_VALIDITY      0x08000000u
#define XCAP_AUD_CHSTATUS      0x10000000u
#define XCAP_AUD_BLOCK_START   0x20000000u

#pragma pack(push, 4)
typedef struct xcap_audio_packet {
    uint32_t magic, seq, words, ts;          // ts: low 32 bits of the 250 MHz counter
    uint32_t word[XCAP_AUD_WORDS];
} xcap_audio_packet;
typedef struct xcap_frame_header {
    uint32_t magic, version, seq, flags;
    uint64_t ts_first_pixel;                 // 250 MHz ticks
    uint32_t geom, stride;
    uint32_t pixfmt, data_bytes, reserved0, reserved1;
    uint32_t meta[XCAP_META_WORDS];
} xcap_frame_header;

typedef struct xcap_frame_trailer {
    uint32_t magic, seq, flags, fifo_max;
    uint64_t ts_last_pixel, ts_last_out;     // last pixel into FIFO / last pixel beat into DMA
    uint32_t frames_drop, frames_bad, stall_max, bands;
    uint32_t crc[4];                         // whole frame, CRC-32C per 32-bit lane
    uint32_t band_crc[XCAP_MAX_BANDS][4];    // running value of crc[] at each band end
} xcap_frame_trailer;
#pragma pack(pop)

// metadata words (written by the HDMI side, or by the host while on the test pattern)
#define XCAP_META_FLAGS      0   // bit0 HDR infoframe present, bit1 BT.2020, bit2 full range, bits 7:4 EOTF
#define XCAP_META_PRIM_GB    1   // ST 2086, packed like the HDMI DRM infoframe, 4 words
#define XCAP_META_AUDIO      5   // rate/100 Hz | channels << 16 | format << 24 (0 none, 1 PCM-layout packets, 2 HBR)
#define XCAP_META_MASTER_LUM 6   // max | min << 16
#define XCAP_META_LIGHT      7   // MaxCLL | MaxFALL << 16
#define XCAP_META_RATE       8   // frame rate in mHz (59940, 60000)
#define XCAP_META_GEOM       9   // detected width | height << 16, 0 = no signal
#define XCAP_META_STATE      10  // bit0 locked, bit4 built-in Atmos EDID, bit5 host EDID (number in 31:24), bit6 HDMI card answers on I2C, bit7 cable 5 V, bits 3:1 colour (0 RGB 1 4:2:2 2 4:4:4 3 4:2:0), 15:8 bits, 23:16 VIC
#define XCAP_META_CHANGES    11  // stream up/down events
#define XCAP_META_ERRORS     12  // PHY errors | bridge overflows << 16
#define XCAP_META_FW         13  // firmware version, 0 = no HDMI firmware
#define XCAP_META_BEAT       14  // heartbeat
#define XCAP_META_REFCLK     15  // TMDS reference clock, kHz
