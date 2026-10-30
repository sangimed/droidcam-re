#include "virtual_camera/media_source.h"
#include "virtual_camera/media_source_factory.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>

#include "virtual_camera/frame_bridge.h"
#include "virtual_camera/frame_conversion.h"

#define CHECK_ASSERT(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::abort(); } } while(0)
#define Check CHECK_ASSERT


using namespace clone_vcam;
using Microsoft::WRL::ComPtr;

namespace {

class DummyToken : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IUnknown> {
};

std::wstring NewId() {
    GUID guid;
    Check(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39];
    Check(StringFromGUID2(guid, id, 39));
    return id;
}

void sourceClassFactoryProvidesActivate() {
    ComPtr<IClassFactory> factory;
    Check(SUCCEEDED(CreateMediaSourceClassFactory(&factory)));

    ComPtr<IMFActivate> activation;
    Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&activation))));

    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(activation->ActivateObject(IID_PPV_ARGS(&source))));
    Check(activation->ShutdownObject() == E_NOTIMPL);
}

void frameServerSourceInterfacesAreAvailable() {
    ComPtr<IClassFactory> factory;
    Check(SUCCEEDED(CreateMediaSourceClassFactory(&factory)));
    ComPtr<IMFActivate> activation;
    Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&activation))));

    ComPtr<IMFMediaSourceEx> source;
    Check(SUCCEEDED(activation->ActivateObject(IID_PPV_ARGS(&source))));
    ComPtr<IMFAttributes> attributes;
    Check(SUCCEEDED(source->GetSourceAttributes(&attributes)));
    ComPtr<IMFAttributes> streamAttributes;
    Check(SUCCEEDED(source->GetStreamAttributes(0, &streamAttributes)));
    UINT32 streamId = UINT32_MAX;
    Check(SUCCEEDED(streamAttributes->GetUINT32(MF_DEVICESTREAM_STREAM_ID, &streamId)) && streamId == 0);
    GUID streamCategory{};
    Check(SUCCEEDED(streamAttributes->GetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, &streamCategory)));
    Check(streamCategory == PINNAME_VIDEO_CAPTURE);
    UINT32 shared = 0;
    Check(SUCCEEDED(streamAttributes->GetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, &shared)) && shared == TRUE);

    const GUID iidKsControl = {0x28f54685, 0x06fd, 0x11d2, {0xb2, 0x7a, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96}};
    void* controls = nullptr;
    Check(SUCCEEDED(activation->ActivateObject(iidKsControl, &controls)));
    static_cast<IUnknown*>(controls)->Release();
    Check(activation->ShutdownObject() == E_NOTIMPL);
}

void oneNv12Stream1080p30() {
    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(MediaSource::CreateInstance(NewId(), &source)));

    ComPtr<IMFPresentationDescriptor> pd;
    Check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));

    DWORD streamCount = 0;
    Check(SUCCEEDED(pd->GetStreamDescriptorCount(&streamCount)));
    Check(streamCount == 1);

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    Check(SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)));
    Check(selected == TRUE);
    DWORD streamIdentifier = UINT32_MAX;
    Check(SUCCEEDED(sd->GetStreamIdentifier(&streamIdentifier)) && streamIdentifier == 0);

    ComPtr<IMFMediaTypeHandler> handler;
    Check(SUCCEEDED(sd->GetMediaTypeHandler(&handler)));

    DWORD typeCount = 0;
    Check(SUCCEEDED(handler->GetMediaTypeCount(&typeCount)));
    Check(typeCount == 1);

    ComPtr<IMFMediaType> mt;
    Check(SUCCEEDED(handler->GetMediaTypeByIndex(0, &mt)));

    GUID majorType{}, subtype{};
    Check(SUCCEEDED(mt->GetGUID(MF_MT_MAJOR_TYPE, &majorType)));
    Check(majorType == MFMediaType_Video);

    Check(SUCCEEDED(mt->GetGUID(MF_MT_SUBTYPE, &subtype)));
    Check(subtype == MFVideoFormat_NV12);

    UINT32 width = 0, height = 0;
    Check(SUCCEEDED(MFGetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, &width, &height)));
    Check(width == 1920 && height == 1080);

    UINT32 fpsNum = 0, fpsDen = 0;
    Check(SUCCEEDED(MFGetAttributeRatio(mt.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen)));
    Check(fpsNum == 30 && fpsDen == 1);

    UINT32 parNum = 0, parDen = 0;
    Check(SUCCEEDED(MFGetAttributeRatio(mt.Get(), MF_MT_PIXEL_ASPECT_RATIO, &parNum, &parDen)));
    Check(parNum == 1 && parDen == 1);

    UINT32 interlace = 0;
    Check(SUCCEEDED(mt->GetUINT32(MF_MT_INTERLACE_MODE, &interlace)));
    Check(interlace == MFVideoInterlace_Progressive);

    UINT32 sampleSize = 0;
    Check(SUCCEEDED(mt->GetUINT32(MF_MT_SAMPLE_SIZE, &sampleSize)));
    Check(sampleSize == 3'110'400);

    Check(SUCCEEDED(source->Shutdown()));
}

