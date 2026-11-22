#ifdef _WIN32
#include "windows_virtual_mime.hpp"
#include "transfer_plan.hpp"
#include "mime_bridge.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <QByteArray>
#include <QStringList>

#include <Windows.h>
#include <ObjIdl.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {
constexpr auto TransferMime = "application/vnd.arky.virtualfiles";
constexpr std::size_t Chunk = 64 * 1024;

// The Shell owns IStream references and is allowed to pull data after the
// Qt drag function returns. Each instance maintains its own libarchive reader.
class ArchiveEntryStream final : public IStream {
public:
    ArchiveEntryStream(QString archive, std::string path, std::string password, std::uint64_t size)
        : archive_(std::move(archive)), path_(std::move(path)), password_(std::move(password)), size_(size) {}
    ~ArchiveEntryStream() { closeReader(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ISequentialStream) || IsEqualIID(iid, IID_IStream)) {
            *out = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = --refs_;
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Read(void* output, ULONG requested, ULONG* actual) override {
        if (actual) *actual = 0;
        if (!output && requested) return STG_E_INVALIDPOINTER;
        if (!requested) return S_OK;
        if (!reader_ && !openReader()) return STG_E_READFAULT;
        auto* destination = static_cast<char*>(output);
        ULONG total = 0;
        while (total < requested) {
            const auto n = archive_read_data(reader_, destination + total,
                                               (std::min<std::size_t>)(requested - total, Chunk));
            if (n < 0) return STG_E_READFAULT;
            if (n == 0) break;
            total += static_cast<ULONG>(n);
            offset_ += static_cast<std::uint64_t>(n);
        }
        if (actual) *actual = total;
        return total == requested ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta, DWORD origin, ULARGE_INTEGER* pos) override {
        std::uint64_t base = 0;
        switch (origin) {
        case STREAM_SEEK_SET: break;
        case STREAM_SEEK_CUR: base = offset_; break;
        case STREAM_SEEK_END: base = size_; break;
        default: return STG_E_INVALIDFUNCTION;
        }
        const auto amount = delta.QuadPart;
        std::uint64_t target = 0;
        if (amount < 0) {
            // Handle INT64_MIN without negating a signed overflow.
            const auto magnitude = static_cast<std::uint64_t>(-(amount + 1)) + 1;
            if (magnitude > base) return STG_E_INVALIDFUNCTION;
            target = base - magnitude;
        } else {
            if (static_cast<std::uint64_t>(amount) > (std::numeric_limits<std::uint64_t>::max)() - base)
                return STG_E_INVALIDFUNCTION;
            target = base + static_cast<std::uint64_t>(amount);
        }
        if (target < offset_) closeReader();
        if (!reader_ && !openReader()) return STG_E_READFAULT;
        std::array<char, Chunk> scratch{};
        while (offset_ < target) {
            const auto requested = static_cast<ULONG>((std::min<std::uint64_t>)(target - offset_, scratch.size()));
            ULONG actual = 0;
            const auto result = Read(scratch.data(), requested, &actual);
            if (FAILED(result) || actual == 0) return STG_E_SEEKERROR;
        }
        if (pos) pos->QuadPart = offset_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* target, ULARGE_INTEGER bytes, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
        if (!target) return STG_E_INVALIDPOINTER;
        if (read) read->QuadPart = 0;
        if (written) written->QuadPart = 0;
        std::array<char, Chunk> buffer{};
        std::uint64_t moved = 0;
        while (moved < bytes.QuadPart) {
            ULONG got = 0;
            const auto ask = static_cast<ULONG>((std::min<std::uint64_t>)(bytes.QuadPart - moved, buffer.size()));
            const auto status = Read(buffer.data(), ask, &got);
            if (FAILED(status)) return status;
            if (!got) break;
            ULONG done = 0;
            const auto writeStatus = target->Write(buffer.data(), got, &done);
            if (read) read->QuadPart += got;
            if (written) written->QuadPart += done;
            moved += got;
            if (FAILED(writeStatus) || done != got) return STG_E_WRITEFAULT;
        }
        return moved == bytes.QuadPart ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_REVERTED; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        std::memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = size_;
        stat->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        auto* stream = new ArchiveEntryStream(archive_, path_, password_, size_);
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset_);
        if (FAILED(stream->Seek(position, STREAM_SEEK_SET, nullptr))) { stream->Release(); return STG_E_READFAULT; }
        *out = stream;
        return S_OK;
    }
private:
    void closeReader() {
        if (reader_) archive_read_free(reader_);
        reader_ = nullptr;
        offset_ = 0;
    }
    bool openReader() {
        closeReader();
        reader_ = archive_read_new();
        if (!reader_) return false;
        archive_read_support_filter_all(reader_);
        archive_read_support_format_all(reader_);
        if (!password_.empty()) archive_read_add_passphrase(reader_, password_.c_str());
        const auto native = archive_.toStdWString();
        if (archive_read_open_filename_w(reader_, native.c_str(), Chunk) != ARCHIVE_OK) { closeReader(); return false; }
        archive_entry* entry = nullptr;
        for (;;) {
            const auto result = archive_read_next_header(reader_, &entry);
            if (result == ARCHIVE_EOF || result < ARCHIVE_WARN || !entry) break;
            std::string name;
            if (const auto* u8 = archive_entry_pathname_utf8(entry)) name = u8;
            else if (const auto* wide = archive_entry_pathname_w(entry)) {
                const auto converted = QString::fromWCharArray(wide).toUtf8();
                name.assign(converted.constData(), converted.size());
            } else if (const auto* raw = archive_entry_pathname(entry)) name = raw;
            while (!name.empty() && name.back() == '/') name.pop_back();
            if (name == path_) return true;
            if (archive_read_data_skip(reader_) < ARCHIVE_WARN) break;
        }
        closeReader();
        return false;
    }
    std::atomic<ULONG> refs_{1};
    QString archive_;
    std::string path_;
    std::string password_;
    std::uint64_t size_ = 0;
    std::uint64_t offset_ = 0;
    archive* reader_ = nullptr;
};

}

