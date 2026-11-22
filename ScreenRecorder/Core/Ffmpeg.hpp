#pragma once
#include "RecordingPlan.hpp"
#include "../Platform/Native.hpp"
#include <QStringList>
#include <QVariantList>

namespace rec {
struct Config {
    int fps=30,bitrate=12000,quality=1,height=1080;
    bool cursor=true,microphone=false,systemAudio=false,hardware=true;
    QString mic,output;
};
struct AudioDevice {QString id,name;};
struct Devices {
    QList<AudioDevice> microphones;
    QString systemSource,error;
};
struct Encoder {QString name,device;};
QString ffmpegPath();
Devices discoverDevices(const QString&);
QList<Encoder> encoders(bool hardware);
QStringList encoderArgs(const Encoder&,const Config&,Size,bool directDxgi=false);
QStringList probeArgs(const Encoder&,const Config&,Size);
QStringList videoInput(const Target&,QRect region,const Config&,Size,const QString& nativeUrl,bool directDxgi=false);
QStringList microphoneInput(const QString&);
QStringList systemInput(const QString& source,const QString& nativeUrl);
QStringList recordArgs(const QStringList& video,const QList<QStringList>& audio,const Encoder&,
                       const Config&,Size,const QString& output,bool directDxgi=false);
QStringList concatArgs(const QString& output);
}
