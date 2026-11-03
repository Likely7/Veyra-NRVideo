// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <io.h>
#include <string>

namespace veyra::pipeline::rdna3::diagnostics {
inline std::atomic<const char*> currentPhase{"before-init"};
inline FILE* mainLog=nullptr;
inline wchar_t exceptionPath[2048]{};
inline LPTOP_LEVEL_EXCEPTION_FILTER previousFilter=nullptr;

// Do not recover from a corrupt third-party stack or GPU context. Write a small
// independent exception record, then leave normal Windows crash handling intact.
inline LONG WINAPI exceptionRecord(EXCEPTION_POINTERS* exception) {
    const auto* record=exception?exception->ExceptionRecord:nullptr;
    if(record&&exceptionPath[0]){
        HANDLE file=CreateFileW(exceptionPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){
            char line[768]{};
            const int size=_snprintf_s(line,sizeof(line),_TRUNCATE,
                "worker pid=%lu tid=%lu phase=%s exception=0x%08lX address=%p parameters=%lu\r\n",
                GetCurrentProcessId(),GetCurrentThreadId(),currentPhase.load(std::memory_order_relaxed),
                record->ExceptionCode,record->ExceptionAddress,record->NumberParameters);
            DWORD written=0;
            if(size>0)WriteFile(file,line,DWORD(size),&written,nullptr);
            if(record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&record->NumberParameters>=2){
                const int count=_snprintf_s(line,sizeof(line),_TRUNCATE,"access=%llu target=0x%llX\r\n",
                    static_cast<unsigned long long>(record->ExceptionInformation[0]),
                    static_cast<unsigned long long>(record->ExceptionInformation[1]));
                if(count>0)WriteFile(file,line,DWORD(count),&written,nullptr);
            }
            FlushFileBuffers(file);CloseHandle(file);
        }
    }
    return previousFilter?previousFilter(exception):EXCEPTION_CONTINUE_SEARCH;
}

inline bool begin(FILE* log,const wchar_t* path) {
    if(!log||!path)return false;
    const std::wstring stderrPath=std::wstring(path)+L".stderr.log";
    if(wcsncpy_s(exceptionPath,std::wstring(path).append(L".exception.log").c_str(),_TRUNCATE))return false;
    FILE* redirected=nullptr;
    if(_wfreopen_s(&redirected,stderrPath.c_str(),L"ab",stderr)||!redirected)return false;
    setvbuf(redirected,nullptr,_IONBF,0);
    const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(redirected)));
    if(handle==INVALID_HANDLE_VALUE||!SetStdHandle(STD_ERROR_HANDLE,handle)||!SetStdHandle(STD_OUTPUT_HANDLE,handle))return false;
    mainLog=log;
    wchar_t backtrace[8]{};
    if(!GetEnvironmentVariableW(L"RUST_BACKTRACE",backtrace,DWORD(std::size(backtrace))))SetEnvironmentVariableW(L"RUST_BACKTRACE",L"1");
    // This is an owned background process. Suppress a Windows crash dialog;
    // the error code, stderr and Windows Application events remain available.
    SetErrorMode(GetErrorMode()|SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    previousFilter=SetUnhandledExceptionFilter(exceptionRecord);
    fprintf(log,"worker diagnostics=2 pid=%lu stderr=%ls\n",GetCurrentProcessId(),stderrPath.c_str());fflush(log);
    return true;
}

inline void phase(const char* value) {
    currentPhase.store(value,std::memory_order_relaxed);
    if(mainLog){fprintf(mainLog,"phase=%s\n",value);fflush(mainLog);}
}

inline void finish() {
    SetUnhandledExceptionFilter(previousFilter);mainLog=nullptr;
}
} // namespace veyra::pipeline::rdna3::diagnostics
