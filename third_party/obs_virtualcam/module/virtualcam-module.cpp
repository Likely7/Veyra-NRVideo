// Adapted for Veyra from OBS Studio 7e3aa99811798255bd1a13a6da7260ada76a4d95.
// GPL-2.0-or-later; original attribution and exact file hashes: ../UPSTREAM.json.
#include "virtualcam-filter.hpp"
#include "virtualcam-guid.h"
#include "CameraRegistration.h"

/* ========================================================================= */

static const REGPINTYPES AMSMediaTypesV = {&MEDIATYPE_Video, &MEDIASUBTYPE_NV12};

static const REGFILTERPINS AMSPinVideo = {nullptr, false, true, false, false, &CLSID_NULL, nullptr, 1, &AMSMediaTypesV};

HINSTANCE dll_inst = nullptr;
volatile long locks = 0;

/* ========================================================================= */

class VCamFactory : public IClassFactory {
	volatile long refs = 1;
	CLSID cls;

public:
	inline VCamFactory(CLSID cls_) : cls(cls_) { os_atomic_inc_long(&locks); }
	inline ~VCamFactory() { os_atomic_dec_long(&locks); }

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **p_ptr);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IClassFactory
	STDMETHODIMP CreateInstance(LPUNKNOWN parent, REFIID riid, void **p_ptr);
	STDMETHODIMP LockServer(BOOL lock);
};

STDMETHODIMP VCamFactory::QueryInterface(REFIID riid, void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	if ((riid == IID_IUnknown) || (riid == IID_IClassFactory)) {
		AddRef();
		*p_ptr = (void *)this;
		return S_OK;
	} else {
		*p_ptr = nullptr;
		return E_NOINTERFACE;
	}
}

STDMETHODIMP_(ULONG) VCamFactory::AddRef()
{
	return os_atomic_inc_long(&refs);
}

STDMETHODIMP_(ULONG) VCamFactory::Release()
{
	long new_refs = os_atomic_dec_long(&refs);
	if (new_refs == 0) {
		delete this;
		return 0;
	}

	return (ULONG)new_refs;
}

STDMETHODIMP VCamFactory::CreateInstance(LPUNKNOWN parent, REFIID riid, void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	*p_ptr = nullptr;

	/* don't bother supporting the "parent" functionality */
	if (parent) {
		return E_NOINTERFACE;
	}

	if (IsEqualCLSID(cls, CLSID_Veyra_VirtualVideo)) {
		auto* filter = new VCamFilter();
		const HRESULT hr = filter->QueryInterface(riid,p_ptr);
		filter->Release();
		return hr;
	}

	return E_NOINTERFACE;
}

STDMETHODIMP VCamFactory::LockServer(BOOL lock)
{
	if (lock) {
		os_atomic_inc_long(&locks);
	} else {
		os_atomic_dec_long(&locks);
	}

	return S_OK;
}

/* ========================================================================= */

// Registration is current-user only, and never overwrites OBS' CLSID.
STDAPI DllRegisterServer() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(dll_inst,path,32768)) return HRESULT_FROM_WIN32(GetLastError());
    return veyra::virtualcam::registerModule(path,sizeof(void*)==4);
}
STDAPI DllUnregisterServer() { return veyra::virtualcam::unregisterModule(sizeof(void*)==4); }
STDAPI DllInstall(BOOL install,LPCWSTR) { return install?DllRegisterServer():DllUnregisterServer(); }

STDAPI DllCanUnloadNow()
{
	return os_atomic_load_long(&locks) ? S_FALSE : S_OK;
}

STDAPI DllGetClassObject(REFCLSID cls, REFIID riid, void **p_ptr)
{
	if (!p_ptr) {
		return E_POINTER;
	}

	*p_ptr = nullptr;

	if (riid != IID_IClassFactory && riid != IID_IUnknown) {
		return E_NOINTERFACE;
	}
	if (!IsEqualCLSID(cls, CLSID_Veyra_VirtualVideo)) {
		return E_INVALIDARG;
	}

	*p_ptr = (void *)new VCamFactory(cls);
	return S_OK;
}

//#define ENABLE_LOGGING

#ifdef ENABLE_LOGGING
void logcallback(DShow::LogType, const wchar_t *msg, void *)
{
	OutputDebugStringW(msg);
	OutputDebugStringW(L"\n");
}
#endif

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
#ifdef ENABLE_LOGGING
		DShow::SetLogCallback(logcallback, nullptr);
#endif
		dll_inst = inst;
	}

	return true;
}
