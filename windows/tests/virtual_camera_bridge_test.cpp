#include "virtual_camera/frame_bridge.h"

#include <Aclapi.h>
#include <ShlObj.h>
#include <Sddl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {
using namespace clone_vcam;
void Check(bool value, const char* what) {
    if (!value) { std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", what, GetLastError()); std::exit(1); }
}
#define CHECK(x) Check(!!(x), #x)
std::uint64_t Now() { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
std::wstring NewId() {
    GUID guid; CHECK(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39]; CHECK(StringFromGUID2(guid, id, 39)); return id;
}
std::wstring Directory(const std::wstring& id) {
    PWSTR root{}; CHECK(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &root)));
    std::wstring result = std::wstring(root) + L"\\DroidCamClone-vcam-" + id;
    CoTaskMemFree(root); return result;
}
struct Installation {
    std::wstring id = NewId();
    ~Installation() { DeleteFileW((Directory(id) + L"\\frames.bin").c_str()); RemoveDirectoryW(Directory(id).c_str()); }
};
OutputFrame Frame(std::uint64_t sequence, std::uint64_t generation = 1) {
    return {{generation, sequence, Now(), FrameState::Live},
            std::vector<std::uint8_t>(kOutputFrameBytes, static_cast<std::uint8_t>(sequence))};
}
struct RawMapping {
    HANDLE file{INVALID_HANDLE_VALUE}, mapping{};
    volatile LONG64* words{};
    RawMapping(const std::wstring& id) {
        file = CreateFileW((Directory(id) + L"\\frames.bin").c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(file != INVALID_HANDLE_VALUE);
        mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr); CHECK(mapping);
        words = static_cast<volatile LONG64*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0)); CHECK(words);
    }
    ~RawMapping() { if (words) UnmapViewOfFile(const_cast<LONG64*>(words)); if (mapping) CloseHandle(mapping); if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
struct Child {
    PROCESS_INFORMATION info{};
    Child(const std::wstring& args) {
        wchar_t executable[MAX_PATH]; CHECK(GetModuleFileNameW(nullptr, executable, MAX_PATH));
        auto command = L"\"" + std::wstring(executable) + L"\" " + args;
        STARTUPINFOW startup{sizeof(startup)};
        CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
    }
    void Finish() {
        CHECK(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0);
        DWORD code; CHECK(GetExitCodeProcess(info.hProcess, &code)); CHECK(code == 0);
    }
    ~Child() { if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(info.hProcess, 99); WaitForSingleObject(info.hProcess, 3000); } CloseHandle(info.hThread); CloseHandle(info.hProcess); }
};

void twoReadersIndependent() {
    Installation install;
    FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id)));
    FramePublisher duplicate; CHECK(FAILED(duplicate.Open(install.id)));
    FrameReader stalled, reader;
    CHECK(SUCCEEDED(stalled.Open(install.id))); CHECK(SUCCEEDED(reader.Open(install.id)));
    OutputFrame result{{9, 9, 9, FrameState::Live}, {42}};
    CHECK(!reader.ReadLatest(Now(), result)); CHECK(result.nv12 == std::vector<std::uint8_t>{42});
    for (unsigned sequence = 0; sequence <= 300; ++sequence) CHECK(publisher.Publish(Frame(sequence)));
    CHECK(reader.ReadLatest(Now(), result)); CHECK(result.metadata.sequence == 300);
    CHECK(result.nv12.size() == 3'110'400);
    CHECK(std::all_of(result.nv12.begin(), result.nv12.end(), [](auto b) { return b == 44; }));
    Child child(L"--read " + install.id); child.Finish();
    HANDLE file = CreateFileW((Directory(install.id) + L"\\frames.bin").c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE); LARGE_INTEGER size; CHECK(GetFileSizeEx(file, &size)); CloseHandle(file);
    CHECK(size.QuadPart == 3'110'528);  // Fixed 128-byte header plus one NV12 frame.
    auto invalid = Frame(301); invalid.nv12.resize(12); CHECK(!publisher.Publish(invalid));
    CHECK(!publisher.Publish(Frame(299))); CHECK(publisher.Publish(Frame(0, 2)));
    CHECK(!publisher.Publish(Frame(400, 1)));
    CHECK(reader.ReadLatest(Now(), result)); CHECK(result.metadata.generation == 2);
}

