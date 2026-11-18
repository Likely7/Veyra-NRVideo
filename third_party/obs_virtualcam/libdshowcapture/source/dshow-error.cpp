/*
 * Copyright (C) 2023 Lain Bailey <lain@obsproject.com>
 * LGPL-2.1-or-later; see ../../COPYING.LGPL-2.1.
 * Extracted unchanged from dshow-base.cpp, libdshowcapture
 * 8878638324393815512f802640b0d5ce940161f1, lines 481-498.
 */
#include "dshow-base.hpp"
namespace DShow {
wstring ConvertHRToEnglish(HRESULT hr)
{
    LPWSTR buffer = NULL;
    wstring str;
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_ALLOCATE_BUFFER |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                   (LPTSTR)&buffer, 0, NULL);
    if (buffer) {
        str = buffer;
        LocalFree(buffer);
    }
    return str;
}
}
