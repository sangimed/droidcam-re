#include "media_source.h"

#include <mferror.h>
#include <shlwapi.h>

#include "media_stream.h"

namespace clone_vcam {

namespace {

class ValidatingMediaTypeHandler : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFMediaTypeHandler>
{
public:
    explicit ValidatingMediaTypeHandler(Microsoft::WRL::ComPtr<IMFMediaTypeHandler> inner)
        : inner_(std::move(inner)) {}

    IFACEMETHODIMP IsMediaTypeSupported(IMFMediaType* pMediaType, IMFMediaType** ppMediaType) override {
        return inner_->IsMediaTypeSupported(pMediaType, ppMediaType);
    }
    IFACEMETHODIMP GetMediaTypeCount(DWORD* pdwTypeCount) override {
        return inner_->GetMediaTypeCount(pdwTypeCount);
    }
    IFACEMETHODIMP GetMediaTypeByIndex(DWORD dwIndex, IMFMediaType** ppMediaType) override {
        return inner_->GetMediaTypeByIndex(dwIndex, ppMediaType);
    }
    IFACEMETHODIMP SetCurrentMediaType(IMFMediaType* pMediaType) override {
        if (!pMediaType) return E_POINTER;
        HRESULT hr = inner_->IsMediaTypeSupported(pMediaType, nullptr);
        if (FAILED(hr)) return MF_E_INVALIDMEDIATYPE;
        return inner_->SetCurrentMediaType(pMediaType);
    }
    IFACEMETHODIMP GetCurrentMediaType(IMFMediaType** ppMediaType) override {
        return inner_->GetCurrentMediaType(ppMediaType);
    }
    IFACEMETHODIMP GetMajorType(GUID* pguidMajorType) override {
        return inner_->GetMajorType(pguidMajorType);
    }

private:
    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> inner_;
};

class ValidatingStreamDescriptor : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFStreamDescriptor>
{
public:
    explicit ValidatingStreamDescriptor(Microsoft::WRL::ComPtr<IMFStreamDescriptor> inner)
        : inner_(std::move(inner)) {}

    // IMFStreamDescriptor
    IFACEMETHODIMP GetStreamIdentifier(DWORD* pdwStreamIdentifier) override {
        return inner_->GetStreamIdentifier(pdwStreamIdentifier);
    }
    IFACEMETHODIMP GetMediaTypeHandler(IMFMediaTypeHandler** ppHandler) override {
        if (!ppHandler) return E_POINTER;
        *ppHandler = nullptr;
        Microsoft::WRL::ComPtr<IMFMediaTypeHandler> innerHandler;
        HRESULT hr = inner_->GetMediaTypeHandler(&innerHandler);
        if (FAILED(hr)) return hr;
        auto validating = Microsoft::WRL::Make<ValidatingMediaTypeHandler>(innerHandler);
        if (!validating) return E_OUTOFMEMORY;
        return validating->QueryInterface(IID_PPV_ARGS(ppHandler));
    }

