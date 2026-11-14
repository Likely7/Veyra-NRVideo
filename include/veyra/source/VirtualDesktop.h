#pragma once
// Windows virtual desktops (Win+Ctrl+D, Task View). The shell cloaks every window on a
// desktop other than the one shown, so a cloaked-window filter hides them, yet Windows
// Graphics Capture keeps composing such a window: a capture can follow a program that
// runs on another desktop while Veyra is shown on this one.
//
// Placement comes from the documented IVirtualDesktopManager. The Task View order and
// the names users give desktops are only kept in Explorer's per-user registry state;
// that is read, never written, and a missing or changed layout just leaves the number
// and name unknown.
#include <guiddef.h>
#include <memory>
#include <string>
#include <vector>

namespace veyra::source::vdesk {
struct Desktop {
    GUID id{};
    std::wstring name; // the name the user gave it in Task View, empty if never renamed
};
// The desktops in Task View order. Empty when Windows does not say (one desktop only
// on some builds, or a registry layout this code does not know).
std::vector<Desktop> desktops();

struct Placement {
    bool other=false;   // the window is on a virtual desktop that is not shown now
    unsigned number=0;  // 1-based Task View position of that desktop, 0 if unknown
    std::wstring name;  // the user's name for that desktop, empty if unnamed
};
// One manager and one desktop list, for classifying many windows of one enumeration.
// Construct and use it on one thread; it initializes COM for itself when needed.
class Lookup {
public:
    Lookup();
    ~Lookup();
    Lookup(const Lookup&)=delete;
    Lookup& operator=(const Lookup&)=delete;
    bool available() const; // false when the virtual desktop manager is unavailable
    // True only for a window the shell cloaked because it is on another desktop.
    // Every other cloaked window (a suspended store app's frame, a shell surface,
    // a window on no listed desktop) returns false. window is an HWND.
    bool otherDesktop(void* window, Placement& out) const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
// Cheap check without COM, safe on any thread: the shell cloaked this window. That is
// what happens to every window on a desktop that is not shown (and to a few shell
// surfaces, which are not capture targets).
bool shellCloaked(void* window);
}
