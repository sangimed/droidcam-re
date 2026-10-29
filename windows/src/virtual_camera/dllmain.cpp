#include <Windows.h>
#include <atomic>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "media_source.h"
#include "registration.h"

namespace {

std::atomic<ULONG> g_lockCount{0};

class MediaSourceClassFactory : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = --refCount_;
        if (count == 0) delete this;
        return count;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto source = Microsoft::WRL::Make<clone_vcam::MediaSource>();
        if (!source) return E_OUTOFMEMORY;
        return source->QueryInterface(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) ++g_lockCount; else --g_lockCount;
        return S_OK;
    }
private:
    std::atomic<ULONG> refCount_{1};
};

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == clone_vcam::CLSID_DroidCamCloneVirtualCameraMediaSource) {
        auto factory = new (std::nothrow) MediaSourceClassFactory();
        if (!factory) return E_OUTOFMEMORY;
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return g_lockCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return clone_vcam::VirtualCameraRegistration::EnsureInstalled();
}

STDAPI DllUnregisterServer() {
    return clone_vcam::VirtualCameraRegistration::Remove();
}
