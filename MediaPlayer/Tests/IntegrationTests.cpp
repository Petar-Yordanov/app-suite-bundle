#include "../App/MainWindow.hpp"
#include <QApplication>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QFileInfo>
#include <functional>
#include <iostream>
#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#endif

bool waitFor(const std::function<bool()>& predicate,int ms=15000){
    QElapsedTimer clock;clock.start();
    while(clock.elapsed()<ms){QCoreApplication::processEvents();if(predicate())return true;QThread::msleep(15);}
    return predicate();
}
int main(int argc,char** argv){
#ifdef Q_OS_LINUX
    XInitThreads();
#endif
    QApplication app(argc,argv);QQuickStyle::setStyle("Basic");QTemporaryDir storage;
    const auto fixture=app.arguments().value(1,QDir::current().filePath("Tests/fixtures"));
    int failed=0,checks=0;
    auto check=[&](bool ok,const char* label){++checks;if(!ok){++failed;std::cerr<<"FAIL: "<<label<<"\n";}};
    check(storage.isValid(),"temporary profile");
    QDir canonical(QFileInfo(storage.path()).canonicalFilePath());
    const auto a=canonical.filePath("a.mkv"),b=canonical.filePath("b.mkv"),c=canonical.filePath("c.mkv");
    check(QFile::copy(QDir(fixture).filePath("sample.mkv"),a),"copy fixture A");
    check(QFile::copy(a,b)&&QFile::copy(a,c),"copy fixtures B/C");
    check(media::videoCandidate(a),"recognized video type");
    check(!media::localVideo(QUrl("https://example.invalid/video.mp4")).size(),"reject network URL");
    check(media::neighbors(a)==QStringList({a,b,c}),"filename-ordered directory navigation");
    QSettings settings(storage.filePath("resume-test.ini"),QSettings::IniFormat);media::ResumeStore store(settings);
    store.save(a,4000,12000);check(store.load(a,12000)==4000,"persist position");store.forget(a);check(store.load(a,12000)==0,"clear position");
    MainWindow window(storage.path(),true,true);check(window.valid(),"QML controls load");window.show();
    auto& p=window.player();p.setPanel("playlist");check(window.panelValid(),"QML playlist loads");p.setPanel("tracks");QCoreApplication::processEvents();
    p.setPanel("settings");QCoreApplication::processEvents();p.setPanel("");
    QString error;check(p.checkEngine(error),"libVLC initializes");if(!error.isEmpty())std::cerr<<error.toStdString()<<"\n";
    if(p.engineVersion().isEmpty())return 1;
    auto url=[](const QString& path){return QUrl::fromLocalFile(path);};
    p.openUrls({url(a)});
    check(waitFor([&]{return p.playing()&&p.seekable()&&p.stats().value("callbackFrames").toULongLong()>0;}),"video decoding and first callback");
    check(p.duration()>10000,"video duration");
    check(waitFor([&]{return p.audioTracks().size()>=3&&p.subtitleTracks().size()>=2;},5000),"embedded audio and subtitles");
    p.setAudioTrack(-1);check(waitFor([&]{return p.audioTrack()==-1;},3000),"disable audio");
    if(p.audioTracks().size()>1){auto id=p.audioTracks()[1].toMap().value("id").toInt();p.setAudioTrack(id);check(waitFor([&]{return p.audioTrack()==id;},3000),"select audio");}
    const auto before=p.subtitleTracks().size();
    check(p.addSubtitle(url(QDir(fixture).filePath("external.srt"))),"external subtitle accepted");
    check(waitFor([&]{return p.subtitleTracks().size()>before;},4000),"external subtitle listed");
    p.setSubtitleTrack(-1);check(waitFor([&]{return p.subtitleTrack()==-1;},3000),"disable subtitles");
    p.setVolume(36);p.setMuted(true);check(p.volume()==36&&p.muted(),"volume and mute settings");
    p.setAspect("4:3");check(p.aspect()=="4:3","aspect ratio");
    p.togglePlay();check(waitFor([&]{return !p.playing()&&!p.busy();},3000),"pause");
    p.seek(5000);p.togglePlay();
    check(waitFor([&]{return p.playing()&&p.position()>=4000&&p.position()<9000;},4000),"seek and resume");
    p.stop();check(p.position()==0&&!p.playing(),"stop clears timeline");
    p.openUrls({url(a)});check(waitFor([&]{return p.playing()&&p.position()>=3500&&p.position()<10000;},5000),"reopen remembers position");
    p.openUrls({url(a),url(b),url(c)});check(p.playlist().size()==3&&p.playlistIndex()==0,"active playlist");
    p.next();check(p.currentPath()==b,"next playlist item");
    p.moveIndex(1,2);check(p.playlistIndex()==2,"playlist reorder");
    p.previous();check(p.currentPath()==c,"previous honors new order");
    p.removeIndex(0);check(p.playlist().size()==2,"playlist removal");
    p.clearPlaylist();p.previous();check(p.currentPath()==b,"directory previous without playlist");
    p.setShortcut("play","Ctrl+Shift+P");check(p.shortcuts().value("play")=="Ctrl+Shift+P","custom shortcut");
    p.setShortcut("stop","Ctrl+Shift+P");check(p.shortcuts().value("stop")=="S","reject duplicate shortcut");
    p.resetShortcuts();check(p.shortcuts().value("play")=="Space","reset shortcuts");
    p.openUrls({url(QDir(fixture).filePath("audio.wav"))});
    check(waitFor([&]{return p.status().contains("Audio-only")||p.status().contains("No playable video")||p.status().contains("No video track");},20000),"reject audio-only playback");
    std::cout<<checks-failed<<"/"<<checks<<" integration tests passed\n";return failed?1:0;
}
