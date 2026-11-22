#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QSettings>

namespace media {
QString localVideo(const QUrl&);
QString localSubtitle(const QUrl&);
bool videoCandidate(const QString&);
QStringList videoExtensions();
QStringList neighbors(const QString& current);
QString videoFilter();
class ResumeStore {
public:
    explicit ResumeStore(QSettings& s):settings_(s){}
    qint64 load(const QString& path,qint64 duration) const;
    void save(const QString& path,qint64 time,qint64 duration);
    void forget(const QString& path);
private:
    QSettings& settings_;
    QString key(const QString&) const;
};
}
