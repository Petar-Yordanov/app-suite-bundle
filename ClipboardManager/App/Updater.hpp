#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QTimer>
#include <QUrl>
#include <memory>

// Optional, user-configured HTTPS update feed. Never silently executes installers.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
    explicit Updater(QString directory,QObject* parent=nullptr);
    void configure(const QString& url);
    QString status() const { return status_; }
    bool ready() const { return !package_.isEmpty(); }
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
signals:
    void changed();
private:
    void download(QUrl url,QByteArray digest,QString name);
    void setStatus(QString status);
    QString directory_,status_,package_;
    QUrl feed_;
    QNetworkAccessManager network_;
    QTimer timer_;
    QPointer<QNetworkReply> reply_;
};
