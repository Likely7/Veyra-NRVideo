// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>

namespace veyra::ui {
inline HANDLE attachLocalWorkerJob(DWORD pid) {
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    const HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const bool assigned = child
        && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit))
        && AssignProcessToJobObject(job, child);
    if (child) CloseHandle(child);
    if (assigned) return job;
    CloseHandle(job);
    return nullptr;
}
}