void rejectsLayoutVersionAndLength() {
    Installation install; FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id)));
    CHECK(publisher.Publish(Frame(1))); FrameReader reader; CHECK(SUCCEEDED(reader.Open(install.id)));
    RawMapping raw(install.id); OutputFrame output;
    for (const auto index : {0, 1, 2, 3, 4, 5}) {
        const auto saved = ReadAcquire64(raw.words + index);
        WriteRelease64(raw.words + index, saved + 1);
        CHECK(!reader.ReadLatest(Now(), output)); FrameReader incompatible; CHECK(FAILED(incompatible.Open(install.id)));
        WriteRelease64(raw.words + index, saved);
    }
    CHECK(reader.ReadLatest(Now(), output));
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    CHECK(!reader.ReadLatest(output.metadata.capturedQpc + frequency.QuadPart * 3 / 4, output));
    CHECK(!reader.ReadLatest(output.metadata.capturedQpc - 1, output));
}

void rejectsTruncatedBackingFile() {
    Installation install;
    { FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id))); CHECK(publisher.Publish(Frame(1))); }
    HANDLE file = CreateFileW((Directory(install.id) + L"\\frames.bin").c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE); LARGE_INTEGER length; length.QuadPart = 128;
    CHECK(SetFilePointerEx(file, length, nullptr, FILE_BEGIN)); CHECK(SetEndOfFile(file)); CloseHandle(file);
    FrameReader reader; CHECK(FAILED(reader.Open(install.id)));
    FramePublisher publisher; CHECK(FAILED(publisher.Open(install.id)));
}

void rejectsUnsafePathsAndSecurity() {
    FramePublisher publisher; FrameReader reader;
    for (const auto* path : {L"..\\..\\Windows", L"{00000000-0000-0000-0000-000000000000}", L"C:\\frames.bin", L""}) {
        CHECK(publisher.Open(path) == E_INVALIDARG); CHECK(reader.Open(path) == E_INVALIDARG);
    }
    Installation install; CHECK(SUCCEEDED(publisher.Open(install.id))); publisher.Close();
    const auto filePath = Directory(install.id) + L"\\frames.bin";
    const auto secondPath = Directory(install.id) + L"\\second.bin";
    CHECK(CreateHardLinkW(secondPath.c_str(), filePath.c_str(), nullptr));
    CHECK(FAILED(reader.Open(install.id))); CHECK(FAILED(publisher.Open(install.id))); CHECK(DeleteFileW(secondPath.c_str()));
    CHECK(SUCCEEDED(reader.Open(install.id))); reader.Close();
    PSECURITY_DESCRIPTOR insecure{};
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &insecure, nullptr));
    PACL dacl{}; BOOL present{}, defaulted{};
    CHECK(GetSecurityDescriptorDacl(insecure, &present, &dacl, &defaulted));
    CHECK(SetNamedSecurityInfoW(const_cast<PWSTR>(filePath.c_str()), SE_FILE_OBJECT,
          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS);
    LocalFree(insecure);
    CHECK(FAILED(reader.Open(install.id))); CHECK(FAILED(publisher.Open(install.id)));
}

