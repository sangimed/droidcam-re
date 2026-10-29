#pragma once

#include <Windows.h>
#include <string>

namespace clone_vcam {

class VirtualCameraRegistration {
public:
    static HRESULT EnsureInstalled(const std::wstring& installationId = L"");
    static HRESULT Start();
    static HRESULT Stop();
    static HRESULT Remove();
    static std::wstring GetInstallationId();
    static std::wstring GetSourceClsidString();
};

}  // namespace clone_vcam
