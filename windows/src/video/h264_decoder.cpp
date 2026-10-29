#include "h264_decoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <chrono>
#include <wrl/client.h>

#include "nv12_to_bgra.h"

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
bool SetOutputType(IMFTransform* transform, const GUID& subtype, UINT32 width, UINT32 height) {
    auto mediaType = MakeType(subtype, width, height);
    return mediaType && SUCCEEDED(transform->SetOutputType(0, mediaType.Get(), 0));
}
UINT32 GetDefaultStride(IMFTransform* transform, UINT32 width) {
    ComPtr<IMFMediaType> mediaType;
    UINT32 stride = width;
    if (SUCCEEDED(transform->GetOutputCurrentType(0, &mediaType))) {
        (void)mediaType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride);
    }
    return stride;
}
bool SetAvailableOutputType(IMFTransform* transform, UINT32 width, UINT32 height,
                            bool& outputIsNv12, UINT32& outputStride, DWORD& outputFlags,
                            UINT32& frameWidth, UINT32& frameHeight) {
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> mediaType;
        const HRESULT result = transform->GetOutputAvailableType(0, index, &mediaType);
        if (FAILED(result)) return false;

        GUID subtype{};
        UINT32 typeWidth = 0;
        UINT32 typeHeight = 0;
        HRESULT candidateResult = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (FAILED(candidateResult)) continue;
        candidateResult = MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &typeWidth, &typeHeight);
        if (FAILED(candidateResult)) continue;
        const bool isNv12 = subtype == MFVideoFormat_NV12;
        if (!isNv12 && subtype != MFVideoFormat_RGB32) continue;
        const UINT32 macroblockHeight = (height + 15u) & ~15u;
        if (typeWidth != width || typeHeight < height || typeHeight > macroblockHeight) continue;
        candidateResult = transform->SetOutputType(0, mediaType.Get(), 0);
        if (FAILED(candidateResult)) continue;

        MFT_OUTPUT_STREAM_INFO streamInfo{};
        candidateResult = transform->GetOutputStreamInfo(0, &streamInfo);
        if (FAILED(candidateResult)) return false;
        outputIsNv12 = isNv12;
        outputStride = GetDefaultStride(transform, width);
        outputFlags = streamInfo.dwFlags;
        frameWidth = typeWidth;
        frameHeight = typeHeight;
        return true;
    }
}
HRESULT RequestLowLatency(IMFTransform* transform) {
    // Live camera video favors freshness; MFTs without writable attributes simply keep their default.
    ComPtr<IMFAttributes> attributes;
    const HRESULT result = transform->GetAttributes(&attributes);
    if (FAILED(result)) return result;
    return attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
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
            if (SetInputType(candidate, width, height)) {
                configured = SetOutputType(candidate, MFVideoFormat_RGB32, width, height);
                outputIsNv12_ = false;
                if (!configured) {
                    configured = SetOutputType(candidate, MFVideoFormat_NV12, width, height);
                    outputIsNv12_ = configured;
                }
            }
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
    outputStride_ = GetDefaultStride(transform_, width_);
    outputFrameWidth_ = width_;
    outputFrameHeight_ = height_;
    MFT_OUTPUT_STREAM_INFO streamInfo{};
    if (SUCCEEDED(transform_->GetOutputStreamInfo(0, &streamInfo))) outputStreamFlags_ = streamInfo.dwFlags;
    mode_ = L"hardware";
    (void)RequestLowLatency(transform_);
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
            if (SetInputType(candidate, width, height)) {
                configured = SetOutputType(candidate, MFVideoFormat_RGB32, width, height);
                outputIsNv12_ = false;
                if (!configured) {
                    configured = SetOutputType(candidate, MFVideoFormat_NV12, width, height);
                    outputIsNv12_ = configured;
                }
            }
            if (configured) transform_ = candidate;
            else candidate->Release();
        }
    }
    for (UINT32 i = 0; i < count; ++i) activations[i]->Release();
    CoTaskMemFree(activations);
    if (!configured) { Stop(); return false; }
    width_ = width;
    height_ = height;
    outputStride_ = GetDefaultStride(transform_, width_);
    outputFrameWidth_ = width_;
    outputFrameHeight_ = height_;
    MFT_OUTPUT_STREAM_INFO streamInfo{};
    if (SUCCEEDED(transform_->GetOutputStreamInfo(0, &streamInfo))) outputStreamFlags_ = streamInfo.dwFlags;
    mode_ = L"software";
    (void)RequestLowLatency(transform_);
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
    mode_ = L"unavailable";
    outputStride_ = 0;
    outputFrameWidth_ = 0;
    outputFrameHeight_ = 0;
    outputIsNv12_ = false;
    outputStreamFlags_ = 0;
}