void invalidMediaTypeRejected() {
    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(MediaSource::CreateInstance(NewId(), &source)));

    ComPtr<IMFPresentationDescriptor> pd;
    Check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    Check(SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)));

    ComPtr<IMFMediaTypeHandler> handler;
    Check(SUCCEEDED(sd->GetMediaTypeHandler(&handler)));

    ComPtr<IMFMediaType> invalidMt;
    Check(SUCCEEDED(MFCreateMediaType(&invalidMt)));
    Check(SUCCEEDED(invalidMt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)));
    Check(SUCCEEDED(invalidMt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)));
    Check(SUCCEEDED(MFSetAttributeSize(invalidMt.Get(), MF_MT_FRAME_SIZE, 640, 480)));

    Check(handler->IsMediaTypeSupported(invalidMt.Get(), nullptr) == MF_E_INVALIDMEDIATYPE);
    Check(handler->SetCurrentMediaType(invalidMt.Get()) == MF_E_INVALIDMEDIATYPE);



    Check(SUCCEEDED(source->Shutdown()));
}

void requestSamplePreservesToken() {
    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(MediaSource::CreateInstance(NewId(), &source)));

    ComPtr<IMFPresentationDescriptor> pd;
    Check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));

    PROPVARIANT startPos;
    PropVariantInit(&startPos);
    startPos.vt = VT_I8;
    startPos.hVal.QuadPart = 0;
    Check(SUCCEEDED(source->Start(pd.Get(), nullptr, &startPos)));

    ComPtr<IMFMediaEvent> event;
    Check(SUCCEEDED(source->GetEvent(0, &event)));
    MediaEventType met;
    Check(SUCCEEDED(event->GetType(&met)) && met == MENewStream);

    PROPVARIANT streamVar;
    PropVariantInit(&streamVar);
    Check(SUCCEEDED(event->GetValue(&streamVar)));
    ComPtr<IMFMediaStream> stream;
    Check(SUCCEEDED(streamVar.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));
    PropVariantClear(&streamVar);

    ComPtr<IMFMediaStream2> stream2;
    Check(SUCCEEDED(stream.As(&stream2)));
    MF_STREAM_STATE streamState{};
    Check(SUCCEEDED(stream2->GetStreamState(&streamState)) && streamState == MF_STREAM_STATE_RUNNING);

    Check(SUCCEEDED(source->GetEvent(0, &event)));
    Check(SUCCEEDED(event->GetType(&met)) && met == MESourceStarted);

    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    Check(SUCCEEDED(event->GetType(&met)) && met == MEStreamStarted);

    auto token = Microsoft::WRL::Make<DummyToken>();
    Check(SUCCEEDED(stream->RequestSample(token.Get())));

    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    Check(SUCCEEDED(event->GetType(&met)) && met == MEMediaSample);

    PROPVARIANT sampleVar;
    PropVariantInit(&sampleVar);
    Check(SUCCEEDED(event->GetValue(&sampleVar)));
    ComPtr<IMFSample> sample;
    Check(SUCCEEDED(sampleVar.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
    PropVariantClear(&sampleVar);

    ComPtr<IUnknown> receivedToken;
    Check(SUCCEEDED(sample->GetUnknown(MFSampleExtension_Token, IID_PPV_ARGS(&receivedToken))));
    Check(receivedToken.Get() == token.Get());

    LONGLONG sampleTime = 0, sampleDuration = 0;
    Check(SUCCEEDED(sample->GetSampleTime(&sampleTime)) && sampleTime == 0);
    Check(SUCCEEDED(sample->GetSampleDuration(&sampleDuration)));
    Check(sampleDuration == 333'333 || sampleDuration == 333'334);

    DWORD totalLen = 0;
    Check(SUCCEEDED(sample->GetTotalLength(&totalLen)) && totalLen == 3'110'400);

    Check(SUCCEEDED(source->Shutdown()));
}

void stopShutdownUnblocksRequests() {
    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(MediaSource::CreateInstance(NewId(), &source)));

    ComPtr<IMFPresentationDescriptor> pd;
    Check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));

    PROPVARIANT startPos;
    PropVariantInit(&startPos);
    startPos.vt = VT_I8;
    startPos.hVal.QuadPart = 0;
    Check(SUCCEEDED(source->Start(pd.Get(), nullptr, &startPos)));

    ComPtr<IMFMediaEvent> event;
    Check(SUCCEEDED(source->GetEvent(0, &event)));
    PROPVARIANT streamVar;
    PropVariantInit(&streamVar);
    Check(SUCCEEDED(event->GetValue(&streamVar)));
    ComPtr<IMFMediaStream> stream;
    Check(SUCCEEDED(streamVar.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));
    PropVariantClear(&streamVar);

    MediaEventType met;
    Check(SUCCEEDED(source->GetEvent(0, &event)));
    Check(SUCCEEDED(stream->GetEvent(0, &event)));

    Check(SUCCEEDED(source->Stop()));

    Check(SUCCEEDED(source->GetEvent(0, &event)));
    Check(SUCCEEDED(event->GetType(&met)) && met == MESourceStopped);

    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    Check(SUCCEEDED(event->GetType(&met)) && met == MEStreamStopped);

    Check(stream->RequestSample(nullptr) == MF_E_MEDIA_SOURCE_WRONGSTATE);

    Check(SUCCEEDED(source->Shutdown()));
    Check(source->Start(pd.Get(), nullptr, &startPos) == MF_E_SHUTDOWN);
    Check(source->Stop() == MF_E_SHUTDOWN);
    Check(source->CreatePresentationDescriptor(&pd) == MF_E_SHUTDOWN);
    Check(stream->RequestSample(nullptr) == MF_E_SHUTDOWN);
}

