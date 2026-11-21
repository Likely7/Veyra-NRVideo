#include "veyra/FileIdentity.h"
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
}

int main(int argc, char** argv) {
    using namespace veyra::source;
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