    // IMFAttributes forwarding
    IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override { return inner_->GetItem(guidKey, pValue); }
    IFACEMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override { return inner_->GetItemType(guidKey, pType); }
    IFACEMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override { return inner_->CompareItem(guidKey, Value, pbResult); }
    IFACEMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override { return inner_->Compare(pTheirs, MatchType, pbResult); }
    IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override { return inner_->GetUINT32(guidKey, punValue); }
    IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override { return inner_->GetUINT64(guidKey, punValue); }
    IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override { return inner_->GetDouble(guidKey, pfValue); }
    IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override { return inner_->GetGUID(guidKey, pguidValue); }
    IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override { return inner_->GetStringLength(guidKey, pcchLength); }
    IFACEMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override { return inner_->GetString(guidKey, pwszValue, cchBufSize, pcchLength); }
    IFACEMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override { return inner_->GetAllocatedString(guidKey, ppwszValue, pcchLength); }
    IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override { return inner_->GetBlobSize(guidKey, pcbBlobSize); }
    IFACEMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override { return inner_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize); }
    IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override { return inner_->GetAllocatedBlob(guidKey, ppBuf, pcbSize); }
    IFACEMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override { return inner_->GetUnknown(guidKey, riid, ppv); }
    IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override { return inner_->SetItem(guidKey, Value); }
    IFACEMETHODIMP DeleteItem(REFGUID guidKey) override { return inner_->DeleteItem(guidKey); }
    IFACEMETHODIMP DeleteAllItems() override { return inner_->DeleteAllItems(); }
    IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override { return inner_->SetUINT32(guidKey, unValue); }
    IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override { return inner_->SetUINT64(guidKey, unValue); }
    IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override { return inner_->SetDouble(guidKey, fValue); }
    IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override { return inner_->SetGUID(guidKey, guidValue); }
    IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override { return inner_->SetString(guidKey, wszValue); }
    IFACEMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override { return inner_->SetBlob(guidKey, pBuf, cbBufSize); }
    IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override { return inner_->SetUnknown(guidKey, pUnknown); }
    IFACEMETHODIMP LockStore() override { return inner_->LockStore(); }
    IFACEMETHODIMP UnlockStore() override { return inner_->UnlockStore(); }
    IFACEMETHODIMP GetCount(UINT32* pcItems) override { return inner_->GetCount(pcItems); }
    IFACEMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override { return inner_->GetItemByIndex(unIndex, pguidKey, pValue); }
    IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override { return inner_->CopyAllItems(pDest); }

private:
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> inner_;
};

std::wstring ReadInstallationIdFromRegistry() {

    wchar_t id[64]{};
    DWORD size = sizeof(id);
    // 1. Try HKLM
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\DroidCamClone", L"InstallationId",
                     RRF_RT_REG_SZ, nullptr, id, &size) == ERROR_SUCCESS) {
        return id;
    }
    // 2. Try HKCR CLSID
    const std::wstring subkey = std::wstring(L"CLSID\\") + kSourceClsidString;
    size = sizeof(id);
    if (RegGetValueW(HKEY_CLASSES_ROOT, subkey.c_str(), L"InstallationId",
                     RRF_RT_REG_SZ, nullptr, id, &size) == ERROR_SUCCESS) {
        return id;
    }
    // 3. Try HKCU
    size = sizeof(id);
    if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\DroidCamClone", L"InstallationId",
                     RRF_RT_REG_SZ, nullptr, id, &size) == ERROR_SUCCESS) {
        return id;
    }
    return {};
}

}  // namespace

MediaSource::MediaSource() : MediaSource(ReadInstallationIdFromRegistry()) {}

MediaSource::MediaSource(std::wstring installationId)
    : installationId_(std::move(installationId)) {
    MFCreateEventQueue(&eventQueue_);
}

MediaSource::~MediaSource() {
    Shutdown();
}

HRESULT MediaSource::CreateInstance(const std::wstring& installationId, IMFMediaSource** ppSource) {
    if (!ppSource) return E_POINTER;
    *ppSource = nullptr;
    auto source = Microsoft::WRL::Make<MediaSource>(installationId);
    if (!source) return E_OUTOFMEMORY;
    return source->QueryInterface(IID_PPV_ARGS(ppSource));
}

HRESULT MediaSource::CheckShutdown() const {
    return isShutdown_ ? MF_E_SHUTDOWN : S_OK;
}