bool H264Decoder::Decode(const std::vector<std::uint8_t>& annexB, DecodedFrame& frame,
                         std::uint32_t inputTimestamp90k) {
    lastDecodeStatus_ = S_OK;
    lastTransformTimeMs_ = 0;
    lastConversionTimeMs_ = 0;
    if (!transform_ || annexB.empty() || annexB.size() > MAXDWORD) {
        lastDecodeStatus_ = E_INVALIDARG;
        return false;
    }
    ComPtr<IMFMediaBuffer> inputBuffer;
    HRESULT result = MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), &inputBuffer);
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    BYTE* inputBytes = nullptr;
    DWORD maxLength = 0;
    result = inputBuffer->Lock(&inputBytes, &maxLength, nullptr);
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    CopyMemory(inputBytes, annexB.data(), annexB.size());
    result = inputBuffer->Unlock();
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    result = inputBuffer->SetCurrentLength(static_cast<DWORD>(annexB.size()));
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    ComPtr<IMFSample> inputSample;
    result = MFCreateSample(&inputSample);
    if (SUCCEEDED(result)) result = inputSample->AddBuffer(inputBuffer.Get());
    const LONGLONG inputSampleTime = static_cast<LONGLONG>(inputTimestamp90k) * 10'000'000 / 90'000;
    if (SUCCEEDED(result)) result = inputSample->SetSampleTime(inputSampleTime);
    const auto transformStarted = std::chrono::steady_clock::now();
    if (SUCCEEDED(result)) result = transform_->ProcessInput(0, inputSample.Get(), 0);
    if (FAILED(result)) {
        lastTransformTimeMs_ = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - transformStarted).count());
        lastDecodeStatus_ = result;
        return false;
    }

    MFT_OUTPUT_STREAM_INFO streamInfo{};
    result = transform_->GetOutputStreamInfo(0, &streamInfo);
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    ComPtr<IMFMediaBuffer> outputBuffer;
    ComPtr<IMFSample> outputSample;
    DWORD outputFlags = streamInfo.dwFlags;
    auto createOutputSample = [&]() -> HRESULT {
        outputBuffer.Reset();
        outputSample.Reset();
        if ((outputFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0) return S_OK;
        const DWORD outputSize = streamInfo.cbSize ? streamInfo.cbSize : outputFrameWidth_ * outputFrameHeight_ * 4;
        HRESULT createResult = MFCreateMemoryBuffer(outputSize, &outputBuffer);
        if (FAILED(createResult)) return createResult;
        createResult = MFCreateSample(&outputSample);
        if (SUCCEEDED(createResult)) createResult = outputSample->AddBuffer(outputBuffer.Get());
        return createResult;
    };
    result = createOutputSample();
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    MFT_OUTPUT_DATA_BUFFER output{0, outputSample.Get(), 0, nullptr};
    DWORD status = 0;
    result = transform_->ProcessOutput(0, 1, &output, &status);
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (output.pEvents) output.pEvents->Release();
        if (!SetAvailableOutputType(transform_, width_, height_, outputIsNv12_, outputStride_, outputStreamFlags_,
                                    outputFrameWidth_, outputFrameHeight_)) {
            lastDecodeStatus_ = MF_E_INVALIDMEDIATYPE;
            return false;
        }
        outputFlags = outputStreamFlags_;
        result = transform_->GetOutputStreamInfo(0, &streamInfo);
        if (SUCCEEDED(result)) result = createOutputSample();
        if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
        output = MFT_OUTPUT_DATA_BUFFER{0, outputSample.Get(), 0, nullptr};
        status = 0;
        result = transform_->ProcessOutput(0, 1, &output, &status);
    }
    lastTransformTimeMs_ = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - transformStarted).count());
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(result)) { lastDecodeStatus_ = result; return false; }
    if (!outputSample && output.pSample) outputSample.Attach(output.pSample);
    ComPtr<IMFMediaBuffer> contiguous;
    result = outputSample->ConvertToContiguousBuffer(&contiguous);
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    BYTE* pixels = nullptr;
    DWORD currentLength = 0;
    result = contiguous->Lock(&pixels, nullptr, &currentLength);
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    frame.width = width_;
    frame.height = height_;
    const auto conversionStarted = std::chrono::steady_clock::now();
    if (outputIsNv12_) {
        const auto signedStride = static_cast<std::int32_t>(outputStride_);
        std::vector<std::uint8_t> decodedBgra;
        if (signedStride <= 0 || !Nv12ToBgra(pixels, currentLength, outputFrameWidth_, outputFrameHeight_,
                                             static_cast<std::size_t>(signedStride), decodedBgra)) {
            contiguous->Unlock();
            lastDecodeStatus_ = MF_E_INVALIDMEDIATYPE;
            return false;
        }
        const std::size_t visibleRowBytes = static_cast<std::size_t>(width_) * 4;
        frame.bgra.resize(visibleRowBytes * height_);
        for (UINT32 row = 0; row < height_; ++row) {
            CopyMemory(frame.bgra.data() + static_cast<std::size_t>(row) * visibleRowBytes,
                       decodedBgra.data() + static_cast<std::size_t>(row) * outputFrameWidth_ * 4,
                       visibleRowBytes);
        }
    } else {
        const std::size_t needed = static_cast<std::size_t>(width_) * height_ * 4;
        const std::size_t outputRowBytes = static_cast<std::size_t>(outputFrameWidth_) * 4;
        if (currentLength < outputRowBytes * outputFrameHeight_) {
            contiguous->Unlock();
            lastDecodeStatus_ = MF_E_BUFFERTOOSMALL;
            return false;
        }
        frame.bgra.resize(needed);
        for (UINT32 row = 0; row < height_; ++row) {
            CopyMemory(frame.bgra.data() + static_cast<std::size_t>(row) * needed / height_,
                       pixels + static_cast<std::size_t>(row) * outputRowBytes,
                       needed / height_);
        }
    }
    lastConversionTimeMs_ = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - conversionStarted).count());
    result = contiguous->Unlock();
    if (FAILED(result)) { lastDecodeStatus_ = result; return false; }
    return true;
}
}
