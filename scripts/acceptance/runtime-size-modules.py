"""List DLL paths for our own test subprocess, never enumerate user processes."""
import ctypes
from ctypes import wintypes
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
psapi=ctypes.WinDLL('psapi',use_last_error=True)
kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
kernel.CloseHandle.argtypes=[wintypes.HANDLE]
psapi.EnumProcessModulesEx.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.HMODULE),wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),wintypes.DWORD]
psapi.GetModuleFileNameExW.argtypes=[wintypes.HANDLE,wintypes.HMODULE,wintypes.LPWSTR,wintypes.DWORD]
def modules(pid):
    handle=kernel.OpenProcess(0x0400|0x0010,False,pid)
    if not handle:return []
    try:
        array=(wintypes.HMODULE*2048)();needed=wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(handle,array,ctypes.sizeof(array),ctypes.byref(needed),3):return []
        paths=[]
        for h in array[:min(needed.value//ctypes.sizeof(wintypes.HMODULE),len(array))]:
            value=ctypes.create_unicode_buffer(32768)
            if psapi.GetModuleFileNameExW(handle,h,value,len(value)):paths.append(value.value)
        return paths
    finally:kernel.CloseHandle(handle)