IFACEMETHODIMP MediaSource::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->BeginGetEvent(pCallback, punkState) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->EndGetEvent(pResult, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaSource::QueueEvent(MediaEventType met, REFGUID guidExtendedType,
                                      HRESULT hrStatus, const PROPVARIANT* pvValue) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP MediaSource::GetCharacteristics(DWORD* pdwCharacteristics) {
    if (!pdwCharacteristics) return E_POINTER;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    *pdwCharacteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) {
    if (!ppPresentationDescriptor) return E_POINTER;
    *ppPresentationDescriptor = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    const auto hr = EnsurePresentationDescriptor();
    if (FAILED(hr)) return hr;
    return presentationDescriptor_->Clone(ppPresentationDescriptor);
}

IFACEMETHODIMP MediaSource::Start(IMFPresentationDescriptor* pPresentationDescriptor,
                                  const GUID* pguidTimeFormat,
                                  const PROPVARIANT* pvarStartPosition) {
    if (!pPresentationDescriptor) return E_POINTER;
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;

    auto hr = EnsurePresentationDescriptor();
    if (FAILED(hr)) return hr;

    BOOL selected = FALSE;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> sd;
    hr = pPresentationDescriptor->GetStreamDescriptorByIndex(0, &selected, &sd);
    if (FAILED(hr)) return hr;

    if (selected) {
        if (!isStarted_) {
            hr = eventQueue_->QueueEventParamUnk(MENewStream, GUID_NULL, S_OK,
                                                 static_cast<IMFMediaStream*>(stream_.Get()));
            if (FAILED(hr)) return hr;
        }
        hr = stream_->Start(pvarStartPosition);
        if (FAILED(hr)) return hr;

        hr = eventQueue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, pvarStartPosition);
        if (FAILED(hr)) return hr;

        isStarted_ = true;
    } else {
        if (isStarted_) {
            Stop();
        }
    }
    return S_OK;
}

IFACEMETHODIMP MediaSource::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;

    if (isStarted_) {
        isStarted_ = false;
        if (stream_) {
            stream_->Stop();
        }
        PROPVARIANT emptyVar;
        PropVariantInit(&emptyVar);
        eventQueue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &emptyVar);
    }
    return S_OK;
}

IFACEMETHODIMP MediaSource::Pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FAILED(CheckShutdown())) return MF_E_SHUTDOWN;
    return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP MediaSource::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    isShutdown_ = true;
    isStarted_ = false;

    if (stream_) {
        stream_->Shutdown();
        stream_.Reset();
    }
    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    presentationDescriptor_.Reset();
    return S_OK;
}

IFACEMETHODIMP MediaSource::GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

HRESULT MediaSource::EnsurePresentationDescriptor() {
    if (presentationDescriptor_) return S_OK;

    Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
    auto hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) return hr;

    hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) return hr;
    hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (FAILED(hr)) return hr;
    hr = MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, kOutputWidth, kOutputHeight);
    if (FAILED(hr)) return hr;
    hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, kOutputFps, 1);
    if (FAILED(hr)) return hr;
    hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) return hr;
    hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) return hr;
    hr = mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) return hr;
    hr = mediaType->SetUINT32(MF_MT_DEFAULT_STRIDE, kOutputWidth);
    if (FAILED(hr)) return hr;
    hr = mediaType->SetUINT32(MF_MT_SAMPLE_SIZE, static_cast<UINT32>(kOutputFrameBytes));
    if (FAILED(hr)) return hr;
    hr = mediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    if (FAILED(hr)) return hr;

    IMFMediaType* types[] = {mediaType.Get()};
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc;
    hr = MFCreateStreamDescriptor(1, 1, types, &streamDesc);
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
    hr = streamDesc->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return hr;
    hr = handler->SetCurrentMediaType(mediaType.Get());
    if (FAILED(hr)) return hr;

    auto validatingDesc = Microsoft::WRL::Make<ValidatingStreamDescriptor>(streamDesc);
    if (!validatingDesc) return E_OUTOFMEMORY;

    IMFStreamDescriptor* streams[] = {validatingDesc.Get()};
    hr = MFCreatePresentationDescriptor(1, streams, &presentationDescriptor_);
    if (FAILED(hr)) return hr;

    hr = presentationDescriptor_->SelectStream(0);
    if (FAILED(hr)) return hr;

    stream_ = Microsoft::WRL::Make<MediaStream>(this, installationId_, validatingDesc);

    if (!stream_) return E_OUTOFMEMORY;

    return S_OK;
}

}  // namespace clone_vcam