void restartChangesGeneration() {
    Installation install; FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id)));
    CHECK(publisher.Publish(Frame(0, 40))); FrameReader reader; CHECK(SUCCEEDED(reader.Open(install.id)));
    OutputFrame output; CHECK(reader.ReadLatest(Now(), output)); const auto epoch = reader.ProducerEpoch(); CHECK(epoch != 0);
    publisher.Close(); CHECK(!reader.ReadLatest(Now(), output));
    CHECK(SUCCEEDED(publisher.Open(install.id))); CHECK(publisher.ProducerEpoch() != epoch);
    CHECK(!reader.ReadLatest(Now(), output)); CHECK(publisher.Publish(Frame(0, 1)));
    CHECK(reader.ReadLatest(Now(), output)); CHECK(output.metadata.generation == 1);
    CHECK(reader.ProducerEpoch() != epoch);
    RawMapping raw(install.id);
    CHECK(publisher.Publish(Frame(0, 2))); CHECK(reader.ReadLatest(Now(), output));
    WriteRelease64(raw.words + 8, 1); // Reader rejects a former, nonzero generation.
    CHECK(!reader.ReadLatest(Now(), output));
    WriteRelease64(raw.words + 8, 0); // Simulate an old session snapshot after a newer generation.
    CHECK(!reader.ReadLatest(Now(), output));
}

void producerCrashMidWrite() {
    Installation install;
    const auto eventName = L"Local\\DroidCamClone-test-" + install.id;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()); CHECK(ready);
    Child child(L"--write " + install.id + L" " + eventName);
    CHECK(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0); CloseHandle(ready);
    FrameReader reader; CHECK(SUCCEEDED(reader.Open(install.id)));
    RawMapping raw(install.id); bool suspendedMidWrite = false; unsigned accepted = 0;
    const auto deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        OutputFrame output;
        if (reader.ReadLatest(Now(), output)) {
            ++accepted;
            CHECK(std::all_of(output.nv12.begin(), output.nv12.end(), [&](auto b) { return b == static_cast<std::uint8_t>(output.metadata.sequence); }));
        }
        if ((ReadAcquire64(raw.words + 6) & 1) && accepted > 0) {
            CHECK(SuspendThread(child.info.hThread) != static_cast<DWORD>(-1));
            if (ReadAcquire64(raw.words + 6) & 1) { suspendedMidWrite = true; break; }
            CHECK(ResumeThread(child.info.hThread) != static_cast<DWORD>(-1));
        }
    }
    CHECK(accepted > 0); CHECK(suspendedMidWrite);
    CHECK(TerminateProcess(child.info.hProcess, 0)); child.Finish();
    const auto started = GetTickCount64(); OutputFrame output;
    for (unsigned i = 0; i < 1000; ++i) CHECK(!reader.ReadLatest(Now(), output));
    CHECK(GetTickCount64() - started < 1000); // Incomplete writes return without copying or waiting.
    FramePublisher restarted; CHECK(SUCCEEDED(restarted.Open(install.id)));
    CHECK(!reader.ReadLatest(Now(), output)); CHECK(restarted.Publish(Frame(0)));
    CHECK(reader.ReadLatest(Now(), output));
}

void persistedPayloadWithoutEpochEventRejected() {
    Installation install; const auto eventName = L"Local\\DroidCamClone-test-" + install.id;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()); CHECK(ready);
    Child child(L"--once " + install.id + L" " + eventName);
    CHECK(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0); CloseHandle(ready);
    FrameReader reader; CHECK(SUCCEEDED(reader.Open(install.id)));
    OutputFrame output; CHECK(reader.ReadLatest(Now(), output));
    CHECK(TerminateProcess(child.info.hProcess, 0)); child.Finish();
    RawMapping raw(install.id); CHECK(ReadAcquire64(raw.words + 6) != 0); CHECK(!(ReadAcquire64(raw.words + 6) & 1));
    // Payload is still complete and fresh, but its kernel epoch object is gone.
    CHECK(!reader.ReadLatest(output.metadata.capturedQpc, output));
}

