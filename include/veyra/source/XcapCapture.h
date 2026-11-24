#pragma once
// XCAP: the project's own PCIe HDMI capture card (AXAU15 FPGA + XDMA).
//
// It plugs into the native-capture state machine of DeckLinkCapture.h as another
// Driver, so signal loss, format changes, audio routing and reconnects behave the
// same. What differs is delivery: the card writes a frame into host memory while
// the HDMI lines arrive, and the frame is handed over at its FIRST slice. The
// ingest copy then follows the DMA slice by slice (Frame::awaitRows), so it ends
// a fraction of a millisecond after the last line instead of starting there.
#include "veyra/source/DeckLinkCapture.h"

namespace veyra::source::xcapture {
bool isPath(std::wstring_view);
std::vector<decklink::Device> devices();
std::vector<decklink::Mode> modes(std::wstring_view);
// Shows the console the EDID of this PC's TV; true when a signal is there.
bool prepare(std::wstring_view path);
std::unique_ptr<decklink::Driver> makeDriver();
}
