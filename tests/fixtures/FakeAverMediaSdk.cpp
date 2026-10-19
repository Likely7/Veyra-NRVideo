// Source-only test double for the public VendorSdk / DeviceOpener ABI.
#include <windows.h>
#include <string>
namespace {
int format=-236,armed=0,queryError=0,writeError=0,applyWrite=1,initialized=0;
unsigned writes=0,disarms=0,inits=0,ctors=0,monitorStarts=0;
}
extern "C" {
__declspec(dllexport) void FakeConfigure(int f,int a,int q,int w,int update){format=f;armed=a;queryError=q;writeError=w;applyWrite=update;writes=disarms=0;}
__declspec(dllexport) unsigned FakeCount(unsigned n){return n==0?writes:n==1?disarms:n==2?inits:n==4?monitorStarts:ctors;}
void* SdkCtor(void* self,const char*){++ctors;return self;}
void SdkDtor(void*){}
int SdkInitialize(void*){initialized=1;++inits;return 0;}
int SdkUninitialize(void*){initialized=0;return 0;}
int SdkClose(void*){return 0;}
int SdkFormat(void*,int* value){if(!initialized||queryError==2)return -1;*value=format;return 0;}
int SdkGet(void*,unsigned* value){if(queryError==3)RaiseException(0xC0000005,0,0,nullptr);if(!initialized||queryError==1)return -1;*value=unsigned(armed);return 0;}
int SdkSet(void*,int value){++writes;if(!value)++disarms;if(!initialized||writeError)return -1;if(applyWrite)armed=value;return 0;}
void* OpenerCtor(void* self){return self;}
void OpenerDtor(void*){}
void OpenerBind(void*,void*){}
void OpenerSwitch(void*,const void*){}
bool OpenerNonPcm(void*){return (format&255)==20;}
void OpenerStart(void*){++monitorStarts;}
void OpenerStop(void*){}
}