void rejectsSameNameEpochImpostor() {
    Installation install; const auto readyName = L"Local\\DroidCamClone-test-" + install.id;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()); CHECK(ready);
    Child child(L"--once " + install.id + L" " + readyName);
    CHECK(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0); CloseHandle(ready);
    FrameReader reader; CHECK(SUCCEEDED(reader.Open(install.id)));
    OutputFrame output; CHECK(reader.ReadLatest(Now(), output));
    const auto freshQpc = output.metadata.capturedQpc;
    wchar_t suffix[18]{}; swprintf_s(suffix, L"-%016llX", reader.ProducerEpoch());
    const auto eventName = L"Global\\DroidCamClone-vcam-" + install.id + suffix;
    // Simulate another reader retaining the genuine, authenticated epoch event.
    HANDLE retained = OpenEventW(SYNCHRONIZE | READ_CONTROL, FALSE, eventName.c_str()); CHECK(retained);
    CHECK(TerminateProcess(child.info.hProcess, 0)); child.Finish();
    CHECK(reader.ReadLatest(freshQpc, output));
    LARGE_INTEGER frequency; CHECK(QueryPerformanceFrequency(&frequency));
    CHECK(!reader.ReadLatest(freshQpc + frequency.QuadPart * 3 / 4, output));
    CloseHandle(retained);
    CHECK(!reader.ReadLatest(freshQpc, output));

    // Freshness is explicitly held at capture time: only event authentication
    // can reject this accessible replacement with a learned authentic name.
    PSECURITY_DESCRIPTOR permissive{};
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;WD)", SDDL_REVISION_1, &permissive, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), permissive, FALSE};
    HANDLE impostor = CreateEventExW(&attributes, eventName.c_str(), CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    CHECK(impostor); CHECK(GetLastError() != ERROR_ALREADY_EXISTS); LocalFree(permissive);
    HANDLE accessible = OpenEventW(SYNCHRONIZE | READ_CONTROL, FALSE, eventName.c_str()); CHECK(accessible); CloseHandle(accessible);
    CHECK(!reader.ReadLatest(freshQpc, output));
    CloseHandle(impostor);
}

