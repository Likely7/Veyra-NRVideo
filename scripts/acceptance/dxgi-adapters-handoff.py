"""List DXGI adapters with the LUID format VEYRA_COMPUTE_ADAPTER_LUID expects (16 hex digits)."""
import ctypes
from ctypes import wintypes


class GUID(ctypes.Structure):
    _fields_ = [('Data1', ctypes.c_uint32), ('Data2', ctypes.c_uint16), ('Data3', ctypes.c_uint16), ('Data4', ctypes.c_ubyte * 8)]


class LUID(ctypes.Structure):
    _fields_ = [('LowPart', wintypes.DWORD), ('HighPart', wintypes.LONG)]


class DESC1(ctypes.Structure):
    _fields_ = [('Description', ctypes.c_wchar * 128), ('VendorId', ctypes.c_uint), ('DeviceId', ctypes.c_uint),
                ('SubSysId', ctypes.c_uint), ('Revision', ctypes.c_uint), ('DedicatedVideoMemory', ctypes.c_size_t),
                ('DedicatedSystemMemory', ctypes.c_size_t), ('SharedSystemMemory', ctypes.c_size_t),
                ('AdapterLuid', LUID), ('Flags', ctypes.c_uint)]


# IID_IDXGIFactory1 {770aae78-f26f-4dba-a829-253c83d1b387}
iid = GUID(0x770aae78, 0xf26f, 0x4dba, (ctypes.c_ubyte * 8)(0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87))
factory = ctypes.c_void_p()
assert ctypes.windll.dxgi.CreateDXGIFactory1(ctypes.byref(iid), ctypes.byref(factory)) == 0


def method(obj, index, *argtypes):
    vtable = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    return ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, *argtypes)(vtable[index])


i = 0
while True:
    adapter = ctypes.c_void_p()
    if method(factory, 12, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p))(factory, i, ctypes.byref(adapter)) != 0:
        break
    desc = DESC1()
    method(adapter, 10, ctypes.POINTER(DESC1))(adapter, ctypes.byref(desc))
    luid = (ctypes.c_uint32(desc.AdapterLuid.HighPart).value << 32) | desc.AdapterLuid.LowPart
    print(f'{i} vendor=0x{desc.VendorId:04X} device=0x{desc.DeviceId:04X} luid={luid:016X} flags={desc.Flags} '
          f'vram={desc.DedicatedVideoMemory >> 20}MiB name={desc.Description}')
    method(adapter, 2)(adapter)
    i += 1
method(factory, 2)(factory)
