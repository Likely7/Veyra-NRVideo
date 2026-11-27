// A card made of threads: same control codes as the driver, for trying the rest of the chain on a PC
// without the hardware. Active only when the environment variable XCAP_SIM names a control file
// (key=value words, read again all the time): lock=1 hdr=1 rate=59940 repeat=2 step=12 audio=2 carrier=<file>
#pragma once
#include <windows.h>
namespace xcap {
bool simEnabled();
bool simIoctl(DWORD code, const void* in, void* out, DWORD& error);
}