void unauthorizedUserDenied() {
    Installation install; FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id)));
    wchar_t epochSuffix[18]{}; swprintf_s(epochSuffix, L"-%016llX", publisher.ProducerEpoch());
    const auto epochEventName = L"Global\\DroidCamClone-vcam-" + install.id + epochSuffix;
    HANDLE token{}, restricted{}; CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE, &token));
    BYTE sidStorage[SECURITY_MAX_SID_SIZE]; DWORD sidLength = sizeof(sidStorage);
    CHECK(CreateWellKnownSid(WinWorldSid, nullptr, sidStorage, &sidLength));
    SID_AND_ATTRIBUTES restrictedSid{sidStorage, 0};
    CHECK(CreateRestrictedToken(token, 0, 0, nullptr, 0, nullptr, 1, &restrictedSid, &restricted));
    CHECK(ImpersonateLoggedOnUser(restricted));
    FrameReader denied; const auto hr = denied.Open(install.id);
    HANDLE write = CreateFileW((Directory(install.id) + L"\\frames.bin").c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE deniedEvent = OpenEventW(SYNCHRONIZE, FALSE, epochEventName.c_str());
    CHECK(RevertToSelf()); CloseHandle(restricted); CloseHandle(token);
    CHECK(FAILED(hr)); CHECK(write == INVALID_HANDLE_VALUE); CHECK(!deniedEvent);
    // Inspect real kernel security descriptor: service principals can read, cannot write.
    HANDLE file = CreateFileW((Directory(install.id) + L"\\frames.bin").c_str(), READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); CHECK(file != INVALID_HANDLE_VALUE);
    PACL dacl{}; PSECURITY_DESCRIPTOR descriptor{};
    CHECK(GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    CHECK(dacl && dacl->AceCount == 3);
    for (DWORD sidKind : {static_cast<DWORD>(WinLocalServiceSid), static_cast<DWORD>(WinLocalSystemSid)}) {
        sidLength = sizeof(sidStorage); CHECK(CreateWellKnownSid(static_cast<WELL_KNOWN_SID_TYPE>(sidKind), nullptr, sidStorage, &sidLength));
        TRUSTEE_W trustee{}; BuildTrusteeWithSidW(&trustee, sidStorage); ACCESS_MASK rights{};
        CHECK(GetEffectiveRightsFromAclW(dacl, &trustee, &rights) == ERROR_SUCCESS);
        CHECK((rights & FILE_READ_DATA) != 0); CHECK((rights & (FILE_WRITE_DATA | FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | DELETE)) == 0);
    }
    LocalFree(descriptor); CloseHandle(file);
    HANDLE event = OpenEventW(READ_CONTROL, FALSE, epochEventName.c_str()); CHECK(event);
    descriptor = nullptr; dacl = nullptr;
    CHECK(GetSecurityInfo(event, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                          &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    CHECK(dacl && dacl->AceCount == 3);
    for (auto kind : {WinLocalServiceSid, WinLocalSystemSid}) {
        sidLength = sizeof(sidStorage); CHECK(CreateWellKnownSid(kind, nullptr, sidStorage, &sidLength));
        TRUSTEE_W trustee{}; BuildTrusteeWithSidW(&trustee, sidStorage); ACCESS_MASK rights{};
        CHECK(GetEffectiveRightsFromAclW(dacl, &trustee, &rights) == ERROR_SUCCESS);
        CHECK(rights == (SYNCHRONIZE | READ_CONTROL));
    }
    LocalFree(descriptor); CloseHandle(event);
}

void requiresTrustedProducerOwner() {
    Installation install; FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(install.id)));
    CHECK(publisher.Publish(Frame(0)));
    FrameReader reader;
    CHECK(reader.Open(install.id, L"S-1-5-19") == E_ACCESSDENIED); // LocalService is not the producer.
    CHECK(reader.Open(install.id, L"S-1-5-18") == E_ACCESSDENIED);
    CHECK(reader.Open(install.id, L"not-a-sid") == E_INVALIDARG);
    HANDLE token{}; CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD bytes{}; GetTokenInformation(token, TokenUser, nullptr, 0, &bytes); std::vector<BYTE> user(bytes);
    CHECK(GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes)); CloseHandle(token);
    PWSTR sid{}; CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid));
    CHECK(SUCCEEDED(reader.Open(install.id, sid))); LocalFree(sid);
    OutputFrame frame; CHECK(reader.ReadLatest(Now(), frame));
}

}  // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && std::wstring(argv[1]) == L"--read") {
        FrameReader reader; CHECK(SUCCEEDED(reader.Open(argv[2]))); OutputFrame frame;
        CHECK(reader.ReadLatest(Now(), frame)); CHECK(frame.metadata.sequence == 300);
        CHECK(std::all_of(frame.nv12.begin(), frame.nv12.end(), [](auto b) { return b == 44; })); return 0;
    }
    if (argc == 4 && (std::wstring(argv[1]) == L"--write" || std::wstring(argv[1]) == L"--once")) {
        FramePublisher publisher; CHECK(SUCCEEDED(publisher.Open(argv[2])));
        if (std::wstring(argv[1]) == L"--once") CHECK(publisher.Publish(Frame(0)));
        HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]); CHECK(ready); CHECK(SetEvent(ready)); CloseHandle(ready);
        if (std::wstring(argv[1]) == L"--once") Sleep(INFINITE);
        for (std::uint64_t seq = 0;; ++seq) { CHECK(publisher.Publish(Frame(seq))); Sleep(2); }
    }
    twoReadersIndependent(); rejectsLayoutVersionAndLength(); rejectsTruncatedBackingFile();
    rejectsUnsafePathsAndSecurity(); restartChangesGeneration();
    producerCrashMidWrite(); persistedPayloadWithoutEpochEventRejected();
    rejectsSameNameEpochImpostor(); unauthorizedUserDenied(); requiresTrustedProducerOwner();
    std::puts("PASS: virtual camera bridge (independent child processes, crash, ACL, restart, bounded layout)");
}
