#pragma once

namespace veyra::portable::launcherText {
// Native launcher dialogs stay bilingual even when the player or Qt is missing.
// The Chinese strings are also the source keys for the translation catalog.
inline constexpr wchar_t missing[] = L"找不到主程序文件。请使用官方安装包修复，或检查 Windows 安全中心的保护历史记录。";
inline constexpr wchar_t redirected[] = L"主程序路径包含链接或云端占位文件。请将 Veyra 放在普通的本地文件夹中。";
inline constexpr wchar_t wrongType[] = L"主程序路径的文件类型不正确。请使用官方安装包修复。";
inline constexpr wchar_t inspect[] = L"Windows 无法读取主程序路径。";
inline constexpr wchar_t start[] = L"Windows 无法启动主程序。";
inline constexpr wchar_t wait[] = L"Windows 等待主程序退出时失败。";
inline constexpr wchar_t exitCode[] = L"Windows 无法读取主程序的退出代码。";
inline constexpr wchar_t denied[] = L"请检查安装目录的访问权限、系统策略和安全软件的保护历史记录。";
inline constexpr wchar_t damaged[] = L"文件可能已损坏，或与此 Windows 系统不兼容。请使用官方安装包修复。";
inline constexpr wchar_t blocked[] = L"Windows 报告文件被安全软件拦截或删除。请检查保护历史记录。";
inline constexpr wchar_t lock[] = L"Windows 无法锁定安装目录。请检查 Veyra 文件夹的访问权限。";
inline constexpr wchar_t dependency[] = L"主程序缺少运行组件。请使用官方安装包修复。";
inline constexpr wchar_t windowsError[] = L"Windows 错误 {}";
inline constexpr wchar_t logPath[] = L"诊断文件";
}
