#include "registration.h"

#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <shlwapi.h>

#include "media_source.h"

using Microsoft::WRL::ComPtr;

namespace clone_vcam {

namespace {

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

std::wstring FindDllPath() {
    std::wstring dir = GetModuleDir();
    std::wstring candidate = dir + L"\\droidcam_clone_virtual_camera.dll";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }
    // Check Release subdirectory if running from build
    candidate = dir + L"\\Release\\droidcam_clone_virtual_camera.dll";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }
    return dir + L"\\droidcam_clone_virtual_camera.dll";
}

HRESULT RegisterComServer(HKEY root, const std::wstring& clsid, const std::wstring& dllPath,
                          const std::wstring& installationId) {
    std::wstring subkey = L"Software\\Classes\\CLSID\\" + clsid;
    HKEY key{};
    auto status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    const wchar_t description[] = L"DroidCam RE Virtual Camera Media Source";
    RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(description),
                   static_cast<DWORD>((wcslen(description) + 1) * sizeof(wchar_t)));

    if (!installationId.empty()) {
        RegSetValueExW(key, L"InstallationId", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(installationId.c_str()),
                       static_cast<DWORD>((installationId.size() + 1) * sizeof(wchar_t)));
    }

    HKEY inprocKey{};
    status = RegCreateKeyExW(key, L"InProcServer32", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &inprocKey, nullptr);
    if (status == ERROR_SUCCESS) {
        RegSetValueExW(inprocKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(dllPath.c_str()),
                       static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
        const wchar_t threading[] = L"Both";
        RegSetValueExW(inprocKey, L"ThreadingModel", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(threading),
                       static_cast<DWORD>((wcslen(threading) + 1) * sizeof(wchar_t)));
        RegCloseKey(inprocKey);
    }
    RegCloseKey(key);

    // Also write to Software\DroidCamClone
    HKEY appKey{};
    status = RegCreateKeyExW(root, L"Software\\DroidCamClone", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &appKey, nullptr);
    if (status == ERROR_SUCCESS) {
        if (!installationId.empty()) {
            RegSetValueExW(appKey, L"InstallationId", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(installationId.c_str()),
                           static_cast<DWORD>((installationId.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(appKey);
    }

    return S_OK;
}

void UnregisterComServer(HKEY root, const std::wstring& clsid) {
    std::wstring inproc = L"Software\\Classes\\CLSID\\" + clsid + L"\\InProcServer32";
    RegDeleteKeyW(root, inproc.c_str());
    std::wstring base = L"Software\\Classes\\CLSID\\" + clsid;
    RegDeleteKeyW(root, base.c_str());
    RegDeleteKeyW(root, L"Software\\DroidCamClone");
}

}  // namespace

std::wstring VirtualCameraRegistration::GetInstallationId() {
    wchar_t id[64]{};
    DWORD size = sizeof(id);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\DroidCamClone", L"InstallationId",
                     RRF_RT_REG_SZ, nullptr, id, &size) == ERROR_SUCCESS) {
        return id;
    }
    size = sizeof(id);
    if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\DroidCamClone", L"InstallationId",
                     RRF_RT_REG_SZ, nullptr, id, &size) == ERROR_SUCCESS) {
        return id;
    }
    return {};
}

std::wstring VirtualCameraRegistration::GetSourceClsidString() {
    return kSourceClsidString;
}

HRESULT VirtualCameraRegistration::EnsureInstalled(const std::wstring& installationId) {
    std::wstring id = installationId;
    if (id.empty()) {
        id = GetInstallationId();
    }
    if (id.empty()) {
        GUID guid{};
        HRESULT hr = CoCreateGuid(&guid);
        if (FAILED(hr)) return hr;
        wchar_t buf[39]{};
        StringFromGUID2(guid, buf, 39);
        id = buf;
    }

    std::wstring dllPath = FindDllPath();
    // Register COM server in HKLM if elevated, and HKCU
    RegisterComServer(HKEY_LOCAL_MACHINE, kSourceClsidString, dllPath, id);
    RegisterComServer(HKEY_CURRENT_USER, kSourceClsidString, dllPath, id);

    // Register with Media Foundation Virtual Camera API
    ComPtr<IMFVirtualCamera> vcam;
    HRESULT hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_CurrentUser,
        kVirtualCameraFriendlyName,
        kSourceClsidString,
        nullptr,
        0,
        &vcam
    );
    if (SUCCEEDED(hr)) {
        hr = vcam->Start(nullptr);
    }
    return hr;
}

HRESULT VirtualCameraRegistration::Start() {
    ComPtr<IMFVirtualCamera> vcam;
    HRESULT hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_CurrentUser,
        kVirtualCameraFriendlyName,
        kSourceClsidString,
        nullptr,
        0,
        &vcam
    );
    if (SUCCEEDED(hr)) {
        hr = vcam->Start(nullptr);
    }
    return hr;
}

HRESULT VirtualCameraRegistration::Stop() {
    ComPtr<IMFVirtualCamera> vcam;
    HRESULT hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_CurrentUser,
        kVirtualCameraFriendlyName,
        kSourceClsidString,
        nullptr,
        0,
        &vcam
    );
    if (SUCCEEDED(hr)) {
        hr = vcam->Stop();
    }
    return hr;
}

HRESULT VirtualCameraRegistration::Remove() {
    ComPtr<IMFVirtualCamera> vcam;
    HRESULT hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_CurrentUser,
        kVirtualCameraFriendlyName,
        kSourceClsidString,
        nullptr,
        0,
        &vcam
    );
    if (SUCCEEDED(hr)) {
        hr = vcam->Remove();
    }

    UnregisterComServer(HKEY_CURRENT_USER, kSourceClsidString);
    UnregisterComServer(HKEY_LOCAL_MACHINE, kSourceClsidString);

    return hr;
}

}  // namespace clone_vcam
