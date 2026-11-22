#include "archive_model.hpp"
#include "mime_bridge.hpp"
#include "export_store.hpp"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#ifdef Q_OS_WIN
#include "windows_virtual_mime.hpp"
#include <Windows.h>
#include <ShlObj.h>
#include <ObjIdl.h>
#endif

#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>

static void ensure(bool v, const char* message) {
    if (!v) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        std::vector<arky::ArchiveEntryInfo> entries{
            {.path = "folder/readme.txt", .size = 13, .sizeKnown = true, .type = arky::EntryType::Regular}
        };
        std::unique_ptr<QMimeData> virtualData(MimeBridge::make(entries, "C:/archives/demo.zip"));
        ensure(virtualData->hasFormat("application/vnd.arky.entries+json"), "custom MIME missing");
        ensure(virtualData->hasFormat("application/vnd.streamark.entries+json"), "legacy MIME alias missing");
        ensure(virtualData->hasFormat("application/json"), "JSON MIME missing");
        ensure(virtualData->hasText() && virtualData->hasHtml(), "text and HTML missing");
        ensure(!virtualData->hasUrls(), "virtual files must not advertise fake URLs");
        auto data = QJsonDocument::fromJson(virtualData->data("application/vnd.arky.entries+json"));
        ensure(data.isObject() && data.object().contains("entries"), "invalid metadata JSON");
        QTemporaryDir tmp;
        ensure(tmp.isValid(), "temp directory unavailable");
        QFile file(tmp.filePath("hello.txt"));
        ensure(file.open(QFile::WriteOnly), "cannot write fixture");
        file.write("hello");
        file.close();
        std::unique_ptr<QMimeData> realData(MimeBridge::make(entries, "C:/archives/demo.zip",
            {QUrl::fromLocalFile(file.fileName())}));
        ensure(realData->hasUrls(), "staged files should advertise real URLs");
        ensure(realData->hasFormat("text/uri-list"), "text/uri-list required for shell interoperability");
        ensure(MimeBridge::localUrls(realData.get()).size() == 1, "local file URL missing");
#ifdef Q_OS_WIN
        const auto preferred = realData->data("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
        ensure(preferred.size() == 4 && preferred.at(0) == char(1), "Windows shell copy effect must be advertised");
#endif
        const auto firstExport = ExportStore::create();
        ensure(!firstExport.isEmpty() && QFileInfo(firstExport).isDir(), "Cannot create managed export directory");
        const auto secondExport = ExportStore::create();
        ensure(firstExport != secondExport, "Export directories must be unique");
        QFile persisted(QDir(firstExport).filePath("persist.txt"));
        ensure(persisted.open(QIODevice::WriteOnly), "Cannot write persistent export fixture");
        persisted.write("export payload");
        persisted.close();
        ensure(QFile::exists(persisted.fileName()), "Persistent export was removed");
        ExportStore::discard(secondExport);
        ensure(!QDir(secondExport).exists(), "Explicit export disposal failed");
        ExportStore::discard(firstExport);
        ensure(!QDir(firstExport).exists(), "Persistent export cleanup failed");
#ifdef Q_OS_WIN
        // Integration test for Windows FILEDESCRIPTORW / FILECONTENTS streams.
        const auto zipPath = tmp.filePath("native-virtual.zip");
        const auto createResult = arky::ArchiveEngine::create(
            std::filesystem::path(zipPath.toStdWString()), {std::filesystem::path(file.fileName().toStdWString())});
        ensure(createResult.ok, "Unable to create Windows virtual-file fixture ZIP");
        auto virtualEntry = entries.front();
        virtualEntry.path = "hello.txt";
        virtualEntry.size = 5;
        WindowsVirtualMimeConverter converter;
        WindowsVirtualMimeData virtualFile(arky::planWindowsTransfer({virtualEntry}, {virtualEntry}), zipPath, {});
        auto formats = converter.formatsForMime("application/vnd.arky.virtualfiles", &virtualFile);
        ensure(formats.size() == 2, "Windows descriptor and stream formats must be registered");
        ensure(formats.at(0).cfFormat != 0 && formats.at(1).cfFormat != 0
               && formats.at(0).cfFormat != formats.at(1).cfFormat,
               "Native Windows clipboard format registration failed");
        STGMEDIUM descriptor{};
        ensure(converter.convertFromMime(formats.at(0), &virtualFile, &descriptor), "Cannot prepare Shell file descriptor");
        auto* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(descriptor.hGlobal));
        ensure(group && group->cItems == 1, "Shell file descriptor has wrong count");
        ensure(QString::fromWCharArray(group->fgd[0].cFileName) == "hello.txt", "Wrong virtual file name");
        GlobalUnlock(descriptor.hGlobal);
        ReleaseStgMedium(&descriptor);
        STGMEDIUM contents{};
        ensure(converter.convertFromMime(formats.at(1), &virtualFile, &contents), "Cannot create Shell virtual-file stream");
        char bytes[16]{};
        ULONG actual = 0;
        ensure(SUCCEEDED(contents.pstm->Read(bytes, 5, &actual)) && actual == 5, "Virtual file stream is not readable");
        ensure(std::string(bytes, bytes + 5) == "hello", "Virtual stream returned wrong content");
        LARGE_INTEGER zero{};
        ensure(SUCCEEDED(contents.pstm->Seek(zero, STREAM_SEEK_SET, nullptr)), "Virtual stream cannot rewind");
        ensure(SUCCEEDED(contents.pstm->Read(bytes, 5, &actual)) && actual == 5, "Rewound virtual stream cannot be read");
        ReleaseStgMedium(&contents);

        // Directory hierarchy: descriptor indices include directories, but
        // FileContents streams are only advertised for regular files.
        const auto folderOnDisk = tmp.filePath("tree");
        ensure(QDir().mkpath(QDir(folderOnDisk).filePath("empty")), "Cannot create empty directory fixture");
        ensure(QDir().mkpath(QDir(folderOnDisk).filePath("nested")), "Cannot create nested directory fixture");
        QFile nestedFile(QDir(folderOnDisk).filePath("nested/payload.bin"));
        ensure(nestedFile.open(QIODevice::WriteOnly), "Cannot create nested file");
        ensure(nestedFile.write("streamed", 8) == 8, "Nested file write failed");
        nestedFile.close();
        const auto treeZipPath = tmp.filePath("native-directory.zip");
        const auto treeResult = arky::ArchiveEngine::create(
            std::filesystem::path(treeZipPath.toStdWString()),
            {std::filesystem::path(folderOnDisk.toStdWString())});
        ensure(treeResult.ok, "Cannot create nested ZIP fixture");
        const auto indexed = arky::ArchiveEngine::inspect(std::filesystem::path(treeZipPath.toStdWString()));
        ensure(indexed.ok, "Cannot index nested ZIP fixture");
        arky::ArchiveEntryInfo selectedFolder;
        selectedFolder.path = "tree";
        selectedFolder.type = arky::EntryType::Directory;
        auto treePlan = arky::planWindowsTransfer({selectedFolder}, indexed.entries);
        ensure(treePlan.size() >= 4, "Nested directory descriptors missing");
        WindowsVirtualMimeData treeMime(std::move(treePlan), treeZipPath, {});
        auto treeFormats = converter.formatsForMime("application/vnd.arky.virtualfiles", &treeMime);
        ensure(treeFormats.size() >= 2, "Nested transfer has no contents");
        STGMEDIUM treeDescriptor{};
        ensure(converter.convertFromMime(treeFormats.first(), &treeMime, &treeDescriptor),
               "Cannot render nested file descriptors");
        auto* treeGroup = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(treeDescriptor.hGlobal));
        ensure(treeGroup && treeGroup->cItems == treeMime.entries().size(),
               "Directory descriptor count mismatch");
        bool sawEmpty = false, sawNestedFile = false;
        LONG nestedIndex = -1;
        for (UINT i = 0; i < treeGroup->cItems; ++i) {
            const auto name = QString::fromWCharArray(treeGroup->fgd[i].cFileName);
            if (name == "tree\\empty") {
                sawEmpty = true;
                ensure(treeGroup->fgd[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY,
                       "Empty folder must advertise a directory attribute");
            }
            if (name == "tree\\nested\\payload.bin") {
                sawNestedFile = true;
                nestedIndex = static_cast<LONG>(i);
                ensure(treeGroup->fgd[i].dwFlags & FD_FILESIZE, "Missing virtual file size");
            }
        }
        ensure(sawEmpty && sawNestedFile, "Tree destinations were not preserved");
        GlobalUnlock(treeDescriptor.hGlobal);
        ReleaseStgMedium(&treeDescriptor);
        const auto fileFormat = std::find_if(treeFormats.begin(), treeFormats.end(),
            [nestedIndex](const FORMATETC& f) { return f.lindex == nestedIndex; });
        ensure(fileFormat != treeFormats.end(), "Missing FileContents index for nested file");
        STGMEDIUM nestedContents{};
        ensure(converter.convertFromMime(*fileFormat, &treeMime, &nestedContents),
               "Unable to produce nested FileContents stream");
        char nestedBytes[16]{};
        ULONG nestedRead = 0;
        ensure(SUCCEEDED(nestedContents.pstm->Read(nestedBytes, 8, &nestedRead)) && nestedRead == 8,
               "Nested virtual file stream failed");
        ensure(std::string(nestedBytes, nestedBytes+8) == "streamed", "Nested FileContents corrupted");
        ReleaseStgMedium(&nestedContents);
        const auto copyPreferred = treeMime.data("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
        ensure(copyPreferred.size() == 4 && copyPreferred[0] == char(1), "Virtual Shell data should request copy");
#endif
        ArchiveModel model;
        model.resetEntries(entries);
        ensure(model.count() == 1, "entry model count");
        model.toggle(0);
        ensure(model.selectedCount() == 1 && model.selectedPaths().size() == 1, "entry selection");
        std::cout << "PASS custom MIME, JSON, plain/HTML, disk-backed uri-list and selection\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
