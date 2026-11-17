#include "archive7z.h"

#include "CPP/Common/MyInitGuid.h"

#include "CPP/7zip/Archive/7z/7zHandler.h"
#include "CPP/7zip/Archive/Rar/Rar5Handler.h"
#include "CPP/7zip/Archive/Rar/RarHandler.h"
#include "CPP/7zip/Common/RegisterArc.h"
#include "CPP/7zip/ICoder.h"
#include "CPP/7zip/IPassword.h"
#include "CPP/Common/UTFConvert.h"
#include "CPP/Windows/PropVariant.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

IInArchive* CreateReadOnlyZipHandler();

// RAR handlers register themselves in upstream bundles. This library constructs handlers directly.
void RegisterArc(const CArcInfo*) throw() {}

namespace {

constexpr PROPID kRawName = kpidUserDefined;
constexpr PROPID kRawNameUtf8 = kpidUserDefined + 1;
constexpr PROPID kUnicodeName = kpidUserDefined + 2;

enum class Format { zip, rar4, rar5, seven_zip };

struct Failure {
    ogplay_7z_status status;
    std::string message;
};

ogplay_7z_status Result(ogplay_7z_error* error, ogplay_7z_status status, const char* message) noexcept {
    if (error) {
        error->code = status;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
        // Keep the message valid UTF-8 if the buffer clipped a multibyte character.
        size_t end = std::char_traits<char>::length(error->message);
        if (message[end] != '\0' && (static_cast<unsigned char>(message[end]) & 0xc0) == 0x80) {
            do {
                --end;
            } while ((static_cast<unsigned char>(message[end]) & 0xc0) == 0x80);
            error->message[end] = '\0';
        }
    }
    return status;
}

template <class Operation>
ogplay_7z_status Call(ogplay_7z_error* error, Operation operation) noexcept {
    try {
        operation();
        return Result(error, OGPLAY_7Z_OK, "");
    } catch (const Failure& failure) {
        return Result(error, failure.status, failure.message.c_str());
    } catch (const std::bad_alloc&) {
        return Result(error, OGPLAY_7Z_NO_MEMORY, "Archive decoder ran out of memory");
    } catch (...) {
        return Result(error, OGPLAY_7Z_INVALID, "Archive decoder failed");
    }
}

void Check(HRESULT result) {
    if (result == E_OUTOFMEMORY) throw std::bad_alloc{};
    if (result != S_OK) throw Failure{OGPLAY_7Z_INVALID, "Invalid or incomplete archive"};
}

std::string Bytes(const PROPVARIANT& value) {
    if (value.vt != VT_BSTR || !value.bstrVal) return {};
    return {reinterpret_cast<const char*>(value.bstrVal), ::SysStringByteLen(value.bstrVal)};
}

std::string Utf8(const PROPVARIANT& value) {
    if (value.vt != VT_BSTR || !value.bstrVal) return {};
    AString utf8;
    ConvertUnicodeToUTF8(UString(value.bstrVal), utf8);
    return {utf8, utf8.Len()};
}

bool Flag(const PROPVARIANT& value) {
    return value.vt == VT_BOOL && value.boolVal != VARIANT_FALSE;
}

uint64_t Number(const PROPVARIANT& value) {
    if (value.vt == VT_UI4) return value.ulVal;
    return value.vt == VT_UI8 ? value.uhVal.QuadPart : 0;
}

class InputStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)

public:
    explicit InputStream(ogplay_7z_input input) : input_(input) {}

    bool Failed() const { return failed_; }

    void ClearError() { failed_ = false; }

private:
    ogplay_7z_input input_;
    uint64_t offset_{};
    bool failed_{};
};

Z7_COM7F_IMF(InputStream::Read(void* data, UInt32 capacity, UInt32* processed)) {
    if (processed) *processed = 0;
    if (offset_ >= input_.size || capacity == 0) return S_OK;
    capacity = static_cast<UInt32>(std::min<uint64_t>(capacity, input_.size - offset_));
    size_t count = 0;
    if (input_.read(input_.context, offset_, data, capacity, &count) || count == 0 || count > capacity) {
        failed_ = true;
        return E_FAIL;
    }
    offset_ += count;
    if (processed) *processed = static_cast<UInt32>(count);
    return S_OK;
}