void independentConsumerClocks() {
    const auto id = NewId();
    ComPtr<IMFMediaSource> source1, source2;
    Check(SUCCEEDED(MediaSource::CreateInstance(id, &source1)));
    Check(SUCCEEDED(MediaSource::CreateInstance(id, &source2)));

    ComPtr<IMFPresentationDescriptor> pd1, pd2;
    Check(SUCCEEDED(source1->CreatePresentationDescriptor(&pd1)));
    Check(SUCCEEDED(source2->CreatePresentationDescriptor(&pd2)));

    PROPVARIANT startPos;
    PropVariantInit(&startPos);
    startPos.vt = VT_I8;
    startPos.hVal.QuadPart = 0;
    Check(SUCCEEDED(source1->Start(pd1.Get(), nullptr, &startPos)));
    Check(SUCCEEDED(source2->Start(pd2.Get(), nullptr, &startPos)));

    ComPtr<IMFMediaEvent> event1, event2;
    Check(SUCCEEDED(source1->GetEvent(0, &event1)));
    Check(SUCCEEDED(source2->GetEvent(0, &event2)));

    PROPVARIANT sv1, sv2;
    PropVariantInit(&sv1);
    PropVariantInit(&sv2);
    Check(SUCCEEDED(event1->GetValue(&sv1)));
    Check(SUCCEEDED(event2->GetValue(&sv2)));
    ComPtr<IMFMediaStream> stream1, stream2;
    Check(SUCCEEDED(sv1.punkVal->QueryInterface(IID_PPV_ARGS(&stream1))));
    Check(SUCCEEDED(sv2.punkVal->QueryInterface(IID_PPV_ARGS(&stream2))));
    PropVariantClear(&sv1);
    PropVariantClear(&sv2);

    Check(SUCCEEDED(source1->GetEvent(0, &event1)));
    Check(SUCCEEDED(source2->GetEvent(0, &event2)));
    Check(SUCCEEDED(stream1->GetEvent(0, &event1)));
    Check(SUCCEEDED(stream2->GetEvent(0, &event2)));

    LONGLONG end1 = 0;
    for (int i = 0; i < 30; ++i) {
        Check(SUCCEEDED(stream1->RequestSample(nullptr)));
        Check(SUCCEEDED(stream1->GetEvent(0, &event1)));
        PROPVARIANT sampleVar;
        PropVariantInit(&sampleVar);
        Check(SUCCEEDED(event1->GetValue(&sampleVar)));
        ComPtr<IMFSample> sample;
        Check(SUCCEEDED(sampleVar.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
        PropVariantClear(&sampleVar);

        LONGLONG t = 0, d = 0;
        Check(SUCCEEDED(sample->GetSampleTime(&t)));
        Check(SUCCEEDED(sample->GetSampleDuration(&d)));
        Check(t == end1);
        end1 = t + d;
    }
    Check(end1 == 10'000'000);

    // Stream 2 requests its first sample; its clock must start from 0
    Check(SUCCEEDED(stream2->RequestSample(nullptr)));
    Check(SUCCEEDED(stream2->GetEvent(0, &event2)));
    PROPVARIANT sampleVar2;
    PropVariantInit(&sampleVar2);
    Check(SUCCEEDED(event2->GetValue(&sampleVar2)));
    ComPtr<IMFSample> sample2;
    Check(SUCCEEDED(sampleVar2.punkVal->QueryInterface(IID_PPV_ARGS(&sample2))));
    PropVariantClear(&sampleVar2);

    LONGLONG t2 = 0;
    Check(SUCCEEDED(sample2->GetSampleTime(&t2)));
    Check(t2 == 0);

    Check(SUCCEEDED(source1->Shutdown()));
    Check(SUCCEEDED(source2->Shutdown()));
}

void missingProducerGeneratesNeutral() {
    const auto id = NewId();
    ComPtr<IMFMediaSource> source;
    Check(SUCCEEDED(MediaSource::CreateInstance(id, &source)));

    ComPtr<IMFPresentationDescriptor> pd;
    Check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));

    PROPVARIANT startPos;
    PropVariantInit(&startPos);
    startPos.vt = VT_I8;
    startPos.hVal.QuadPart = 0;
    Check(SUCCEEDED(source->Start(pd.Get(), nullptr, &startPos)));

    ComPtr<IMFMediaEvent> event;
    Check(SUCCEEDED(source->GetEvent(0, &event)));
    PROPVARIANT streamVar;
    PropVariantInit(&streamVar);
    Check(SUCCEEDED(event->GetValue(&streamVar)));
    ComPtr<IMFMediaStream> stream;
    Check(SUCCEEDED(streamVar.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));
    PropVariantClear(&streamVar);

    Check(SUCCEEDED(source->GetEvent(0, &event)));
    Check(SUCCEEDED(stream->GetEvent(0, &event)));

    // 1. Missing producer -> neutral frame
    Check(SUCCEEDED(stream->RequestSample(nullptr)));
    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    PROPVARIANT sampleVar;
    PropVariantInit(&sampleVar);
    Check(SUCCEEDED(event->GetValue(&sampleVar)));
    ComPtr<IMFSample> sample;
    Check(SUCCEEDED(sampleVar.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
    PropVariantClear(&sampleVar);

    ComPtr<IMFMediaBuffer> buffer;
    Check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
    BYTE* data = nullptr;
    DWORD len = 0;
    Check(SUCCEEDED(buffer->Lock(&data, nullptr, &len)));

    OutputFrame expectedNeutral;
    GenerateNeutralFrame(FrameState::Disconnected, expectedNeutral);
    Check(len == kOutputFrameBytes);
    Check(std::memcmp(data, expectedNeutral.nv12.data(), kOutputFrameBytes) == 0);
    buffer->Unlock();

    // 2. Start publisher, publish live frame
    FramePublisher publisher;
    Check(SUCCEEDED(publisher.Open(id)));

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    OutputFrame liveFrame{{1, 10, static_cast<uint64_t>(qpc.QuadPart), FrameState::Live},
                          std::vector<uint8_t>(kOutputFrameBytes, 200)};
    Check(publisher.Publish(liveFrame));

    Check(SUCCEEDED(stream->RequestSample(nullptr)));
    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    PropVariantInit(&sampleVar);
    Check(SUCCEEDED(event->GetValue(&sampleVar)));
    Check(SUCCEEDED(sampleVar.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
    PropVariantClear(&sampleVar);

    Check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
    Check(SUCCEEDED(buffer->Lock(&data, nullptr, &len)));
    Check(len == kOutputFrameBytes);
    Check(data[0] == 200);
    Check(std::memcmp(data, liveFrame.nv12.data(), kOutputFrameBytes) == 0);
    buffer->Unlock();

    // 3. Close publisher -> frame reader rejects stale epoch -> generates neutral
    publisher.Close();
    Check(SUCCEEDED(stream->RequestSample(nullptr)));
    Check(SUCCEEDED(stream->GetEvent(0, &event)));
    PropVariantInit(&sampleVar);
    Check(SUCCEEDED(event->GetValue(&sampleVar)));
    Check(SUCCEEDED(sampleVar.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
    PropVariantClear(&sampleVar);

    Check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
    Check(SUCCEEDED(buffer->Lock(&data, nullptr, &len)));
    Check(std::memcmp(data, expectedNeutral.nv12.data(), kOutputFrameBytes) == 0);
    buffer->Unlock();

    Check(SUCCEEDED(source->Shutdown()));
}

}  // namespace

int main() {
    std::fprintf(stderr, "Starting main...\n");
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    Check(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)));

    std::fprintf(stderr, "Testing sourceClassFactoryProvidesActivate...\n");
    sourceClassFactoryProvidesActivate();
    std::fprintf(stderr, "Testing frameServerSourceInterfacesAreAvailable...\n");
    frameServerSourceInterfacesAreAvailable();
    std::fprintf(stderr, "Testing oneNv12Stream1080p30...\n");
    oneNv12Stream1080p30();
    std::fprintf(stderr, "Testing invalidMediaTypeRejected...\n");
    invalidMediaTypeRejected();
    std::fprintf(stderr, "Testing requestSamplePreservesToken...\n");
    requestSamplePreservesToken();
    std::fprintf(stderr, "Testing stopShutdownUnblocksRequests...\n");
    stopShutdownUnblocksRequests();
    std::fprintf(stderr, "Testing independentConsumerClocks...\n");
    independentConsumerClocks();
    std::fprintf(stderr, "Testing missingProducerGeneratesNeutral...\n");
    missingProducerGeneratesNeutral();

    MFShutdown();
    CoUninitialize();
    std::fprintf(stderr, "PASS: virtual camera source\n");
    return 0;
}
