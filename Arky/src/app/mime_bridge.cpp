#include "mime_bridge.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtGlobal>

QString MimeBridge::jsonFor(const std::vector<arky::ArchiveEntryInfo>& entries,
                            const QString& archivePath) {
    QJsonObject root;
    root["schema"] = "https://arky.local/schema/entries-v1";
    root["archive"] = archivePath;
    QJsonArray items;
    for (const auto& e : entries) {
        QJsonObject item;
        item["path"] = QString::fromUtf8(e.path.c_str());
        item["sizeBytes"] = QString::number(e.size); // No JSON integer precision loss
        item["type"] = e.type == arky::EntryType::Directory ? "directory"
                      : e.type == arky::EntryType::Regular ? "file" : "other";
        items.append(item);
    }
    root["entries"] = items;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
QMimeData* MimeBridge::make(const std::vector<arky::ArchiveEntryInfo>& entries,
                             const QString& archivePath, const QList<QUrl>& actualFiles) {
    auto* data = new QMimeData;
    auto json = jsonFor(entries, archivePath).toUtf8();
    data->setData("application/vnd.arky.entries+json", json);
    data->setData("application/vnd.streamark.entries+json", json); // Compatibility with older StreamArk releases
    data->setData("application/json", json);
    QStringList lines;
    QString html = "<ul>";
    for (const auto& e : entries) {
        const auto path = QString::fromUtf8(e.path.c_str());
        lines << path;
        html += "<li>" + path.toHtmlEscaped() + "</li>";
    }
    data->setText(lines.join('\n'));
    data->setHtml(html + "</ul>");
    if (!actualFiles.isEmpty()) {
        // Shell targets expect bona fide on-disk paths. Qt maps URLs to
        // CF_HDROP on Windows, NSURLs on macOS and text/uri-list on X11.
        // Never put fictional archive member URLs into the OS clipboard.
        data->setUrls(actualFiles);
#ifdef Q_OS_WIN
        // The shell must COPY these temporary exports, not MOVE them out of the
        // owned cache. DWORD DROPEFFECT_COPY = 1 (little endian).
        const QByteArray copyEffect(4, '\0');
        QByteArray effect = copyEffect;
        effect[0] = char(1);
        data->setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", effect);
#endif
    }
    return data;
}
QList<QUrl> MimeBridge::localUrls(const QMimeData* data) {
    QList<QUrl> result;
    if (!data || !data->hasUrls()) return result;
    for (const auto& url : data->urls()) {
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) result.push_back(url);
    }
    return result;
}
