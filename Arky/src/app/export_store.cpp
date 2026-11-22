#include "export_store.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {
constexpr auto Prefix = "export-";
constexpr auto Marker = ".arky-export";

QString canonical(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
bool validOwnedExport(const QString& path) {
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink() && info.fileName().startsWith(Prefix)
        && QFileInfo(QDir(path).filePath(Marker)).isFile();
}
}
QString ExportStore::root() {
    const auto base = qEnvironmentVariableIsSet("ARKY_DATA_DIR") ? qEnvironmentVariable("ARKY_DATA_DIR") :
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath("exports");
}
QString ExportStore::create() {
    const auto base = root();
    if (base.isEmpty() || !QDir().mkpath(base)) return {};
    const auto directory = QDir(base).filePath(QString::fromLatin1(Prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkdir(directory)) return {};
    QSaveFile marker(QDir(directory).filePath(Marker));
    if (!marker.open(QIODevice::WriteOnly) || marker.write("Arky export v1\n") < 0 || !marker.commit()) {
        QDir(directory).removeRecursively();
        return {};
    }
    return directory;
}
void ExportStore::discard(const QString& directory) {
    // Only delete a direct, marked child of our own export root.
    if (!validOwnedExport(directory)) return;
    const auto base = canonical(root());
    if (canonical(QFileInfo(directory).absolutePath()) != base) return;
    QDir(directory).removeRecursively();
}
int ExportStore::prune(int olderThanDays) {
    if (olderThanDays < 1) return 0;
    const auto base = root();
    if (base.isEmpty() || !QDir(base).exists()) return 0;
    const auto now = QDateTime::currentDateTimeUtc();
    QStringList clipboardFiles;
    if (auto* app = QGuiApplication::instance()) {
        Q_UNUSED(app);
        if (const auto* mime = QGuiApplication::clipboard()->mimeData(); mime && mime->hasUrls()) {
            for (const auto& url : mime->urls())
                if (url.isLocalFile()) clipboardFiles.push_back(canonical(url.toLocalFile()));
        }
    }
    int removed = 0;
    for (const auto& info : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto directory = info.absoluteFilePath();
        if (!validOwnedExport(directory) || info.lastModified().toUTC().daysTo(now) < olderThanDays) continue;
        const auto prefix = canonical(directory) + "/";
        bool clipboardReferencesExport = false;
        for (const auto& file : clipboardFiles)
            if (file.startsWith(prefix, Qt::CaseInsensitive)) { clipboardReferencesExport = true; break; }
        if (clipboardReferencesExport) continue;
        discard(directory);
        if (!QDir(directory).exists()) ++removed;
    }
    return removed;
}
