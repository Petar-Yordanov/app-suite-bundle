#pragma once
#include "../Core/Emulator.hpp"
#include "../Core/Pty.hpp"
#include "Shells.hpp"
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <condition_variable>
#include <mutex>
#include <memory>

class Session:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString shellName READ shellName CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY screenChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(int offset READ offset WRITE setOffset NOTIFY screenChanged)
    Q_PROPERTY(int history READ history NOTIFY screenChanged)
    Q_PROPERTY(int rows READ rows NOTIFY screenChanged)
public:
    Session(Shell,QString cwd,QObject* parent=nullptr);
    ~Session()override;
    QString shellId()const{return shell_.id;}QString shellName()const{return shell_.name;}
    QString title()const;
    QString status()const{return status_;}
    bool running()const{return running_;}
    QString cwd()const;
    int offset()const{return offset_;}
    int history()const{return int(emu_.historySize());}
    int rows()const{return emu_.rows();}
    term::Emulator& emulator(){return emu_;}const term::Emulator& emulator()const{return emu_;}
    int top()const{return qMax(0,history()-offset_);}
    void resize(int rows,int cols);
    void apply(const QVariantMap&);
    void setBudget(size_t bytes){emu_.setHistoryByteLimit(bytes);offset_=qMin(offset_,history());frame();}
    void sendText(const QString&);
    void key(VTermKey,VTermModifier);
    void character(uint32_t,VTermModifier);
    void inputFocus(bool);
    Q_INVOKABLE void setOffset(int);
    Q_INVOKABLE void scroll(int delta){setOffset(offset_+delta);}
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void restart();
    Q_INVOKABLE bool find(QString query,bool backwards);
    Q_INVOKABLE void clearFind();
    int matchRow()const{return matchRow_;}int matchStart()const{return matchStart_;}int matchEnd()const{return matchEnd_;}
    static QString cellText(const VTermScreenCell&);
    static QString lineText(const term::Line&,QVector<int>* columns=nullptr);
signals:
    void screenChanged();void statusChanged();
private:
    void start();void stop();void enqueue(std::string_view);void drain();void frame();
    void write(std::string_view);
    Shell shell_;QString initialCwd_,status_;bool running_=false;
    std::unique_ptr<term::Pty> pty_;term::Emulator emu_;
    QTimer frameTimer_;
    std::mutex mutex_;std::condition_variable cv_;QByteArray queue_;
    bool pending_=false,closing_=false;int offset_=0;
    int matchRow_=-1,matchStart_=0,matchEnd_=0;QString query_;
    uint32_t fg_=0xd8dee9,bg_=0x171a20;
    quint64 serial_=0; // rejects queued notifications from a stopped process
};
