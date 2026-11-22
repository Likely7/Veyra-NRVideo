#include "veyra/FileIdentity.h"
#include "veyra/engine/CaptureReconnectBudget.h"
#include "veyra/source/CaptureCardSource.h"
#include <windows.h>
#include <vfwmsgs.h>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string_view>

namespace {
int passed = 0;
int failed = 0;

void expect(bool ok, const char* description) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
    if (ok) ++passed;
    else ++failed;
}

void testReconnectBudget() {
    using Budget = veyra::engine::CaptureReconnectBudget;
    using namespace std::chrono_literals;
    auto now = Budget::Clock::time_point{};
    Budget noFrames;
    bool backoffMatched = true;
    unsigned attempts = 0;
    for (const auto expected : {1s, 2s, 4s, 8s, 15s}) {
        const auto decision = noFrames.afterFailure(now, false);
        backoffMatched &= decision.limit == Budget::Limit::None && decision.delay == expected;
        now += decision.delay;
        noFrames.attemptStarted();
        backoffMatched &= noFrames.attempts() == ++attempts;
        // Graph Run succeeded, but three seconds passed without a video frame.
        now += 3s;
    }
    expect(backoffMatched, "no-frame attempts wait 1, 2, 4, 8 and 15 seconds");
    expect(noFrames.afterFailure(now, false).limit == Budget::Limit::NoFrames &&
           noFrames.attempts() == 5, "five reopened graphs without a frame stop automatic reconnect");
    expect(now.time_since_epoch() == 45s, "backoff plus the five no-frame timeouts is 45 seconds in the fixture");

    Budget recovered;
    recovered.attemptStarted();
    recovered.attemptStarted();
    recovered.frameDelivered();
    auto next = recovered.afterFailure(now, false);
    expect(recovered.attempts() == 0 && next.delay == 1s, "a delivered video frame resets consecutive attempts");

    Budget flapping;
    bool allowed = true;
    for (int i = 0; i < 9; ++i) {
        const auto decision = flapping.afterFailure(now, true);
        allowed &= decision.limit == Budget::Limit::None;
        flapping.attemptStarted();
        flapping.frameDelivered();
        now += 1min;
    }
    expect(allowed && flapping.recentRemovals() == 9, "device removals survive successful video recovery");
    expect(flapping.afterFailure(now, true).limit == Budget::Limit::DeviceRemovals,
           "the tenth removal within ten minutes stops automatic reconnect");
    next = flapping.afterFailure(now + 10min, true);
    expect(next.limit == Budget::Limit::None && flapping.recentRemovals() == 1,
           "device removals age out at the rolling ten-minute boundary");
}
}

int main(int argc, char** argv) {
    using namespace veyra::source;
    testReconnectBudget();
    for (const DWORD code : {ERROR_NO_SUCH_DEVICE, ERROR_GEN_FAILURE, ERROR_OPERATION_ABORTED}) {
        const auto hr = HRESULT_FROM_WIN32(code);
        expect(isCaptureDisconnect(hr) && captureFailureAdvice(hr).find(L"USB 3") != std::wstring::npos,
               std::format("0x{:08X} gives the capture disconnection advice", uint32_t(hr)).c_str());
    }
    expect(!isCaptureDisconnect(HRESULT_FROM_WIN32(ERROR_NO_SYSTEM_RESOURCES)) && !isCaptureDisconnect(E_ACCESSDENIED),
           "resource and privacy errors stay distinct from disconnects");
    expect(std::wstring_view(captureFailureAdvice(HRESULT_FROM_WIN32(ERROR_NO_SYSTEM_RESOURCES)))
               .starts_with(L"驱动报告系统资源不足。"), "0x800705AA is a driver resource error");
    expect(std::wstring_view(captureFailureAdvice(HRESULT_FROM_WIN32(ERROR_BUSY)))
               .starts_with(L"设备当前忙。"), "0x800700AA is a busy device");
    expect(std::wstring_view(captureFailureAdvice(E_ACCESSDENIED)).starts_with(L"访问被拒绝。"),
           "access errors point to Windows privacy");
    expect(std::wstring_view(captureFailureAdvice(VFW_E_CANNOT_CONNECT)).starts_with(L"驱动无法连接所选格式。"),
           "DirectShow format errors point to format selection");
    expect(std::wstring_view(captureFailureAdvice(E_FAIL)).starts_with(L"请检查设备连接与驱动"),
           "generic HRESULT gets generic diagnostics");
    if (argc == 2) {
        const auto expected = std::format(L"PID {}", std::strtoul(argv[1], nullptr, 10));
        expect(captureFailureAdvice(HRESULT_FROM_WIN32(ERROR_NO_SYSTEM_RESOURCES)).find(expected)!=std::wstring::npos,
               "resource exhaustion includes the other Veyra PID");
        expect(captureFailureAdvice(HRESULT_FROM_WIN32(ERROR_BUSY)).find(expected)!=std::wstring::npos,
               "busy device includes the other Veyra PID");
        expect(captureFailureAdvice(E_ACCESSDENIED).find(expected)==std::wstring::npos,
               "access denied keeps the privacy advice");
    }

    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 2;
    // Fake moniker, so the test can reserve it without binding a real driver.
    const auto path = std::format(L"veyra-test-capture-{}-{}", GetCurrentProcessId(), GetTickCount64());
    const auto key = veyra::sha256Hex(reinterpret_cast<const uint8_t*>(path.data()), path.size() * sizeof(wchar_t));
    const auto name = L"Local\\Veyra.Capture." + std::wstring(key.begin(), key.end());
    CaptureDevice device{L"fixture", path};
    SourceOpenDesc desc{CaptureCardSource::makeCapturePath(0, device, 1, kCaptureAudioDisabled, nullptr)};
    {
        CaptureCardSource capture;
        const HANDLE owner = CreateMutexW(nullptr, FALSE, name.c_str());
        expect(owner != nullptr, "mock reservation created");
        expect(!capture.configure(desc) && capture.errorMessage().starts_with(L"另一个 Veyra"),
               "same-device reservation stops capture before driver negotiation");
        if (owner) CloseHandle(owner);
        expect(!capture.configure(desc)
               && capture.errorMessage().find(std::format(L"0x{:08X}", uint32_t(VFW_E_NOT_FOUND))) != std::wstring::npos,
               "retry after release reaches negotiation and reports the device HRESULT");
        const HANDLE leaked = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
        const DWORD error = GetLastError();
        expect(!leaked && error == ERROR_FILE_NOT_FOUND, "failed negotiation releases reservation immediately");
        if (leaked) CloseHandle(leaked);
    }
    CoUninitialize();
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
