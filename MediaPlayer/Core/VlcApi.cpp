#include "VlcApi.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

bool VlcApi::load(QString& error){
    if(ready_)return true;
    const auto app=QCoreApplication::applicationDirPath();
    const auto root=qEnvironmentVariable("VLC_SDK_ROOT");
    const bool bundledOnly=qEnvironmentVariableIsSet("MEDIAPLAYER_BUNDLED_ONLY");
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates<<app+"/vlc/libvlc.dll"<<app+"/libvlc.dll";
    if(!bundledOnly&&!root.isEmpty())candidates<<QDir(root).filePath("libvlc.dll");
    if(!bundledOnly){
        const auto programFiles=qEnvironmentVariable("ProgramFiles");
        if(!programFiles.isEmpty())candidates<<QDir(programFiles).filePath("VideoLAN/VLC/libvlc.dll");
    }
#elif defined(Q_OS_MACOS)
    candidates<<app+"/lib/libvlc.dylib"<<app+"/lib/libvlc.5.dylib";
    if(!bundledOnly){
        if(!root.isEmpty())candidates<<QDir(root).filePath("lib/libvlc.dylib")<<QDir(root).filePath("lib/libvlc.5.dylib")<<QDir(root).filePath("libvlc.dylib");
        candidates<<"/Applications/VLC.app/Contents/MacOS/lib/libvlc.dylib"<<"/Applications/VLC.app/Contents/MacOS/lib/libvlc.5.dylib";
    }
#else
    if(!bundledOnly&&!root.isEmpty())candidates<<QDir(root).filePath("lib/libvlc.so.5")<<QDir(root).filePath("libvlc.so.5");
    candidates<<"libvlc.so.5";
#endif
    QStringList failures;
    for(const auto& file:candidates){
        if(QDir::isAbsolutePath(file)&&!QFileInfo::exists(file))continue;
        library_.setFileName(file);library_.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if(!library_.load()){failures<<library_.errorString();continue;}
        libvlc_get_version=reinterpret_cast<decltype(libvlc_get_version)>(library_.resolve("libvlc_get_version"));
        if(!libvlc_get_version||!version().startsWith("3.")){failures<<"Expected libVLC 3.x";library_.unload();continue;}
        QString missing;
#define RESOLVE(name) name=reinterpret_cast<decltype(name)>(library_.resolve(#name));if(!name)missing+=QStringLiteral(" " #name);
        VLC_API_SYMBOLS(RESOLVE)
        VLC_PLATFORM_SYMBOLS(RESOLVE)
#undef RESOLVE
        if(!missing.isEmpty()){failures<<"Missing symbols:"+missing;library_.unload();continue;}
        if(QDir::isAbsolutePath(file)){
            QDir dir(QFileInfo(file).absolutePath());QString pluginDir=dir.filePath("plugins");
            if(!QFileInfo(pluginDir).isDir()){dir.cdUp();pluginDir=dir.filePath("plugins");}
            if(QFileInfo(pluginDir).isDir())qputenv("VLC_PLUGIN_PATH",QDir::toNativeSeparators(pluginDir).toUtf8());
        }
        ready_=true;return true;
    }
    error="Cannot load libVLC 3.x. Install the matching runtime or set VLC_SDK_ROOT. "+failures.join("; ");return false;
}
QString VlcApi::version() const{return libvlc_get_version?QString::fromUtf8(libvlc_get_version()):QString{};}
QString VlcApi::lastError() const{auto msg=libvlc_errmsg?libvlc_errmsg():nullptr;return msg?QString::fromUtf8(msg):QString("Unknown libVLC error");}
