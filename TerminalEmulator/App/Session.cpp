#include "Session.hpp"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QColor>
#include <QHostInfo>
#include <QMetaObject>
#include <algorithm>

Session::Session(Shell shell,QString cwd,QObject* parent):QObject(parent),shell_(std::move(shell)),
    initialCwd_(std::move(cwd)),emu_(24,80,[this](std::string_view s){write(s);}){
    frameTimer_.setSingleShot(true);frameTimer_.setInterval(16);
    connect(&frameTimer_,&QTimer::timeout,this,&Session::screenChanged);
    start();
}
Session::~Session(){stop();}
QString Session::title()const{
    auto title=QString::fromStdString(emu_.title()).left(160);
    return title.isEmpty()?shell_.name:title;
}
QString Session::cwd()const{
#ifdef Q_OS_WIN
    auto report=QString::fromStdString(emu_.cwdReport());
    if(report.startsWith("file:")){
        auto url=QUrl(report);
        if(!shell_.distribution.isEmpty())report=url.path(QUrl::FullyDecoded);
        else if(url.host().isEmpty()||url.host()=="localhost"||url.host().compare(QHostInfo::localHostName(),Qt::CaseInsensitive)==0){
            report=url.path(QUrl::FullyDecoded);if(report.size()>2&&report[0]=='/'&&report[2]==':')report.remove(0,1);
        }else report.clear();
    }
    if(!shell_.distribution.isEmpty()&&report.startsWith('/'))return report;
    if(!report.isEmpty()&&!report.startsWith("//")&&!report.startsWith("\\\\")&&QFileInfo(report).isDir())return QDir::cleanPath(report);
#else
    if(pty_){auto path=QString::fromStdString(pty_->cwd());if(QFileInfo(path).isDir())return path;}
#endif
    return initialCwd_.isEmpty()?QDir::homePath():initialCwd_;
}
void Session::start(){
    auto generation=++serial_;
    {std::lock_guard lock(mutex_);closing_=false;pending_=false;queue_.clear();}
    auto launch=launchFor(shell_,initialCwd_,emu_.rows(),emu_.cols());
    std::string error;
    pty_=term::Pty::start(launch,[this](auto bytes){enqueue(bytes);},[this,generation](int exitCode){
        QMetaObject::invokeMethod(this,[this,exitCode,generation]{
            if(generation!=serial_)return;
            running_=false;status_=QString("Exited (%1). Restart to open a new shell.").arg(exitCode);emit statusChanged();
        },Qt::QueuedConnection);
    },error);
    running_=bool(pty_);status_=running_?QString("Running"):QString::fromStdString(error);
    if(!running_)emu_.feed("\r\n[Could not open shell: "+error+"]\r\n");
    emit statusChanged();frame();
}
void Session::stop(){
    ++serial_;{std::lock_guard lock(mutex_);closing_=true;queue_.clear();pending_=false;}cv_.notify_all();
    if(pty_){pty_->stop();pty_.reset();}running_=false;
}
void Session::restart(){
    initialCwd_=cwd();stop();offset_=0;clearFind();emu_.feed("\x1b[?1049l\x1b" "c");start();
}
void Session::enqueue(std::string_view bytes){
    std::unique_lock lock(mutex_);
    cv_.wait(lock,[&]{return closing_||size_t(queue_.size())+bytes.size()<=term::QueueLimit;});
    if(closing_)return;queue_.append(bytes.data(),qsizetype(bytes.size()));
    if(!pending_){pending_=true;QMetaObject::invokeMethod(this,[this]{drain();},Qt::QueuedConnection);}
}
void Session::drain(){
    QByteArray chunk;bool more;
    {
        std::lock_guard lock(mutex_);if(closing_){pending_=false;return;}
        auto count=qMin<qsizetype>(queue_.size(),65536);chunk=queue_.left(count);queue_.remove(0,count);
        more=!queue_.isEmpty();pending_=more;
    }
    cv_.notify_all();
    if(!chunk.isEmpty()){
        auto before=emu_.historySerial();bool alternate=emu_.alternate();
        emu_.feed({chunk.constData(),size_t(chunk.size())});
        auto pushes=emu_.historySerial()-before;
        if(alternate!=emu_.alternate())offset_=0;
        else if(offset_>0)offset_=qMin(history(),offset_+int(qMin<uint64_t>(pushes,100000)));
        if(pushes||alternate!=emu_.alternate())matchRow_=-1;
        frame();
    }
    if(more)QMetaObject::invokeMethod(this,[this]{drain();},Qt::QueuedConnection);
}
void Session::frame(){if(!frameTimer_.isActive())frameTimer_.start();}
void Session::write(std::string_view data){
    if(pty_&&running_&&!pty_->write(data)){
        status_="Input queue full; wait for the shell to consume input.";emit statusChanged();
    }
}
void Session::resize(int rows,int cols){
    rows=qBound(2,rows,300);cols=qBound(2,cols,500);
    if(rows==emu_.rows()&&cols==emu_.cols())return;
    emu_.resize(rows,cols);offset_=qMin(offset_,history());matchRow_=-1;
    if(pty_)pty_->resize(rows,cols);frame();
}
void Session::apply(const QVariantMap& options){
    emu_.setHistoryLimit(size_t(qBound(0,options.value("scrollback",10000).toInt(),100000)));
    offset_=qMin(offset_,history());
    auto newFg=QColor(options.value("foreground","#d8dee9").toString());
    auto newBg=QColor(options.value("background","#171a20").toString());
    if(newFg.isValid())fg_=newFg.rgb()&0xffffff;if(newBg.isValid())bg_=newBg.rgb()&0xffffff;
    static const uint32_t colors[]={0x22262e,0xe06c75,0x98c379,0xe5c07b,0x61afef,0xc678dd,0x56b6c2,0xd8dee9,
        0x68707f,0xff7a85,0xb5e890,0xffdb96,0x80c6ff,0xe5a0f4,0x79dbe3,0xffffff};
    emu_.palette(colors,fg_,bg_);frame();
}
void Session::setOffset(int n){
    auto clamped=qBound(0,n,history());if(clamped!=offset_){offset_=clamped;emit screenChanged();}
}
void Session::clearHistory(){emu_.clearHistory();offset_=0;clearFind();emit screenChanged();}
void Session::sendText(const QString& text){
    if(!running_)return;
    auto data=text.toUtf8();
    if(data.size()>512*1024){status_="Paste exceeds the 512 KiB per-paste limit.";emit statusChanged();return;}
    offset_=0;emu_.paste({data.constData(),size_t(data.size())});frame();
}
void Session::key(VTermKey key,VTermModifier mod){if(running_){offset_=0;emu_.key(key,mod);frame();}}
void Session::character(uint32_t cp,VTermModifier mod){if(running_){offset_=0;emu_.character(cp,mod);frame();}}
void Session::inputFocus(bool focus){if(running_)emu_.focus(focus);}
QString Session::cellText(const VTermScreenCell& c){
    if(term::Emulator::continuation(c))return {};
    if(!c.chars[0])return " ";
    QString text;
    for(uint32_t cp:c.chars){
        if(!cp||cp==UINT32_MAX)break;
        char32_t scalar=char32_t(cp);text+=QString::fromUcs4(&scalar,1);
    }
    return text;
}
QString Session::lineText(const term::Line& row,QVector<int>* positions){
    QString text;
    for(int i=0;i<int(row.size());++i){
        auto cell=cellText(row[i].value);
        if(positions)for(int j=0;j<cell.size();++j)positions->append(i);
        text+=cell;
    }
    return text;
}
bool Session::find(QString q,bool backwards){
    if(q.isEmpty()){clearFind();return false;}
    auto total=history()+emu_.rows();if(!total)return false;
    bool same=q==query_;query_=q;
    auto row=same&&matchRow_>=0?matchRow_:(backwards?total-1:top());
    for(int n=0;n<=total;++n){
        int r=(row+(backwards?-n:n)+total*2)%total;
        QVector<int> positions;auto text=lineText(emu_.line(size_t(r)),&positions);
        int from=backwards?text.size()-1:0;
        if(n==0&&same&&matchRow_>=0){
            int old=positions.indexOf(matchStart_);
            from=old<0?from:old+(backwards?-1:1);
            if(backwards&&from<0)continue;
        }
        int i=backwards?text.lastIndexOf(q,from,Qt::CaseInsensitive):text.indexOf(q,from,Qt::CaseInsensitive);
        if(i>=0&&i+q.size()-1<positions.size()){
            matchRow_=r;matchStart_=positions[i];matchEnd_=positions[i+q.size()-1]+1;
            if(r<top()||r>=top()+emu_.rows())offset_=qBound(0,history()-r+emu_.rows()/2,history());
            emit screenChanged();return true;
        }
    }
    matchRow_=-1;emit screenChanged();return false;
}
void Session::clearFind(){matchRow_=-1;query_.clear();emit screenChanged();}