Z7_COM7F_IMF(InputStream::Seek(Int64 offset, UInt32 origin, UInt64* position)) {
    uint64_t base = 0;
    switch (origin) {
    case STREAM_SEEK_SET: break;
    case STREAM_SEEK_CUR: base = offset_; break;
    case STREAM_SEEK_END: base = input_.size; break;
    default: return E_INVALIDARG;
    }
    if (offset < 0) {
        const auto distance = uint64_t{0} - static_cast<uint64_t>(offset);
        if (distance > base) return E_INVALIDARG;
        offset_ = base - distance;
    } else {
        const auto distance = static_cast<uint64_t>(offset);
        if (distance > std::numeric_limits<int64_t>::max() - base) return E_INVALIDARG;
        offset_ = base + distance;
    }
    if (position) *position = offset_;
    return S_OK;
}

class OpenCallback final : public IArchiveOpenCallback, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IArchiveOpenCallback, ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)

public:
    bool encrypted{};
};

Z7_COM7F_IMF(OpenCallback::SetTotal(const UInt64*, const UInt64*)) {
    return S_OK;
}

Z7_COM7F_IMF(OpenCallback::SetCompleted(const UInt64*, const UInt64*)) {
    return S_OK;
}

Z7_COM7F_IMF(OpenCallback::CryptoGetTextPassword(BSTR* password)) {
    *password = nullptr;
    encrypted = true;
    return E_ABORT;
}

Format Detect(IInStream& input) {
    std::array<char, 8> head{};
    size_t length = 0;
    while (length < head.size()) {
        UInt32 count = 0;
        Check(input.Read(head.data() + length, static_cast<UInt32>(head.size() - length), &count));
        if (count == 0) break;
        length += count;
    }
    Check(input.Seek(0, STREAM_SEEK_SET, nullptr));
    const std::string_view magic(head.data(), length);
    if (magic.starts_with("7z\xbc\xaf\x27\x1c")) return Format::seven_zip;
    if (magic.starts_with(std::string_view("Rar!\x1a\x07\x00", 7))) return Format::rar4;
    if (magic.starts_with(std::string_view("Rar!\x1a\x07\x01\x00", 8))) return Format::rar5;
    if (magic.starts_with("PK\x07\x08")) throw Failure{OGPLAY_7Z_MULTIVOLUME, "Multi-volume ZIP archive"};
    if (magic.starts_with("PK\x03\x04") || magic.starts_with("PK\x05\x06") || magic.starts_with("PK\x06\x06") ||
        magic.starts_with("PK00"))
        return Format::zip;
    throw Failure{OGPLAY_7Z_INVALID, "Expected a ZIP, RAR or 7z archive"};
}

IInArchive* Create(Format format) {
    switch (format) {
    case Format::zip: return CreateReadOnlyZipHandler();
    case Format::rar4: return new NArchive::NRar::CHandler;
    case Format::rar5: return new NArchive::NRar5::CHandler;
    case Format::seven_zip: return new NArchive::N7z::CHandler;
    }
    return nullptr;
}

struct Member {
    std::string name;
    std::string unicode_name;
    uint64_t size{};
    bool name_is_utf8{};
    bool directory{};
    bool encrypted{};
    bool split{};
    bool special{};
};

}  // namespace

struct ogplay_7z_archive {
    CMyComPtr<IInStream> stream;
    CMyComPtr<IInArchive> handler;
    InputStream* reader{};
    Format format{};
    bool volume{};
    std::vector<Member> members;
};

