#include "h264_decoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace clone_video {
namespace {
ComPtr<IMFMediaType> MakeType(const GUID& subtype, UINT32 width, UINT32 height) {
    ComPtr<IMFMediaType> mediaType;
    if (FAILED(MFCreateMediaType(&mediaType))) return {};
    if (FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, subtype)) ||
        FAILED(MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, width, height)) ||
        FAILED(mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))) return {};
    return mediaType;
}
bool SetInputType(IMFTransform* transform, UINT32 width, UINT32 height) {
    auto mediaType = MakeType(MFVideoFormat_H264, width, height);
    return mediaType && SUCCEEDED(transform->SetInputType(0, mediaType.Get(), 0));
}
bool SetOutputType(IMFTransform* transform, UINT32 width, UINT32 height) {
    auto mediaType = MakeType(MFVideoFormat_RGB32, width, height);
    return mediaType && SUCCEEDED(transform->SetOutputType(0, mediaType.Get(), 0));
}
}

H264Decoder::~H264Decoder() { Stop(); }

bool H264Decoder::Start(UINT32 width, UINT32 height) {
    Stop();
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    started_ = true;
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const HRESULT found = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_LOCALMFT,
        &input, nullptr, &activations, &count);
    if (FAILED(found) || count == 0) {
        if (activations) CoTaskMemFree(activations);
        Stop();
        return StartSoftware(width, height);
    }
    bool configured = false;
    for (UINT32 i = 0; i < count && !configured; ++i) {
        IMFTransform* candidate = nullptr;
        if (SUCCEEDED(activations[i]->ActivateObject(IID_PPV_ARGS(&candidate)))) {
            configured = SetInputType(candidate, width, height) && SetOutputType(candidate, width, height);
            if (configured) transform_ = candidate;
            else candidate->Release();
        }
    }
    for (UINT32 i = 0; i < count; ++i) activations[i]->Release();
    CoTaskMemFree(activations);
    if (!configured) {
        Stop();
        return StartSoftware(width, height);
    }
    width_ = width;
    height_ = height;
    return true;
}

bool H264Decoder::StartSoftware(UINT32 width, UINT32 height) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    started_ = true;
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const HRESULT found = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT,
        &input, nullptr, &activations, &count);
    bool configured = false;
    for (UINT32 i = 0; SUCCEEDED(found) && i < count && !configured; ++i) {
        IMFTransform* candidate = nullptr;
        if (SUCCEEDED(activations[i]->ActivateObject(IID_PPV_ARGS(&candidate)))) {
            configured = SetInputType(candidate, width, height) && SetOutputType(candidate, width, height);
            if (configured) transform_ = candidate;
            else candidate->Release();
        }
    }
    for (UINT32 i = 0; i < count; ++i) activations[i]->Release();
    CoTaskMemFree(activations);
    if (!configured) { Stop(); return false; }
    width_ = width;
    height_ = height;
    return true;
}

void H264Decoder::Stop() {
    if (transform_) {
        transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        transform_->Release();
        transform_ = nullptr;
    }
    if (started_) {
        MFShutdown();
        started_ = false;
    }
}

bool H264Decoder::Decode(const std::vector<std::uint8_t>& annexB, DecodedFrame& frame) {
    if (!transform_ || annexB.empty() || annexB.size() > MAXDWORD) return false;
    ComPtr<IMFMediaBuffer> inputBuffer;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), &inputBuffer))) return false;
    BYTE* inputBytes = nullptr;
    DWORD maxLength = 0;
    if (FAILED(inputBuffer->Lock(&inputBytes, &maxLength, nullptr))) return false;
    CopyMemory(inputBytes, annexB.data(), annexB.size());
    inputBuffer->Unlock();
    inputBuffer->SetCurrentLength(static_cast<DWORD>(annexB.size()));
    ComPtr<IMFSample> inputSample;
    if (FAILED(MFCreateSample(&inputSample)) || FAILED(inputSample->AddBuffer(inputBuffer.Get())) ||
        FAILED(transform_->ProcessInput(0, inputSample.Get(), 0))) return false;

    MFT_OUTPUT_STREAM_INFO streamInfo{};
    if (FAILED(transform_->GetOutputStreamInfo(0, &streamInfo))) return false;
    const DWORD outputSize = streamInfo.cbSize ? streamInfo.cbSize : width_ * height_ * 4;
    ComPtr<IMFMediaBuffer> outputBuffer;
    if (FAILED(MFCreateMemoryBuffer(outputSize, &outputBuffer))) return false;
    ComPtr<IMFSample> outputSample;
    if (FAILED(MFCreateSample(&outputSample)) || FAILED(outputSample->AddBuffer(outputBuffer.Get()))) return false;
    MFT_OUTPUT_DATA_BUFFER output{0, outputSample.Get(), 0, nullptr};
    DWORD status = 0;
    const HRESULT result = transform_->ProcessOutput(0, 1, &output, &status);
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(result)) return false;
    ComPtr<IMFMediaBuffer> contiguous;
    if (FAILED(outputSample->ConvertToContiguousBuffer(&contiguous))) return false;
    BYTE* pixels = nullptr;
    DWORD currentLength = 0;
    if (FAILED(contiguous->Lock(&pixels, nullptr, &currentLength))) return false;
    const std::size_t needed = static_cast<std::size_t>(width_) * height_ * 4;
    if (currentLength < needed) { contiguous->Unlock(); return false; }
    frame.width = width_;
    frame.height = height_;
    frame.bgra.assign(pixels, pixels + needed);
    contiguous->Unlock();
    return true;
}
}
