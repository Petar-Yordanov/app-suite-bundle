#pragma once

#ifdef _WIN32
#include "windows_transfer_plan.hpp"
#include <QMimeData>
#include <QString>
#include <QWindowsMimeConverter>
#include <vector>

// Windows-only Shell virtual-file transfer. Unlike CF_HDROP, FileContents can
// be supplied as an IStream, so native Explorer drag-out does not require
// pre-extracting the entire file into RAM or onto disk.
class WindowsVirtualMimeData final : public QMimeData {
public:
    WindowsVirtualMimeData(std::vector<arky::WindowsTransferItem> files,
                           QString archivePath, QString passphrase);
    const std::vector<arky::WindowsTransferItem>& entries() const { return files_; }
    const QString& archivePath() const { return archivePath_; }
    const QString& passphrase() const { return passphrase_; }
private:
    std::vector<arky::WindowsTransferItem> files_;
    QString archivePath_;
    QString passphrase_;
};

class WindowsVirtualMimeConverter final : public QWindowsMimeConverter {
public:
    WindowsVirtualMimeConverter();
    bool canConvertFromMime(const FORMATETC& format, const QMimeData* mime) const override;
    bool canConvertToMime(const QString&, IDataObject*) const override { return false; }
    bool convertFromMime(const FORMATETC& format, const QMimeData* mime, STGMEDIUM* medium) const override;
    QVariant convertToMime(const QString&, IDataObject*, QMetaType) const override { return {}; }
    QList<FORMATETC> formatsForMime(const QString& mimeType, const QMimeData* mime) const override;
    QString mimeForFormat(const FORMATETC&) const override { return {}; }
private:
    // RegisterClipboardFormatA returns UINT. Store the registered format ID
    // without requiring a separate CLIPFORMAT typedef from Win32 headers.
    unsigned int descriptorFormat_;
    unsigned int contentsFormat_;
};
#endif