namespace {

NWindows::NCOM::CPropVariant Property(const ogplay_7z_archive& archive, UInt32 index, PROPID id) {
    NWindows::NCOM::CPropVariant value;
    Check(archive.handler->GetProperty(index, id, &value));
    return value;
}

NWindows::NCOM::CPropVariant ArchiveProperty(const ogplay_7z_archive& archive, PROPID id) {
    NWindows::NCOM::CPropVariant value;
    Check(archive.handler->GetArchiveProperty(id, &value));
    return value;
}

Member ReadMember(const ogplay_7z_archive& archive, UInt32 index) {
    Member member;
    if (archive.format == Format::zip || archive.format == Format::rar4) {
        const auto raw = Property(archive, index, kRawName);
        if (raw.vt != VT_BSTR) throw Failure{OGPLAY_7Z_INVALID, "Archive filename properties unavailable"};
        member.name = Bytes(raw);
        const auto unicode = Property(archive, index, kUnicodeName);
        if (archive.format == Format::zip) {
            member.name_is_utf8 = Flag(Property(archive, index, kRawNameUtf8));
            member.unicode_name = Bytes(unicode);
        } else {
            member.unicode_name = Utf8(unicode);
        }
    } else {
        member.name = Utf8(Property(archive, index, kpidPath));
        member.name_is_utf8 = true;
    }
    member.size = Number(Property(archive, index, kpidSize));
    member.directory = Flag(Property(archive, index, kpidIsDir));
    member.encrypted = Flag(Property(archive, index, kpidEncrypted));
    member.split = Flag(Property(archive, index, kpidSplitBefore)) || Flag(Property(archive, index, kpidSplitAfter));
    if (archive.format == Format::zip && Number(Property(archive, index, kpidVolumeIndex))) member.split = true;
    const auto attrib = Number(Property(archive, index, kpidAttrib));
    auto mode = Number(Property(archive, index, kpidPosixAttrib));
    if (!mode) mode = attrib >> 16;
    const auto kind = mode & 0170000;
    member.special = (kind && kind != 0100000 && kind != 0040000) || (attrib & 0x400) ||
                     Flag(Property(archive, index, kpidIsAnti)) || Flag(Property(archive, index, kpidIsAltStream)) ||
                     Property(archive, index, kpidSymLink).vt != VT_EMPTY ||
                     Property(archive, index, kpidHardLink).vt != VT_EMPTY ||
                     Property(archive, index, kpidCopyLink).vt != VT_EMPTY;
    return member;
}

struct Extraction {
    const ogplay_7z_output* output{};
    uint64_t written{};
    uint64_t remaining{};
    bool io_error{};
    bool wrong_size{};

    bool Cancelled() const { return output->cancelled && output->cancelled(output->context); }
};

class OutputStream final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_0
    Z7_IFACE_COM7_IMP(ISequentialOutStream)

public:
    explicit OutputStream(Extraction& extraction) : extraction_(extraction) {}

private:
    Extraction& extraction_;
};

Z7_COM7F_IMF(OutputStream::Write(const void* data, UInt32 size, UInt32* processed)) {
    if (processed) *processed = 0;
    if (extraction_.Cancelled()) return E_ABORT;
    if (size > extraction_.remaining) {
        extraction_.wrong_size = true;
        return E_FAIL;
    }
    const auto& output = *extraction_.output;
    if (size && output.write(output.context, data, size)) {
        extraction_.io_error = true;
        return E_FAIL;
    }
    extraction_.remaining -= size;
    extraction_.written += size;
    if (processed) *processed = size;
    if (size && output.progress) output.progress(output.context, extraction_.written);
    return S_OK;
}

class ExtractCallback final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IArchiveExtractCallback, IProgress)
    Z7_IFACE_COM7_IMP(IProgress)
    Z7_IFACE_COM7_IMP(IArchiveExtractCallback)

public:
    ExtractCallback(ogplay_7z_archive& archive, Extraction& extraction) : archive_(archive), extraction_(extraction) {}

    UInt32 index{};
    UInt32 completed{};
    Int32 operation_result = NArchive::NExtract::NOperationResult::kOK;

private:
    ogplay_7z_archive& archive_;
    Extraction& extraction_;
    bool selected_{};
};

Z7_COM7F_IMF(ExtractCallback::SetTotal(UInt64)) {
    return extraction_.Cancelled() ? E_ABORT : S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetCompleted(const UInt64*)) {
    return extraction_.Cancelled() ? E_ABORT : S_OK;
}

