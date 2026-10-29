#include "frame_bridge.h"

#include <Aclapi.h>
#include <Sddl.h>
#include <ShlObj.h>
#include <cstring>
#include <cwchar>
#include <limits>
#include <utility>

#if !defined(_M_X64)
#error The frame bridge requires Windows x64 aligned atomic 64-bit accesses.
#endif

namespace clone_vcam {
namespace {
constexpr std::uint64_t kMagic = 0x314d415643524444;
constexpr std::uint64_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 128;
constexpr std::size_t kMappingBytes = kHeaderBytes + kOutputFrameBytes;
constexpr std::size_t kPayloadWords = kOutputFrameBytes / sizeof(LONG64);
enum Word : std::size_t {
    Magic, Version, Length, Width, Height, PayloadLength, Commit, Epoch,
    Generation, Sequence, CapturedQpc, State, Payload = kHeaderBytes / sizeof(LONG64)
};
static_assert(kMappingBytes == 3'110'528 && kOutputFrameBytes % 8 == 0);

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct LocalAllocation {
    void* value{};
    ~LocalAllocation() { if (value) LocalFree(value); }
};
struct Mapping {
    // Directory handle denies delete sharing, keeping the validated path pinned.
    Handle directory, file, mapping;
    volatile LONG64* words{};
    std::wstring installationId;
    std::wstring producerSid;
    ~Mapping() { if (words) UnmapViewOfFile(const_cast<LONG64*>(words)); }
};

HRESULT LastError() { return HRESULT_FROM_WIN32(GetLastError()); }
std::uint64_t Load(const volatile LONG64* words, std::size_t index) {
    return static_cast<std::uint64_t>(ReadAcquire64(words + index));
}
void Store(volatile LONG64* words, std::size_t index, std::uint64_t value) {
    WriteRelease64(words + index, static_cast<LONG64>(value));
}
bool ValidLayout(const volatile LONG64* words) {
    return Load(words, Magic) == kMagic && Load(words, Version) == kVersion &&
           Load(words, Length) == kMappingBytes && Load(words, Width) == kOutputWidth &&
           Load(words, Height) == kOutputHeight && Load(words, PayloadLength) == kOutputFrameBytes;
}

HRESULT Path(const std::wstring& id, std::wstring& directory) {
    // Only canonical GUIDs enter the path. No arbitrary path, slash, or ADS input.
    GUID guid{}; wchar_t canonical[39]{};
    if (id.size() != 38 || FAILED(CLSIDFromString(id.c_str(), &guid)) ||
        !StringFromGUID2(guid, canonical, 39) || _wcsicmp(canonical, id.c_str()) != 0 || guid == GUID_NULL)
        return E_INVALIDARG;
    PWSTR root{};
    const auto hr = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &root);
    if (FAILED(hr)) return hr;
    directory = std::wstring(root) + L"\\DroidCamClone-vcam-" + canonical;
    CoTaskMemFree(root);
    return S_OK;
}

HRESULT UserSid(std::wstring& sid) {
    // Effective token matters when called under impersonation.
    Handle token;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token.value)) {
        if (GetLastError() != ERROR_NO_TOKEN || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return LastError();
    }
    DWORD bytes{}; GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> data(bytes);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), bytes, &bytes)) return LastError();
    LocalAllocation text;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,
                               reinterpret_cast<PWSTR*>(&text.value))) return LastError();
    sid = static_cast<PCWSTR>(text.value);
    return S_OK;
}
HRESULT Security(const std::wstring& user, LocalAllocation& descriptor, bool epochEvent = false) {
    // No inherited ACEs. Hosts receive read/execute only, including on directory.
    const auto sddl = epochEvent
        ? L"O:" + user + L"D:P(A;;0x001F0003;;;" + user + L")(A;;0x00120000;;;SY)(A;;0x00120000;;;LS)"
        : L"O:" + user + L"D:P(A;;FA;;;" + user + L")(A;;FRFX;;;SY)(A;;FRFX;;;LS)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value), nullptr)) return LastError();
    return S_OK;
}

