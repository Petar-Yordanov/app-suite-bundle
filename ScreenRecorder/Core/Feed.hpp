#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <memory>

namespace rec {
// Capture callback mailbox: exactly one latest video frame and at most 0.5s PCM.
// The network writer never blocks the GUI and does not queue unlimited data.
class Mailbox {
    QMutex mutex_;
    QByteArray video_,audio_;
    QString error_;
public:
    void video(QByteArray);
    void audio(const char*,qsizetype);
    void fail(QString);
    QByteArray frame();
    QByteArray samples(qsizetype);
    QString error();
};
class Feed : public QObject {
    Q_OBJECT
public:
    Feed(std::shared_ptr<Mailbox>,bool video,int fps,QObject* parent=nullptr);
    bool open();
    QString url() const;
    void authorize(qint64 pid){authorizedPid_=pid;}
signals:
    void fault(QString);
private:
    void pump();
#ifdef Q_OS_WIN
    QTcpServer server_;
    QTcpSocket* socket_=nullptr;
#else
    QTemporaryDir localDir_;
    QLocalServer server_;
    QLocalSocket* socket_=nullptr;
#endif
    QTimer timer_;
    QElapsedTimer clock_;
    std::shared_ptr<Mailbox> mailbox_;
    bool video_;
    int fps_;
    qint64 sentSamples_=0;
    qint64 authorizedPid_=0;
};
}