Z7_COM7F_IMF(ExtractCallback::GetStream(UInt32 item, ISequentialOutStream** stream, Int32 mode)) {
    *stream = nullptr;
    index = item;
    selected_ = mode == NArchive::NExtract::NAskMode::kExtract;
    if (extraction_.Cancelled()) return E_ABORT;
    if (!selected_) return S_OK;
    if (item >= archive_.members.size()) return E_FAIL;
    auto out = std::unique_ptr<OutputStream>(new (std::nothrow) OutputStream(extraction_));
    if (!out) return E_OUTOFMEMORY;
    const auto& output = *extraction_.output;
    if (output.begin(output.context, item)) {
        extraction_.io_error = true;
        return E_FAIL;
    }
    extraction_.remaining = archive_.members[item].size;
    *stream = out.release();
    (*stream)->AddRef();
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::PrepareOperation(Int32)) {
    return extraction_.Cancelled() ? E_ABORT : S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetOperationResult(Int32 result)) {
    operation_result = result;
    if (result != NArchive::NExtract::NOperationResult::kOK) return E_FAIL;
    if (!selected_) return S_OK;
    if (extraction_.remaining) {
        extraction_.wrong_size = true;
        return E_FAIL;
    }
    const auto& output = *extraction_.output;
    if (output.end(output.context)) {
        extraction_.io_error = true;
        return E_FAIL;
    }
    ++completed;
    return S_OK;
}

std::string Method(const ogplay_7z_archive& archive, UInt32 index) {
    auto method = Utf8(Property(archive, index, kpidMethod));
    if (archive.format == Format::rar4) {
        const auto version = Number(Property(archive, index, kpidUnpackVer));
        method = "RAR " + method;
        if (version >= 29 && version <= 40) method += " (RAR3 VM filter or method)";
    }
    if (method.empty()) method = Utf8(ArchiveProperty(archive, kpidMethod));
    return method.empty() ? "unknown" : method;
}

}  // namespace

ogplay_7z_status ogplay_7z_open(const ogplay_7z_input* input, ogplay_7z_archive** archive, ogplay_7z_error* error) {
    if (archive) *archive = nullptr;
    return Call(error, [&] {
        if (!archive || !input || !input->read || input->size > std::numeric_limits<int64_t>::max())
            throw Failure{OGPLAY_7Z_INVALID, "Invalid archive input"};
        auto handle = std::make_unique<ogplay_7z_archive>();
        handle->reader = new InputStream(*input);
        handle->stream = handle->reader;
        try {
            handle->format = Detect(*handle->stream);
        } catch (...) {
            if (handle->reader->Failed()) throw Failure{OGPLAY_7Z_IO, "Cannot read archive"};
            throw;
        }
        handle->handler = Create(handle->format);
        auto* callback = new OpenCallback;
        const CMyComPtr<IArchiveOpenCallback> open(callback);
        const UInt64 search = 0;
        const auto status = handle->handler->Open(handle->stream, &search, open);
        if (handle->reader->Failed()) throw Failure{OGPLAY_7Z_IO, "Cannot read archive"};
        const auto flags = Number(ArchiveProperty(*handle, kpidErrorFlags));
        if (callback->encrypted || Flag(ArchiveProperty(*handle, kpidEncrypted)) ||
            (flags & kpv_ErrorFlags_EncryptedHeadersError))
            throw Failure{OGPLAY_7Z_ENCRYPTED, "Archive headers are encrypted"};
        handle->volume = Flag(ArchiveProperty(*handle, kpidIsVolume));
        if (handle->volume && status != S_OK) throw Failure{OGPLAY_7Z_MULTIVOLUME, "Multi-volume archive"};
        if (status == E_NOTIMPL || (flags & kpv_ErrorFlags_UnsupportedMethod))
            throw Failure{OGPLAY_7Z_UNSUPPORTED, "Unsupported archive header compression method"};
        Check(status);
        if (!handle->volume && (flags & (kpv_ErrorFlags_IsNotArc | kpv_ErrorFlags_HeadersError |
                                         kpv_ErrorFlags_UnavailableStart | kpv_ErrorFlags_UnexpectedEnd)))
            throw Failure{OGPLAY_7Z_INVALID, "Invalid or incomplete archive headers"};
        UInt32 count = 0;
        Check(handle->handler->GetNumberOfItems(&count));
        handle->members.reserve(count);
        for (UInt32 i = 0; i < count; ++i)
            handle->members.push_back(ReadMember(*handle, i));
        *archive = handle.release();
    });
}