WindowsVirtualMimeData::WindowsVirtualMimeData(std::vector<arky::WindowsTransferItem> files,
                                               QString archivePath, QString passphrase)
    : files_(std::move(files)), archivePath_(std::move(archivePath)), passphrase_(std::move(passphrase)) {
    // Descriptor order is the transfer plan order; indexes are never renumbered
    // when a directory has no FileContents stream.
    setData(TransferMime, "1");
    QByteArray preferred(4, '\0');
    preferred[0] = char(1); // DROPEFFECT_COPY
    setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", preferred);
    QStringList paths;
    std::vector<arky::ArchiveEntryInfo> metadata;
    for (const auto& entry : files_) {
        paths << QString::fromUtf8(entry.shellPath.c_str());
        metadata.push_back(entry.entry);
    }
    const auto json = MimeBridge::jsonFor(metadata, archivePath_).toUtf8();
    setData("application/vnd.arky.entries+json", json);
    setData("application/vnd.streamark.entries+json", json); // Compatibility alias
    setData("application/json", json);
    setText(paths.join('\n'));
}

WindowsVirtualMimeConverter::WindowsVirtualMimeConverter()
    // CFSTR_FILEDESCRIPTORW / CFSTR_FILECONTENTS are narrow, ASCII Win32
    // clipboard-format names (the W in FILEDESCRIPTORW refers to the data
    // structure, not the name string passed to RegisterClipboardFormat).
    : descriptorFormat_(RegisterClipboardFormatA(CFSTR_FILEDESCRIPTORW)),
      contentsFormat_(RegisterClipboardFormatA(CFSTR_FILECONTENTS)) {}

