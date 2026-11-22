#include "../Core/Ffmpeg.hpp"
#include "../Core/Feed.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <iostream>
#include <stdexcept>

using namespace rec;
namespace {
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
QByteArray run(const QString& exe,const QStringList& args,const QString& dir,int limit=30000){
    QProcess proc;proc.setWorkingDirectory(dir);proc.start(exe,args);
    if(!proc.waitForStarted(5000)||!proc.waitForFinished(limit)){
        proc.kill();proc.waitForFinished(1000);throw std::runtime_error("FFmpeg test timed out");
    }
    if(proc.exitCode()!=0||proc.exitStatus()!=QProcess::NormalExit){
        std::cerr<<proc.readAllStandardError().constData()<<'\n';throw std::runtime_error("FFmpeg test failed");
    }
    return proc.readAllStandardOutput();
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        auto exe=ffmpegPath();check(!exe.isEmpty(),"FFmpeg unavailable");
        QTemporaryDir temp;check(temp.isValid(),"temporary directory");
        Config cfg;cfg.fps=24;cfg.bitrate=1500;cfg.quality=0;Size size{320,240};
        Encoder enc{"libx264",{}};
        run(exe,probeArgs(enc,cfg,size),temp.path());
        auto video=QStringList{"-f","lavfi","-i","testsrc2=size=320x240:rate=24"};
        QList<QStringList> audio{
            {"-f","lavfi","-i","sine=frequency=440:sample_rate=48000"},
            {"-f","lavfi","-i","sine=frequency=880:sample_rate=48000"}
        };
        QFile list(temp.filePath("segments.txt"));
        check(list.open(QIODevice::WriteOnly),"concat manifest");
        for(int i=0;i<2;++i){
            auto name=QString("part-%1.mp4").arg(i);
            auto args=recordArgs(video,audio,enc,cfg,size,name);
            args.insert(args.size()-1,"-t");args.insert(args.size()-1,"1");
            auto progress=run(exe,args,temp.path());check(progress.contains("progress=end"),"final progress");
            check(QFileInfo(temp.filePath(name)).size()>1024,"nonempty segment");
            list.write(("file '"+name+"'\n").toUtf8());
        }
        list.close();
        run(exe,concatArgs("joined.mp4"),temp.path());
        run(exe,{"-v","error","-i","joined.mp4","-map","0:v:0","-map","0:a:0","-f","null","-"},temp.path());
        check(QFileInfo(temp.filePath("joined.mp4")).size()>1024,"joined recording");
        // Real FFmpeg no-audio MP4 path.
        auto silent=recordArgs(video,{},enc,cfg,size,"silent.mp4");
        silent.insert(silent.size()-1,"-t");silent.insert(silent.size()-1,"0.25");
        run(exe,silent,temp.path());
        run(exe,{"-v","error","-i","silent.mp4","-map","0:v:0","-f","null","-"},temp.path());
        // Bound native PCM and validate zero fill for silent capture.
        Mailbox mailbox;QByteArray samples(48000*8,'a');mailbox.audio(samples.constData(),samples.size());
        auto retained=mailbox.samples(samples.size());check(retained.size()==samples.size(),"fixed sample output");
        check(retained[48000*4-1]=='a'&&retained[48000*4]=='\0',"bounded PCM");
        std::cout<<"PASS: encoder probe, real H.264/AAC recording, dual-source mixing, pause segments, remux, decoding, silent recording and PCM bound\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
