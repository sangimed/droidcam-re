#include "media_stream.h"

#include <mferror.h>
#include <cstring>

#include "media_source.h"
#include "frame_conversion.h"

namespace clone_vcam {

MediaStream::MediaStream(MediaSource* source, std::wstring installationId,
                         Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDescriptor)
    : source_(source),
      installationId_(std::move(installationId)),
      streamDescriptor_(std::move(streamDescriptor)) {
    MFCreateEventQueue(&eventQueue_);
}

MediaStream::~MediaStream() {
    Shutdown();
}

HRESULT MediaStream::CheckShutdown() const {
    return isShutdown_ ? MF_E_SHUTDOWN : S_OK;
}

IFACEMETHODIMP MediaStream::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->BeginGetEvent(pCallback, punkState) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->EndGetEvent(pResult, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaStream::QueueEvent(MediaEventType met, REFGUID guidExtendedType,
                                      HRESULT hrStatus, const PROPVARIANT* pvValue) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** ppMediaSource) {
    if (!ppMediaSource) return E_POINTER;
    *ppMediaSource = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    if (!source_) return E_UNEXPECTED;
    return source_->QueryInterface(IID_PPV_ARGS(ppMediaSource));
}

IFACEMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) {
    if (!ppStreamDescriptor) return E_POINTER;
    *ppStreamDescriptor = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    if (!streamDescriptor_) return E_UNEXPECTED;
    *ppStreamDescriptor = streamDescriptor_.Get();
    (*ppStreamDescriptor)->AddRef();
    return S_OK;
}

IFACEMETHODIMP MediaStream::RequestSample(IUnknown* pToken) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    if (!isStarted_) return MF_E_MEDIA_SOURCE_WRONGSTATE;
    return DeliverSample(pToken);
}

IFACEMETHODIMP MediaStream::GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

HRESULT MediaStream::Start(const PROPVARIANT* pvarStartPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    isStarted_ = true;
    streamState_ = MF_STREAM_STATE_RUNNING;
    sampleClock_.Reset();
    return eventQueue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, pvarStartPosition);
}

HRESULT MediaStream::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    if (isStarted_) {
        isStarted_ = false;
        streamState_ = MF_STREAM_STATE_STOPPED;
        PROPVARIANT emptyVar;
        PropVariantInit(&emptyVar);
        return eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, &emptyVar);
    }
    return S_OK;
}

HRESULT MediaStream::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    isShutdown_ = true;
    isStarted_ = false;
    streamState_ = MF_STREAM_STATE_STOPPED;

    frameReader_.Close();
    isReaderOpened_ = false;

    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    streamDescriptor_.Reset();
    source_ = nullptr;
    return S_OK;
}

IFACEMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state) {
    if (state != MF_STREAM_STATE_STOPPED && state != MF_STREAM_STATE_PAUSED &&
        state != MF_STREAM_STATE_RUNNING) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    streamState_ = state;
    return S_OK;
}

IFACEMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state) {
    if (!state) return E_POINTER;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    *state = streamState_;
    return S_OK;
}

HRESULT MediaStream::DeliverSample(IUnknown* pToken) {
    if (!isReaderOpened_ && !installationId_.empty()) {
        if (SUCCEEDED(frameReader_.Open(installationId_))) {
            isReaderOpened_ = true;
        }
    }

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    OutputFrame frame;
    bool gotLive = false;
    if (isReaderOpened_) {
        gotLive = frameReader_.ReadLatest(static_cast<std::uint64_t>(qpc.QuadPart), frame);
    }
    if (!gotLive) {
        GenerateNeutralFrame(FrameState::Disconnected, frame);
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    auto hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(static_cast<DWORD>(kOutputFrameBytes), &buffer);
    if (FAILED(hr)) return hr;

    BYTE* data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    hr = buffer->Lock(&data, &maxLen, &curLen);
    if (FAILED(hr)) return hr;

    std::memcpy(data, frame.nv12.data(), kOutputFrameBytes);
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(kOutputFrameBytes));

    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) return hr;

    const auto timing = sampleClock_.Next();
    sample->SetSampleTime(timing.timestamp);
    sample->SetSampleDuration(timing.duration);

    if (pToken) {
        sample->SetUnknown(MFSampleExtension_Token, pToken);
    }

    return eventQueue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

}  // namespace clone_vcam