// Both publisher and privileged reader validate exactly the same closed ACL.
// Reader derives owner from the pinned directory; the file must have that owner.
HRESULT ValidateSecurity(HANDLE handle, const std::wstring* expectedOwner, std::wstring& ownerText, bool epochEvent = false) {
    PSID owner{}; PACL actual{}; LocalAllocation descriptor;
    auto error = GetSecurityInfo(handle, epochEvent ? SE_KERNEL_OBJECT : SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                &owner, nullptr, &actual, nullptr, reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value));
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    LocalAllocation text;
    if (!owner || !ConvertSidToStringSidW(owner, reinterpret_cast<PWSTR*>(&text.value))) return E_ACCESSDENIED;
    ownerText = static_cast<PCWSTR>(text.value);
    if (expectedOwner && ownerText != *expectedOwner) return E_ACCESSDENIED;
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision{};
    if (!GetSecurityDescriptorControl(descriptor.value, &control, &revision) || !(control & SE_DACL_PROTECTED) || !actual) return E_ACCESSDENIED;
    LocalAllocation expected; auto hr = Security(ownerText, expected, epochEvent); if (FAILED(hr)) return hr;
    PACL wanted{}; BOOL present{}, defaulted{};
    if (!GetSecurityDescriptorDacl(expected.value, &present, &wanted, &defaulted) || !present || !wanted || actual->AceCount != wanted->AceCount) return E_ACCESSDENIED;
    // Exact ACE type/flags/mask/SID; do not accept extra principals or inherited grants.
    for (DWORD i = 0; i < wanted->AceCount; ++i) {
        void* left{}; void* right{};
        if (!GetAce(actual, i, &left) || !GetAce(wanted, i, &right)) return E_ACCESSDENIED;
        const auto size = static_cast<ACE_HEADER*>(right)->AceSize;
        if (static_cast<ACE_HEADER*>(left)->AceSize != size || std::memcmp(left, right, size) != 0) return E_ACCESSDENIED;
    }
    return S_OK;
}

HRESULT OpenMapping(const std::wstring& id, bool producer, Mapping& result, const std::wstring& expectedReaderOwner = {}) {
    std::wstring directoryPath; auto hr = Path(id, directoryPath); if (FAILED(hr)) return hr;
    result.installationId = id;
    std::wstring user; LocalAllocation descriptor; SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, FALSE};
    if (producer) {
        hr = UserSid(user); if (FAILED(hr)) return hr;
        hr = Security(user, descriptor); if (FAILED(hr)) return hr;
        attributes.lpSecurityDescriptor = descriptor.value;
        if (!CreateDirectoryW(directoryPath.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) return LastError();
    }
    result.directory.value = CreateFileW(directoryPath.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (result.directory.value == INVALID_HANDLE_VALUE) return LastError();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(result.directory.value, &info)) return LastError();
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return E_ACCESSDENIED;
    std::wstring owner;
    hr = ValidateSecurity(result.directory.value, producer ? &user : &expectedReaderOwner, owner); if (FAILED(hr)) return hr;
    result.producerSid = owner;
    result.file.value = CreateFileW((directoryPath + L"\\frames.bin").c_str(),
        producer ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        producer ? &attributes : nullptr, producer ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (result.file.value == INVALID_HANDLE_VALUE) return LastError();
    const bool created = producer && GetLastError() != ERROR_ALREADY_EXISTS;
    if (!GetFileInformationByHandle(result.file.value, &info)) return LastError();
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks != 1) return E_ACCESSDENIED;
    std::wstring fileOwner;
    hr = ValidateSecurity(result.file.value, &owner, fileOwner); if (FAILED(hr)) return hr;
    if (producer) {
        OVERLAPPED offset{};
        if (!LockFileEx(result.file.value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset)) return LastError();
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result.file.value, &size)) return LastError();
    if (created) {
        size.QuadPart = kMappingBytes;
        if (!SetFilePointerEx(result.file.value, size, nullptr, FILE_BEGIN) || !SetEndOfFile(result.file.value)) return LastError();
    } else if (size.QuadPart != kMappingBytes) return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);
    result.mapping.value = CreateFileMappingW(result.file.value, nullptr, producer ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
    if (!result.mapping.value) return LastError();
    result.words = static_cast<volatile LONG64*>(MapViewOfFile(result.mapping.value,
        producer ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, kMappingBytes));
    if (!result.words) return LastError();
    if (!created && !ValidLayout(result.words)) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return S_OK;
}

