#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>
#include <string>

#include "frame_bridge.h"
#include "frame_contract.h"

namespace clone_vcam {

// Permanent source CLSID frozen for DroidCam RE Media Source:
// {C1B4DF19-9430-47FE-8356-9F7C2D323E70}
inline constexpr GUID CLSID_DroidCamCloneVirtualCameraMediaSource =
    {0xc1b4df19, 0x9430, 0x47fe, {0x83, 0x56, 0x9f, 0x7c, 0x2d, 0x32, 0x3e, 0x70}};
inline constexpr wchar_t kSourceClsidString[] = L"{C1B4DF19-9430-47FE-8356-9F7C2D323E70}";
inline constexpr wchar_t kVirtualCameraFriendlyName[] = L"DroidCam RE";

class MediaStream;

class MediaSource final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFMediaSource,
    IMFGetService>
{
public:
    MediaSource();
    explicit MediaSource(std::wstring installationId);
    ~MediaSource() override;

    static HRESULT CreateInstance(const std::wstring& installationId, IMFMediaSource** ppSource);

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) override;

    bool IsShutdown() const noexcept { return isShutdown_; }

private:
    HRESULT CheckShutdown() const;
    HRESULT EnsurePresentationDescriptor();

    mutable std::mutex mutex_;
    std::wstring installationId_;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presentationDescriptor_;
    Microsoft::WRL::ComPtr<MediaStream> stream_;
    bool isShutdown_{false};
    bool isStarted_{false};
};

}  // namespace clone_vcam
