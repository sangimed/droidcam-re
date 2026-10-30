#include <Windows.h>
#include <wrl/client.h>

#include "media_source_factory.h"
#include "media_source.h"
#include "registration.h"

namespace {

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == clone_vcam::CLSID_DroidCamCloneVirtualCameraMediaSource) {
        Microsoft::WRL::ComPtr<IClassFactory> factory;
        HRESULT hr = clone_vcam::CreateMediaSourceClassFactory(&factory);
        if (FAILED(hr)) return hr;
        hr = factory->QueryInterface(riid, ppv);
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return clone_vcam::CanUnloadMediaSourceServer() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return clone_vcam::VirtualCameraRegistration::EnsureInstalled();
}

STDAPI DllUnregisterServer() {
    return clone_vcam::VirtualCameraRegistration::Remove();
}
