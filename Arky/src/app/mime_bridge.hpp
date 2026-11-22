#pragma once

#include "archive_engine.hpp"

#include <QList>
#include <QMimeData>
#include <QUrl>

// MIME metadata describes *virtual archive entries*. File URLs only appear after
// real disk-backed extraction into an export directory; no fake/nonexistent URLs.
class MimeBridge final {
public:
    static QMimeData* make(const std::vector<arky::ArchiveEntryInfo>& entries,
                           const QString& archivePath, const QList<QUrl>& actualFiles = {});
    static QString jsonFor(const std::vector<arky::ArchiveEntryInfo>& entries,
                           const QString& archivePath);
    static QList<QUrl> localUrls(const QMimeData* data);
};
