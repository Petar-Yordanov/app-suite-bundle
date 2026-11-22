#include "Native.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamWriter>

namespace rec::native {
namespace {
QString startupPath(){
#ifdef Q_OS_MACOS
    return QDir::homePath()+"/Library/LaunchAgents/org.appsuite.screenrecorder.plist";
#else
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/autostart/org.appsuite.screenrecorder.desktop";
#endif
}
QString executable(){return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());}
bool portable(){return QCoreApplication::arguments().contains("--portable");}
}
bool autostartEnabled(){
#ifdef Q_OS_WIN
    return QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat).contains("ScreenRecorder");
#else
    return QFile::exists(startupPath());
#endif
}
bool setAutostart(bool enable,QString& error){
#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);
    if(enable)reg.setValue("ScreenRecorder","\""+executable()+"\" --background"+(portable()?" --portable":""));else reg.remove("ScreenRecorder");
    reg.sync();if(reg.status()!=QSettings::NoError){error="Cannot modify login startup.";return false;}return true;
#else
    const auto path=startupPath();if(!enable){if(QFile::exists(path)&&!QFile::remove(path)){error="Cannot remove login startup.";return false;}return true;}
    if(!QDir().mkpath(QFileInfo(path).absolutePath())){error="Cannot create startup directory.";return false;}
    QSaveFile out(path);if(!out.open(QIODevice::WriteOnly)){error=out.errorString();return false;}
#ifdef Q_OS_MACOS
    QXmlStreamWriter x(&out);x.setAutoFormatting(true);x.writeStartDocument();x.writeDTD("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
    x.writeStartElement("plist");x.writeAttribute("version","1.0");x.writeStartElement("dict");x.writeTextElement("key","Label");x.writeTextElement("string","org.appsuite.screenrecorder");
    x.writeTextElement("key","ProgramArguments");x.writeStartElement("array");x.writeTextElement("string",executable());x.writeTextElement("string","--background");if(portable())x.writeTextElement("string","--portable");x.writeEndElement();
    x.writeTextElement("key","RunAtLoad");x.writeEmptyElement("true");x.writeEndElement();x.writeEndElement();x.writeEndDocument();if(x.hasError()){error="Cannot write login agent.";return false;}
#else
    QString pathEsc=executable();pathEsc.replace("\\","\\\\").replace("\"","\\\"").replace("`","\\`").replace("$","\\$").replace("%","%%");
    const auto bytes=("[Desktop Entry]\nType=Application\nName=Screen Recorder\nExec=\""+pathEsc+"\" --background"+(portable()?" --portable":"")+"\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8();
    if(out.write(bytes)!=bytes.size()){error=out.errorString();return false;}
#endif
    if(!out.commit()){error=out.errorString();return false;}return true;
#endif
}
}
