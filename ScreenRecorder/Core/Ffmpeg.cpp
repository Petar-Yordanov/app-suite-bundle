#include "Ffmpeg.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace rec {
namespace {
QString n(int value){return QString::number(value);}
QString size(Size s){return n(s.w)+"x"+n(s.h);}
QStringList inputPrefix(int queue,bool audio=false){
    QStringList args{"-thread_queue_size",n(queue),"-use_wallclock_as_timestamps","1"};
    if(audio)args<<"-isync"<<"0";
    return args;
}
QStringList rawAudio(const QString& url){
    auto args=inputPrefix(32,true);
    args<<"-probesize"<<"32"<<"-analyzeduration"<<"0"<<"-f"<<"f32le"<<"-ar"<<"48000"<<"-ac"<<"2"<<"-i"<<url;
    return args;
}
QStringList command(const Encoder& e){
    QStringList a{"-hide_banner","-loglevel","warning","-y","-filter_threads","2","-filter_complex_threads","2"};
    if(e.name=="h264_vaapi")a<<"-vaapi_device"<<e.device;
    return a;
}
QByteArray query(const QString& exe,const QStringList& args){
    QProcess p;p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe,args);
    if(!p.waitForStarted(3000))return {};
    if(!p.waitForFinished(8000)){p.kill();p.waitForFinished(1000);}
    auto data=p.readAll();
    return data.left(1024*1024);
}
}
QString ffmpegPath(){
    const auto dir=QDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    auto packaged=dir.filePath("ffmpeg.exe");
#else
    auto packaged=dir.filePath("ffmpeg");
#endif
    if(QFileInfo(packaged).isExecutable())return packaged;
#ifdef Q_OS_WIN
    const auto explicitPath=qEnvironmentVariable("FFMPEG_BINARY");
    if(QFileInfo(explicitPath).isExecutable()&&QFileInfo(explicitPath).size()>1024*1024)return explicitPath;
    const auto chocolatey=qEnvironmentVariable("ChocolateyInstall");
    if(!chocolatey.isEmpty()){
        QDirIterator it(QDir(chocolatey).filePath("lib/ffmpeg/tools"),{"ffmpeg.exe"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()){auto path=it.next();if(QFileInfo(path).size()>1024*1024)return path;}
    }
#endif
#ifdef Q_OS_MACOS
    for(const auto& path:{QString("/opt/homebrew/bin/ffmpeg"),QString("/usr/local/bin/ffmpeg")})
        if(QFileInfo(path).isExecutable())return path;
#endif
    return QStandardPaths::findExecutable("ffmpeg");
}
Devices discoverDevices(const QString& ffmpeg){
    Devices result;
    if(ffmpeg.isEmpty()){result.error="FFmpeg was not found.";return result;}
#ifdef Q_OS_WIN
    const auto output=QString::fromUtf8(query(ffmpeg,{"-hide_banner","-list_devices","true","-f","dshow","-i","dummy"}));
    QRegularExpression re(R"re("([^"]+)"\s*\(audio\))re");
    auto matches=re.globalMatch(output);
    while(matches.hasNext()){auto match=matches.next();result.microphones.append(AudioDevice{match.captured(1),match.captured(1)});}
    result.systemSource="WASAPI default playback device";
#elif defined(Q_OS_MACOS)
    const auto output=QString::fromUtf8(query(ffmpeg,{"-hide_banner","-f","avfoundation","-list_devices","true","-i",""}));
    bool audio=false;
    QRegularExpression re(R"(\[(\d+)\]\s+(.+)$)");
    for(const auto& line:output.split('\n')){
        if(line.contains("AVFoundation audio devices")){audio=true;continue;}
        if(!audio)continue;
        auto match=re.match(line);
        if(match.hasMatch())result.microphones.append(AudioDevice{match.captured(1),match.captured(2).trimmed()});
    }
    result.systemSource="ScreenCaptureKit";
#else
    auto pactl=QStandardPaths::findExecutable("pactl");
    if(!pactl.isEmpty()){
        const auto output=QString::fromUtf8(query(pactl,{"list","short","sources"}));
        for(const auto& line:output.split('\n')){
            auto fields=line.split('\t');
            if(fields.size()<2||fields[1].endsWith(".monitor"))continue;
            if(fields[0].toInt()>0||fields[0]=="0")result.microphones.append(AudioDevice{fields[1],fields[1]});
        }
        const auto sink=QString::fromUtf8(query(pactl,{"get-default-sink"})).trimmed();
        if(!sink.isEmpty()&&!sink.contains('\n')&&!sink.contains(' '))result.systemSource=sink+".monitor";
    }
    if(result.microphones.isEmpty())result.microphones.append(AudioDevice{"default","Default PulseAudio microphone"});
#endif
    return result;
}
QList<Encoder> encoders(bool hardware){
    QList<Encoder> result;
    if(hardware){
#ifdef Q_OS_MACOS
        result.append(Encoder{"h264_videotoolbox",{}});
#elif defined(Q_OS_WIN)
        for(const auto* name:{"h264_nvenc","h264_qsv","h264_amf","h264_mf"})result.append(Encoder{name,{}});
#else
        result.append(Encoder{"h264_nvenc",{}});
        for(const auto& device:QDir("/dev/dri").entryList({"renderD*"},QDir::System|QDir::Files))
            result.append(Encoder{"h264_vaapi","/dev/dri/"+device});
        result.append(Encoder{"h264_qsv",{}});
#endif
    }
    result.append(Encoder{"libx264",{}});
    return result;
}
QStringList encoderArgs(const Encoder& e,const Config& cfg,Size out,bool directDxgi){
    QStringList args;
    const auto filter="scale="+n(out.w)+":"+n(out.h)+":flags=fast_bilinear,setsar=1";
    if(!directDxgi)args<<"-vf"<<(e.name=="h264_vaapi"?filter+",format=nv12,hwupload":filter);
    args<<"-c:v"<<e.name<<"-b:v"<<n(cfg.bitrate)+"k"<<"-maxrate"<<n(cfg.bitrate*2)+"k"
        <<"-bufsize"<<n(cfg.bitrate*2)+"k"<<"-g"<<n(cfg.fps*2)<<"-bf"<<"0"
        <<"-r"<<n(cfg.fps)<<"-fps_mode"<<"cfr";
    if(!directDxgi&&e.name!="h264_vaapi")args<<"-pix_fmt"<<(e.name=="h264_qsv"||e.name=="h264_mf"?"nv12":"yuv420p");
    if(e.name=="libx264")args<<"-preset"<<QStringList{"ultrafast","veryfast","fast"}.at(cfg.quality)<<"-threads"<<"4";
    if(e.name=="h264_nvenc")args<<"-preset"<<QStringList{"p1","p4","p6"}.at(cfg.quality);
    if(e.name=="h264_amf")args<<"-quality"<<QStringList{"speed","balanced","quality"}.at(cfg.quality);
    if(e.name=="h264_qsv")args<<"-preset"<<QStringList{"veryfast","medium","slow"}.at(cfg.quality);
    if(e.name=="h264_vaapi")args<<"-quality"<<QStringList{"7","4","1"}.at(cfg.quality);
    if(e.name=="h264_videotoolbox")args<<"-realtime"<<"1"<<"-allow_sw"<<"0";
    if(e.name=="h264_mf")args<<"-hw_encoding"<<"1";
    return args;
}
QStringList probeArgs(const Encoder& e,const Config& cfg,Size out){
    auto a=command(e);
    a<<"-f"<<"lavfi"<<"-i"<<"color=c=black:s="+size(out)+":r="+n(cfg.fps)
      <<encoderArgs(e,cfg,out)<<"-frames:v"<<"3"<<"-an"<<"-f"<<"null"<<"-";
    return a;
}
QStringList videoInput(const Target& target,QRect region,const Config& cfg,Size out,const QString& nativeUrl,bool directDxgi){
    auto a=inputPrefix(4);
    if(!nativeUrl.isEmpty()){
        a<<"-probesize"<<"32"<<"-analyzeduration"<<"0"<<"-f"<<"rawvideo"<<"-pixel_format"<<"bgra"
          <<"-video_size"<<size(out)<<"-framerate"<<n(cfg.fps)<<"-i"<<nativeUrl;
        return a;
    }
    const auto bounds=region.isEmpty()?target.native:QRect(target.native.topLeft()+region.topLeft(),region.size());
#ifdef Q_OS_WIN
    if(directDxgi){
        auto crop=region.isEmpty()?QRect(QPoint(),target.native.size()):region;
        a<<"-f"<<"lavfi"<<"-i"<<QString("ddagrab=output_idx=%1:framerate=%2:draw_mouse=%3:video_size=%4:offset_x=%5:offset_y=%6")
            .arg(target.hardwareIndex).arg(cfg.fps).arg(cfg.cursor?1:0).arg(size(out)).arg(crop.x()).arg(crop.y());
        return a;
    }
    a<<"-f"<<"gdigrab"<<"-framerate"<<n(cfg.fps)<<"-draw_mouse"<<n(cfg.cursor);
    if(target.window)a<<"-i"<<QString("hwnd=0x%1").arg(target.id,0,16);
    else a<<"-offset_x"<<n(bounds.x())<<"-offset_y"<<n(bounds.y())<<"-video_size"<<size({bounds.width(),bounds.height()})<<"-i"<<"desktop";
#elif defined(Q_OS_MACOS)
    // All macOS streams use ScreenCaptureKit through the local raw video transport.
    return {};
#else
    a<<"-f"<<"x11grab"<<"-framerate"<<n(cfg.fps)<<"-draw_mouse"<<n(cfg.cursor);
    auto display=qEnvironmentVariable("DISPLAY");
    if(target.window)a<<"-window_id"<<QString("0x%1").arg(target.id,0,16)<<"-i"<<display;
    else a<<"-video_size"<<size({bounds.width(),bounds.height()})<<"-i"<<QString("%1+%2,%3").arg(display).arg(bounds.x()).arg(bounds.y());
#endif
    return a;
}
QStringList microphoneInput(const QString& source){
    auto a=inputPrefix(32,true);
#ifdef Q_OS_WIN
    a<<"-f"<<"dshow"<<"-audio_buffer_size"<<"50"<<"-i"<<"audio="+source;
#elif defined(Q_OS_MACOS)
    a<<"-f"<<"avfoundation"<<"-i"<<":"+source;
#else
    a<<"-f"<<"pulse"<<"-i"<<source;
#endif
    return a;
}
QStringList systemInput(const QString& source,const QString& nativeUrl){
#if defined(Q_OS_WIN)||defined(Q_OS_MACOS)
    return rawAudio(nativeUrl);
#else
    auto a=inputPrefix(32,true);a<<"-f"<<"pulse"<<"-i"<<source;return a;
#endif
}
QStringList recordArgs(const QStringList& video,const QList<QStringList>& audio,const Encoder& e,
                       const Config& cfg,Size out,const QString& output,bool directDxgi){
    auto a=command(e);
    a<<"-progress"<<"pipe:1"<<"-stats_period"<<"0.25"<<"-nostats"<<video;
    for(const auto& input:audio)a<<input;
    a<<"-map"<<"0:v:0";
    if(audio.size()==1)a<<"-map"<<"1:a:0"<<"-af"<<"aresample=48000:async=1000:first_pts=0";
    else if(audio.size()==2)a<<"-filter_complex"
        <<"[1:a:0]aresample=48000:async=1000:first_pts=0,aformat=channel_layouts=stereo[a];[2:a:0]aresample=48000:async=1000:first_pts=0,aformat=channel_layouts=stereo[b];[a][b]amix=inputs=2:duration=longest:normalize=1[mix]"
        <<"-map"<<"[mix]";
    if(audio.isEmpty())a<<"-an";
    else a<<"-c:a"<<"aac"<<"-ar"<<"48000"<<"-ac"<<"2"<<"-b:a"<<"192k";
    a<<encoderArgs(e,cfg,out,directDxgi)<<"-video_track_timescale"<<"90000"<<"-f"<<"mp4"<<output;
    return a;
}
QStringList concatArgs(const QString& output){
    return {"-hide_banner","-loglevel","warning","-y","-f","concat","-safe","1","-i","segments.txt",
            "-map","0","-c","copy","-movflags","+faststart",output};
}
}