std::wstring EpochEventName(const std::wstring& id, std::uint64_t epoch) {
    GUID guid{}; CLSIDFromString(id.c_str(), &guid);
    wchar_t canonical[39]{}; StringFromGUID2(guid, canonical, 39);
    wchar_t suffix[18]{}; swprintf_s(suffix, L"-%016llX", epoch);
    return L"Global\\DroidCamClone-vcam-" + std::wstring(canonical) + suffix;
}
}  // namespace

struct FramePublisher::Impl {
    Mapping mapping;
    Handle epochEvent;
    std::uint64_t epoch{}, commit{}, generation{}, sequence{};
    bool published{};
    ~Impl() {
        if (epoch) {
            Store(mapping.words, Commit, 1);
            MemoryBarrier();
            Store(mapping.words, Epoch, 0);
        }
    }
};
struct FrameReader::Impl {
    Mapping mapping;
    std::uint64_t frequency{}, epoch{}, generation{}, sequence{};
};

FramePublisher::FramePublisher() = default;
FramePublisher::~FramePublisher() = default;
HRESULT FramePublisher::Open(const std::wstring& id) {
    Close();
    auto next = std::make_unique<Impl>();
    auto hr = OpenMapping(id, true, next->mapping); if (FAILED(hr)) return hr;
    GUID random{}; hr = CoCreateGuid(&random); if (FAILED(hr)) return hr;
    std::uint64_t epoch{}; std::memcpy(&epoch, &random, sizeof(epoch)); if (!epoch) epoch = 1;
    std::wstring user; hr = UserSid(user); if (FAILED(hr)) return hr;
    LocalAllocation eventSecurity; hr = Security(user, eventSecurity, true); if (FAILED(hr)) return hr;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), eventSecurity.value, FALSE};
    next->epochEvent.value = CreateEventExW(&attributes, EpochEventName(id, epoch).c_str(),
                                           CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!next->epochEvent.value) return LastError();
    // A preexisting object never authenticates this newly generated lifetime.
    if (GetLastError() == ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    auto* words = next->mapping.words;
    Store(words, Commit, 1); MemoryBarrier();
    Store(words, Epoch, epoch);
    Store(words, Magic, kMagic); Store(words, Version, kVersion); Store(words, Length, kMappingBytes);
    Store(words, Width, kOutputWidth); Store(words, Height, kOutputHeight); Store(words, PayloadLength, kOutputFrameBytes);
    next->epoch = epoch;
    impl_ = std::move(next);
    return S_OK;
}
bool FramePublisher::Publish(const OutputFrame& frame) {
    if (!impl_ || frame.nv12.size() != kOutputFrameBytes || !frame.metadata.generation ||
        (frame.metadata.state != FrameState::Disconnected && frame.metadata.state != FrameState::Reconnecting && frame.metadata.state != FrameState::Live)) return false;
    auto& p = *impl_;
    if (p.published && (frame.metadata.generation < p.generation ||
        (frame.metadata.generation == p.generation && frame.metadata.sequence <= p.sequence))) return false;
    if (p.commit > (std::numeric_limits<std::uint64_t>::max)() - 2 || !ValidLayout(p.mapping.words)) return false;
    auto* words = p.mapping.words;
    Store(words, Commit, p.commit + 1); MemoryBarrier();
    Store(words, Generation, frame.metadata.generation); Store(words, Sequence, frame.metadata.sequence);
    Store(words, CapturedQpc, frame.metadata.capturedQpc); Store(words, State, static_cast<std::uint64_t>(frame.metadata.state));
    // Every shared payload access is an aligned Windows atomic word access.
    // Never memcpy across concurrently changing shared memory.
    for (std::size_t i = 0; i < kPayloadWords; ++i) {
        std::uint64_t word; std::memcpy(&word, frame.nv12.data() + i * 8, 8);
        Store(words, Payload + i, word);
    }
    p.commit += 2; Store(words, Commit, p.commit);
    p.generation = frame.metadata.generation; p.sequence = frame.metadata.sequence; p.published = true;
    return true;
}
void FramePublisher::Close() noexcept { impl_.reset(); }
std::uint64_t FramePublisher::ProducerEpoch() const noexcept { return impl_ ? impl_->epoch : 0; }