void ogplay_7z_close(ogplay_7z_archive* archive) {
    delete archive;
}

uint32_t ogplay_7z_count(const ogplay_7z_archive* archive) {
    return archive ? static_cast<uint32_t>(archive->members.size()) : 0;
}

int ogplay_7z_is_volume(const ogplay_7z_archive* archive) {
    return archive && archive->volume;
}

ogplay_7z_status ogplay_7z_get(const ogplay_7z_archive* archive, uint32_t index, ogplay_7z_entry* entry,
                               ogplay_7z_error* error) {
    if (!archive || !entry || index >= archive->members.size())
        return Result(error, OGPLAY_7Z_INVALID, "Invalid archive entry index");
    const auto& member = archive->members[index];
    *entry = {{member.name.data(), member.name.size()},
              {member.unicode_name.data(), member.unicode_name.size()},
              member.size,
              member.name_is_utf8,
              member.directory,
              member.encrypted,
              member.split,
              member.special};
    return Result(error, OGPLAY_7Z_OK, "");
}

ogplay_7z_status ogplay_7z_extract(ogplay_7z_archive* archive, const uint32_t* indices, uint32_t count,
                                   const ogplay_7z_output* output, ogplay_7z_error* error) {
    return Call(error, [&] {
        if (!archive || (count && !indices) || !output || !output->begin || !output->write || !output->end)
            throw Failure{OGPLAY_7Z_INVALID, "Invalid archive extraction arguments"};
        if (archive->volume) throw Failure{OGPLAY_7Z_MULTIVOLUME, "Multi-volume archive"};
        for (uint32_t i = 0; i < count; ++i) {
            if (indices[i] >= archive->members.size() || (i && indices[i - 1] >= indices[i]))
                throw Failure{OGPLAY_7Z_INVALID, "Invalid archive extraction indices"};
            const auto& member = archive->members[indices[i]];
            if (member.encrypted) throw Failure{OGPLAY_7Z_ENCRYPTED, "Encrypted archive entry"};
            if (member.split) throw Failure{OGPLAY_7Z_MULTIVOLUME, "Split archive entry"};
            if (member.directory || member.special) throw Failure{OGPLAY_7Z_INVALID, "Entry is not a regular file"};
        }
        Extraction extraction{output};
        if (extraction.Cancelled()) throw Failure{OGPLAY_7Z_CANCELLED, "Extraction cancelled"};
        if (count == 0) return;
        archive->reader->ClearError();
        auto* callback = new ExtractCallback(*archive, extraction);
        const CMyComPtr<IArchiveExtractCallback> extract(callback);
        const auto status = archive->handler->Extract(indices, count, 0, extract);
        using namespace NArchive::NExtract::NOperationResult;
        if (extraction.Cancelled()) throw Failure{OGPLAY_7Z_CANCELLED, "Extraction cancelled"};
        if (archive->reader->Failed() || extraction.io_error)
            throw Failure{OGPLAY_7Z_IO, "Archive input/output failed"};
        if (status == E_NOTIMPL || callback->operation_result == kUnsupportedMethod)
            throw Failure{OGPLAY_7Z_UNSUPPORTED, Method(*archive, callback->index)};
        if (callback->operation_result == kWrongPassword) throw Failure{OGPLAY_7Z_ENCRYPTED, "Encrypted archive entry"};
        if (callback->operation_result == kCRCError) throw Failure{OGPLAY_7Z_INVALID, "Archive entry CRC check failed"};
        if (extraction.wrong_size || callback->operation_result == kUnexpectedEnd)
            throw Failure{OGPLAY_7Z_INVALID, "Archive entry is incomplete or has an invalid size"};
        if (callback->operation_result != kOK) throw Failure{OGPLAY_7Z_INVALID, "Archive entry decoding failed"};
        Check(status);
        if (callback->completed != count) throw Failure{OGPLAY_7Z_INVALID, "Archive extraction did not finish"};
    });
}
