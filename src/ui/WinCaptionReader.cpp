// SPDX-License-Identifier: GPL-3.0-only
// Live Captions UI Automation element discovered in jjjxxxttt's Veyra PR #203.
#include "WinCaptionReader.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>

namespace veyra::ui {
using Microsoft::WRL::ComPtr;

QString windowsSystemExecutable(const wchar_t* name) {
    wchar_t system[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(system, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return QString::fromWCharArray(system, n) + QLatin1Char('/') + QString::fromWCharArray(name);
}

namespace {
struct CaptionWindow {
    std::wstring exe;
    HWND hwnd = nullptr;
};

BOOL CALLBACK findCaptionWindow(HWND hwnd, LPARAM param) {
    auto& result = *reinterpret_cast<CaptionWindow*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    wchar_t path[MAX_PATH]{};
    DWORD length = MAX_PATH;
    const bool match = QueryFullProcessImageNameW(process, 0, path, &length) &&
        CompareStringOrdinal(path, int(length), result.exe.c_str(), int(result.exe.size()), TRUE) == CSTR_EQUAL;
    CloseHandle(process);
    if (!match || !IsWindowVisible(hwnd)) return TRUE;
    result.hwnd = hwnd;
    return FALSE;
}

QString captionText(IUIAutomationElement* element) {
    BSTR value = nullptr;
    ComPtr<IUIAutomationTextPattern> pattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern)))) {
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(pattern->get_DocumentRange(&range)) && range) {
            // Read a bounded rolling window from the end of the document. Keep
            // complete lines so sentence IDs survive revisions of trailing words.
            int moved = 0;
            ComPtr<IUIAutomationTextRange> tail;
            if (SUCCEEDED(range->Clone(&tail)) && tail &&
                SUCCEEDED(tail->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range.Get(), TextPatternRangeEndpoint_End)) &&
                SUCCEEDED(tail->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -4096, &moved)))
                tail->GetText(4096, &value);
            if (!value) range->GetText(4096, &value);
        }
    }
    if (!value) element->get_CurrentName(&value);
    QString text;
    if (value) {
        text = QString::fromWCharArray(value, int(SysStringLen(value)));
        SysFreeString(value);
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text.right(4096).trimmed();
}

int readCaptions(HANDLE input, HANDLE output) {
    ComPtr<IUIAutomation2> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))))
        return 2;
    automation->put_ConnectionTimeout(1000);
    automation->put_TransactionTimeout(1500);
    VARIANT id{};
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"CaptionsTextBlock");
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &condition);
    VariantClear(&id);
    if (FAILED(hr)) return 2;
    CaptionWindow window{windowsSystemExecutable(L"LiveCaptions.exe").replace(QLatin1Char('/'), QLatin1Char('\\')).toStdWString()};
    QByteArray previous;
    unsigned ticks = 0;
    ComPtr<IUIAutomationElement> element;
    while (PeekNamedPipe(input, nullptr, 0, nullptr, nullptr, nullptr)) {
        if (!IsWindow(window.hwnd) || ticks % 12 == 0) {
            window.hwnd = nullptr;
            EnumWindows(findCaptionWindow, reinterpret_cast<LPARAM>(&window));
            element.Reset();
        }
        if (window.hwnd && !element) {
            ComPtr<IUIAutomationElement> root;
            if (SUCCEEDED(automation->ElementFromHandle(window.hwnd, &root)) && root)
                root->FindFirst(TreeScope_Descendants, condition.Get(), &element);
        }
        const QString text = element ? captionText(element.Get()) : QString{};
        if (text.isEmpty()) element.Reset();
        const bool ready = window.hwnd != nullptr;
        QByteArray bytes = QJsonDocument(QJsonObject{{"ready", ready}, {"text", text}})
            .toJson(QJsonDocument::Compact);
        if (bytes != previous || ticks % 4 == 0) {
            previous = bytes;
            bytes += '\n';
            DWORD written = 0;
            if (!WriteFile(output, bytes.data(), DWORD(bytes.size()), &written, nullptr)) return 0;
        }
        ++ticks;
        Sleep(250);
    }
    return 0;
}
} // namespace

int runLiveCaptionReader() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    // This worker only runs as an owned subprocess with private pipes.
    if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    const int result = readCaptions(input, output);
    CoUninitialize();
    return result;
}
} // namespace veyra::ui
