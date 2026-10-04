"""Read Windows GPU Engine counters; report only one owned test PID."""
import ctypes
from ctypes import wintypes

class Value(ctypes.Structure):
    _fields_=[('status',wintypes.DWORD),('value',ctypes.c_double)]
class Item(ctypes.Structure):
    _fields_=[('name',wintypes.LPWSTR),('value',Value)]

class GpuProcessMeter:
    def __init__(self,pid):
        self.pid=pid;self.query=wintypes.HANDLE();self.counter=wintypes.HANDLE();self.api=ctypes.WinDLL('pdh')
        signatures={
            'PdhOpenQueryW':([wintypes.LPCWSTR,ctypes.c_size_t,ctypes.POINTER(wintypes.HANDLE)],wintypes.LONG),
            'PdhAddEnglishCounterW':([wintypes.HANDLE,wintypes.LPCWSTR,ctypes.c_size_t,ctypes.POINTER(wintypes.HANDLE)],wintypes.LONG),
            'PdhCollectQueryData':([wintypes.HANDLE],wintypes.LONG),
            'PdhGetFormattedCounterArrayW':([wintypes.HANDLE,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.POINTER(wintypes.DWORD),ctypes.c_void_p],wintypes.LONG),
            'PdhCloseQuery':([wintypes.HANDLE],wintypes.LONG)}
        for name,(args,result) in signatures.items():
            fn=getattr(self.api,name);fn.argtypes=args;fn.restype=result
        self.openStatus=self.api.PdhOpenQueryW(None,0,ctypes.byref(self.query))
        self.addStatus=self.api.PdhAddEnglishCounterW(self.query,r'\GPU Engine(*)\Utilization Percentage',0,ctypes.byref(self.counter)) if self.openStatus==0 else self.openStatus
        self.lastStatus=self.api.PdhCollectQueryData(self.query) if self.addStatus==0 else self.addStatus
    def sample(self):
        status=self.api.PdhCollectQueryData(self.query) if self.addStatus==0 else self.addStatus
        if status!=0:return {'pdhStatus':hex(status&0xffffffff),'engines':{},'measured':False}
        size=wintypes.DWORD();count=wintypes.DWORD()
        status=self.api.PdhGetFormattedCounterArrayW(self.counter,0x8200,ctypes.byref(size),ctypes.byref(count),None)
        if (status&0xffffffff)!=0x800007d2 or not size.value:return {'pdhStatus':hex(status&0xffffffff),'engines':{},'measured':False}
        buffer=ctypes.create_string_buffer(size.value)
        status=self.api.PdhGetFormattedCounterArrayW(self.counter,0x8200,ctypes.byref(size),ctypes.byref(count),buffer)
        if status!=0:return {'pdhStatus':hex(status&0xffffffff),'engines':{},'measured':False}
        items=ctypes.cast(buffer,ctypes.POINTER(Item));engines={}
        for i in range(count.value):
            item=items[i]
            if item.name and item.name.startswith(f'pid_{self.pid}_') and item.value.status in (0,1):engines[item.name]=item.value.value
        return {'pdhStatus':'0x0','engines':engines,'maxEnginePercent':max(engines.values(),default=0),'sumEnginePercent':sum(engines.values()),'measured':bool(engines)}
    def close(self):
        if self.query:self.api.PdhCloseQuery(self.query);self.query=wintypes.HANDLE()
