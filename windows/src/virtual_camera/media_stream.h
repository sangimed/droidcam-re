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

class MediaSource;

class MediaStream final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFMediaStream2,
    IMFMediaStream,
    IMFGetService>
{
public:
    MediaStream(MediaSource* source, std::wstring installationId, Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDescriptor);
    ~MediaStream() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** ppMediaSource) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* pToken) override;
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) override;

    // Controls called by MediaSource
    HRESULT Start(const PROPVARIANT* pvarStartPosition);
    HRESULT Stop();
    HRESULT Shutdown();

private:
    HRESULT CheckShutdown() const;
    HRESULT DeliverSample(IUnknown* pToken);

    mutable std::mutex mutex_;
    MediaSource* source_{nullptr};
    std::wstring installationId_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDescriptor_;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    FrameReader frameReader_;
    SampleClock sampleClock_;
    bool isShutdown_{false};
    bool isStarted_{false};
    MF_STREAM_STATE streamState_{MF_STREAM_STATE_STOPPED};
    bool isReaderOpened_{false};
};

}  // namespace clone_vcam
