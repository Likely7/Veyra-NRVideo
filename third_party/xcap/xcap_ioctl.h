// XCAP driver <-> user mode contract.
#pragma once
#include "xcap_hw.h"

// {5B2E2C7A-8B0C-4E0E-9C7B-6E1D0C3A15C1}
#ifdef DEFINE_GUID
DEFINE_GUID(GUID_DEVINTERFACE_XCAP, 0x5b2e2c7a, 0x8b0c, 0x4e0e, 0x9c, 0x7b, 0x6e, 0x1d, 0x0c, 0x3a, 0x15, 0xc1);
#endif

#define XCAP_DRV_VERSION   0x00020001u
#define XCAP_MAX_SLOTS     16u
#define XCAP_MAX_SLICES    128u
#define XCAP_CTL_BYTES     16384u
#define XCAP_AUD_RING      512u            // audio packets in the ring (about 0.4 s of stereo PCM, 0.34 s of HBR)

#define XCAP_IOCTL(n)      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900 + (n), METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define XCAP_IOCTL_INFO    XCAP_IOCTL(0)   // out: xcap_info
#define XCAP_IOCTL_REG_RD  XCAP_IOCTL(1)   // in: xcap_reg, out: xcap_reg
#define XCAP_IOCTL_REG_WR  XCAP_IOCTL(2)   // in: xcap_reg
#define XCAP_IOCTL_CLOCK   XCAP_IOCTL(3)   // out: xcap_clock
#define XCAP_IOCTL_START   XCAP_IOCTL(4)   // in: xcap_start_in, out: xcap_start_out
#define XCAP_IOCTL_STOP    XCAP_IOCTL(5)

#pragma pack(push, 8)
typedef struct xcap_reg  { uint32_t offset, value; } xcap_reg;
typedef struct xcap_clock { uint64_t fpga_ticks; uint64_t qpc_before, qpc_after; } xcap_clock;
typedef struct xcap_info {
    uint32_t drv_version, hw_version, xdma_c2h_id, xdma_cfg_id;
    uint32_t link_speed, link_width, max_payload, max_read_request;
    uint32_t streaming, engine_stuck, status_reg, reserved;
    uint64_t irq_total, start_total;
} xcap_info;

typedef struct xcap_start_in {
    uint32_t size;            // sizeof(xcap_start_in)
    uint32_t slots;           // 2..XCAP_MAX_SLOTS
    uint32_t data_bytes;      // pixel bytes of one frame, multiple of 16
    uint32_t slice_bytes;     // progress is reported every slice_bytes of pixels; multiple of 16; at most XCAP_MAX_BANDS slices per frame
    uint32_t geom, stride, pixfmt;
    uint32_t ctrl;            // XCAP_CTRL_SRC_HDMI / XCAP_CTRL_TPG_EN
    uint64_t event;           // optional user event handle, set on every slice
    uint64_t audio_event;     // optional, set on every audio packet
    uint32_t audio;           // 1: also stream audio
    uint32_t reserved;
} xcap_start_in;

typedef struct xcap_start_out {
    uint32_t frame_bytes;     // header + pixels + pad + trailer
    uint32_t slices;          // per frame
    uint32_t descs_per_ring;  // hardware descriptors for one pass over all slots
    uint32_t contiguous_kb;   // size of the physically contiguous chunks obtained
    uint64_t ctl;             // user address of xcap_ctl (read only)
    uint64_t slot[XCAP_MAX_SLOTS]; // user address of each frame slot (read only)
    uint64_t audio_ring;      // user address of XCAP_AUD_RING packets, 0 when audio is off
} xcap_start_out;

// Mapped read only into the capturing process.
typedef struct xcap_ctl {
    volatile uint32_t hw_desc_count;   // written by the DMA engine itself: bit31 error, 23:0 descriptors done
    uint32_t pad0[15];
    volatile uint32_t state;           // 1 streaming, 2 stopped, 3 error
    volatile uint32_t engine_status;   // last non-zero engine status seen
    volatile uint32_t align_errors;    // frames whose last descriptor was not the end of a packet
    volatile uint32_t irq_count;
    volatile uint64_t last_irq_qpc;
    volatile uint32_t sw_desc_count;   // descriptors done as seen by the last interrupt
    uint32_t pad1[9];
    volatile uint32_t aud_hw_count;    // audio packets done, written by the second DMA engine
    uint32_t pad2[15];
    // descriptors completed, counted from the start of the ring pass, when slice i of slot s is complete
    uint32_t slice_end[XCAP_MAX_SLOTS][XCAP_MAX_SLICES];
} xcap_ctl;
#pragma pack(pop)
