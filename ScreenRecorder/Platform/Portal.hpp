#pragma once
#include "../Core/Feed.hpp"
#include "../Core/RecordingPlan.hpp"
#include <QImage>
#include <QPointer>
#include <QRect>
#include <thread>

class PortalRequest;
// A single compositor-authorized PipeWire session. Shared with Recorder across pauses.
class PortalSession : public QObject {
    Q_OBJECT
public:
    PortalSession();
    ~PortalSession() override;
    void choose(bool window,bool cursor);
    void record(std::shared_ptr<rec::Mailbox>,QRect region,rec::Size output);
    void pause();
    QImage preview()const{return preview_;}
    QSize size()const{return preview_.size();}
    void discardPreview(){preview_={};}
signals:
    void ready();
    void failed(QString);
private slots:
    void sessionClosed();
private:
    void select(bool window,bool cursor);
    void launch();
    void remote(uint node);
    void consume(std::stop_token,int fd,uint node);
    void notifyFailure(QString);
    QPointer<PortalRequest> request_;
    QString session_;
    QImage preview_;
    std::jthread worker_;
    QMutex mutex_;
    std::shared_ptr<rec::Mailbox> mailbox_;
    QRect region_;
    rec::Size output_;
};
