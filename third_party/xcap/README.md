# XCAP host library (bundled copy)

User-mode receiver for the XCAP PCIe HDMI capture card, used by `src/source/XcapCapture.cpp` and `XcapAudio.cpp`.

- `xcap.h/.cpp`: open the card, start/stop capture, map the frame ring, follow slice progress and audio packets, correlate the card clock with QPC.
- `xcap_edid.h/.cpp`: read monitor EDIDs and compose the EDID offered to the HDMI source from the TV capabilities.
- `xcap_sim.h/.cpp`: a virtual card for testing without hardware (`XCAP_SIM`).
- `xcap_hw.h`, `xcap_ioctl.h`: register map and driver interface shared with the FPGA and the Windows driver.

Origin: https://github.com/isaac-cheng-if/Veyra-NRVideo (branch `xcap`, folders `xcap/host` and `xcap/common`).
License: GPL-3.0, the same as Veyra. No binaries, SDKs or vendor code are included.