FrameReader::FrameReader() = default;
FrameReader::~FrameReader() = default;
HRESULT FrameReader::Open(const std::wstring& id) {
    Close();
    std::wstring user;
    const auto hr = UserSid(user); if (FAILED(hr)) return hr;
    return Open(id, user);
}
HRESULT FrameReader::Open(const std::wstring& id, const std::wstring& expectedProducerSid) {
    Close();
    LocalAllocation sid, canonical;
    if (!ConvertStringSidToSidW(expectedProducerSid.c_str(), &sid.value) || !IsValidSid(sid.value) ||
        !ConvertSidToStringSidW(sid.value, reinterpret_cast<PWSTR*>(&canonical.value)) ||
        expectedProducerSid != static_cast<PCWSTR>(canonical.value)) return E_INVALIDARG;
    auto next = std::make_unique<Impl>();
    auto hr = OpenMapping(id, false, next->mapping, expectedProducerSid); if (FAILED(hr)) return hr;
    LARGE_INTEGER frequency; if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return E_FAIL;
    next->frequency = frequency.QuadPart; impl_ = std::move(next); return S_OK;
}
bool FrameReader::ReadLatest(std::uint64_t nowQpc, OutputFrame& output) {
    if (!impl_) return false;
    auto& r = *impl_; const auto* words = r.mapping.words;
    OutputFrame candidate;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        // Epoch must precede commit: restart reuses small commit values. Reading
        // commit first could combine an old commit with a new epoch (ABA).
        const auto epoch = Load(words, Epoch); if (!epoch) continue;
        const auto commit = Load(words, Commit);
        if (!commit || (commit & 1) || !ValidLayout(words)) continue;
        // Kernel objects cannot survive reboot. Persisted file/QPC bytes alone
        // must never authenticate a producer epoch from an earlier boot.
        // No signal state, waits or event-write rights are used by a reader.
        Handle epochEvent(OpenEventW(SYNCHRONIZE | READ_CONTROL, FALSE, EpochEventName(r.mapping.installationId, epoch).c_str()));
        if (!epochEvent.value) continue;
        std::wstring eventOwner;
        if (FAILED(ValidateSecurity(epochEvent.value, &r.mapping.producerSid, eventOwner, true))) continue;
        const auto state = Load(words, State); if (state > static_cast<std::uint64_t>(FrameState::Live)) continue;
        candidate.metadata = {Load(words, Generation), Load(words, Sequence), Load(words, CapturedQpc), static_cast<FrameState>(state)};
        if (!candidate.metadata.generation) continue;
        if (epoch == r.epoch && (candidate.metadata.generation < r.generation ||
            (candidate.metadata.generation == r.generation && candidate.metadata.sequence < r.sequence))) continue;
        // Neutral state images use the same bounded age contract as live images.
        auto freshness = candidate.metadata; freshness.state = FrameState::Live;
        if (!IsFresh(freshness, candidate.metadata.generation, nowQpc, r.frequency)) continue;
        candidate.nv12.resize(kOutputFrameBytes);
        for (std::size_t i = 0; i < kPayloadWords; ++i) {
            const auto word = Load(words, Payload + i);
            std::memcpy(candidate.nv12.data() + i * 8, &word, 8);
        }
        MemoryBarrier();
        if (Load(words, Commit) != commit || Load(words, Epoch) != epoch || !ValidLayout(words)) continue;
        r.epoch = epoch; r.generation = candidate.metadata.generation; r.sequence = candidate.metadata.sequence;
        output = std::move(candidate); return true;
    }
    return false;
}
void FrameReader::Close() noexcept { impl_.reset(); }
std::uint64_t FrameReader::ProducerEpoch() const noexcept { return impl_ ? impl_->epoch : 0; }
}  // namespace clone_vcam