bool WindowsVirtualMimeConverter::canConvertFromMime(const FORMATETC& format, const QMimeData* mime) const {
    const auto* virtualData = dynamic_cast<const WindowsVirtualMimeData*>(mime);
    if (!virtualData) return false;
    if (format.cfFormat == descriptorFormat_) return (format.tymed & TYMED_HGLOBAL) != 0;
    return format.cfFormat == contentsFormat_ && (format.tymed & TYMED_ISTREAM) != 0
        && format.lindex >= 0 && static_cast<std::size_t>(format.lindex) < virtualData->entries().size()
        && virtualData->entries()[static_cast<std::size_t>(format.lindex)].entry.type == arky::EntryType::Regular;
}

QList<FORMATETC> WindowsVirtualMimeConverter::formatsForMime(const QString& mimeType, const QMimeData* mime) const {
    const auto* files = dynamic_cast<const WindowsVirtualMimeData*>(mime);
    if (mimeType != QLatin1String(TransferMime) || !files || files->entries().empty()) return {};
    QList<FORMATETC> formats;
    formats.append(FORMATETC{static_cast<CLIPFORMAT>(descriptorFormat_), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
    for (std::size_t i = 0; i < files->entries().size(); ++i)
        if (files->entries()[i].entry.type == arky::EntryType::Regular)
            formats.append(FORMATETC{static_cast<CLIPFORMAT>(contentsFormat_), nullptr, DVASPECT_CONTENT, static_cast<LONG>(i), TYMED_ISTREAM});
    return formats;
}

bool WindowsVirtualMimeConverter::convertFromMime(const FORMATETC& format, const QMimeData* mime,
                                                   STGMEDIUM* medium) const {
    if (!medium || !canConvertFromMime(format, mime)) return false;
    const auto& files = *static_cast<const WindowsVirtualMimeData*>(mime);
    std::memset(medium, 0, sizeof(*medium));
    if (format.cfFormat == contentsFormat_) {
        const auto& entry = files.entries().at(static_cast<std::size_t>(format.lindex));
        medium->tymed = TYMED_ISTREAM;
        const auto p = files.passphrase().toUtf8();
        medium->pstm = new ArchiveEntryStream(files.archivePath(), entry.entry.path,
            {p.constData(), static_cast<std::size_t>(p.size())}, entry.entry.size);
        return true;
    }
    if (format.cfFormat != descriptorFormat_) return false;
    const auto& entries = files.entries();
    if (entries.empty() || entries.size() > 2048) return false;
    const auto bytes = offsetof(FILEGROUPDESCRIPTORW, fgd) + entries.size() * sizeof(FILEDESCRIPTORW);
    auto* global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!global) return false;
    auto* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(global));
    if (!group) { GlobalFree(global); return false; }
    group->cItems = static_cast<UINT>(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i].entry;
        auto& descriptor = group->fgd[i];
        descriptor.dwFlags = FD_ATTRIBUTES;
        descriptor.dwFileAttributes = entry.type == arky::EntryType::Directory
            ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        // FILETIME is 100ns intervals since 1601-01-01 (UTC).
        if (entry.modified > 0) {
            constexpr std::uint64_t UnixEpochOffset = 116444736000000000ULL;
            const auto ticks = static_cast<std::uint64_t>(entry.modified) * 10000000ULL + UnixEpochOffset;
            descriptor.dwFlags |= FD_WRITESTIME;
            descriptor.ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
            descriptor.ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        }
        if (entry.type == arky::EntryType::Regular && entry.sizeKnown) {
            descriptor.dwFlags |= FD_FILESIZE;
            descriptor.nFileSizeHigh = static_cast<DWORD>(entry.size >> 32);
            descriptor.nFileSizeLow = static_cast<DWORD>(entry.size & 0xFFFFFFFFu);
        }
        // FileGroupDescriptorW supports relative paths; backslashes preserve
        // a selected folder's hierarchy without staging any file content.
        const auto wide = QString::fromUtf8(entries[i].shellPath.c_str()).toStdWString();
        if (wide.size() >= MAX_PATH) { GlobalUnlock(global); GlobalFree(global); return false; }
        std::copy(wide.begin(), wide.end(), descriptor.cFileName);
        descriptor.cFileName[wide.size()] = L'\0';
    }
    GlobalUnlock(global);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    return true;
}
#endif
