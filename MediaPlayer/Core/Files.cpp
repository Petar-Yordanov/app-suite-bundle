#include "Files.hpp"
#include "Algorithms.hpp"
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>
#include <QCryptographicHash>
#include <QDateTime>
#include <QVariantMap>
#include <QSet>
#include <algorithm>

namespace media {
QStringList videoExtensions(){
    return {"mp4","m4v","mkv","webm","avi","mov","qt","wmv","asf","flv","f4v","mpeg","mpg","mpe","m1v","m2v","mpv","ts","mts","m2ts","m2t","vob","ogv","ogg","ogm","3gp","3g2","divx","rm","rmvb","mxf","dv","mjpg","mjpeg","nsv","nut","y4m","h264","h265","hevc","av1","ivf","vc1","bik","smk","roq","wtv","dvr-ms","amv","mod","tod","iso","dat","gxf","m4s","mp2v","trp","fli","flc"};
}
namespace {
QString localFile(const QUrl& url){
    if(!url.isLocalFile()||url.hasQuery()||url.hasFragment()||!url.host().isEmpty())return {};
    QFileInfo f(url.toLocalFile());return f.isFile()&&f.isReadable()?f.canonicalFilePath():QString{};
}
bool manifest(const QString& path){
    const auto suffix=QFileInfo(path).suffix().toLower();
    static const QSet<QString> set={"m3u","m3u8","xspf","pls","asx","wax","wvx","wpl","mpd","sdp","ram","url","strm","desktop","torrent","cue","smil","smi"};
    return set.contains(suffix);
}
}
QString localVideo(const QUrl& url){
    auto path=localFile(url);if(path.isEmpty()||manifest(path))return {};
    if(!videoCandidate(path)&&QMimeDatabase().mimeTypeForFile(path,QMimeDatabase::MatchExtension).name().startsWith("image/"))return {};
    return path; // actual video streams are verified by libVLC, not extension alone
}
QString localSubtitle(const QUrl& url){
    auto path=localFile(url);if(path.isEmpty())return {};
    static const QSet<QString> allowed={"srt","ass","ssa","sub","idx","vtt","smi","sami","ttml","dfxp","txt","usf","psb","scc","sup","stl","aqt","jss","rt","mpl","mpl2","pjs","sbt"};
    return allowed.contains(QFileInfo(path).suffix().toLower())?path:QString{};
}
bool videoCandidate(const QString& path){
    if(manifest(path))return false;
    static const QSet<QString> extensions=[] {auto list=videoExtensions();return QSet<QString>(list.begin(),list.end());}();
    if(extensions.contains(QFileInfo(path).suffix().toLower()))return true;
    return QMimeDatabase().mimeTypeForFile(path,QMimeDatabase::MatchExtension).name().startsWith("video/");
}
QStringList neighbors(const QString& current){
    QFileInfo info(current);if(!info.isFile())return {};
    QDir dir(info.absolutePath());QStringList out;QSet<QString> seen;
    const auto files=dir.entryInfoList(QDir::Files|QDir::Readable,QDir::Name|QDir::IgnoreCase);
    for(const auto& f:files)if(videoCandidate(f.fileName())||f.canonicalFilePath()==current){
        auto canonical=f.canonicalFilePath();if(!canonical.isEmpty()&&!seen.contains(canonical)){seen.insert(canonical);out.append(canonical);}
    }
    return out;
}
QString videoFilter(){QStringList filters;for(auto ext:videoExtensions())filters.append("*."+ext);return "Videos ("+filters.join(' ')+");;All files (*)";}
QString ResumeStore::key(const QString& path) const{return "resume/"+QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex());}
qint64 ResumeStore::load(const QString& path,qint64 duration) const{
    auto value=settings_.value(key(path)).toMap();QFileInfo f(path);
    if(!f.exists()||value.value("size").toLongLong()!=f.size()||value.value("modified").toLongLong()!=f.lastModified().toMSecsSinceEpoch())return 0;
    return resumePosition(value.value("time").toLongLong(),duration>0?duration:value.value("duration").toLongLong());
}
void ResumeStore::forget(const QString& path){if(!path.isEmpty())settings_.remove(key(path));}
void ResumeStore::save(const QString& path,qint64 time,qint64 duration){
    if(path.isEmpty()||duration<=0)return;
    auto pos=resumePosition(time,duration);if(!pos){forget(path);return;}
    QFileInfo f(path);if(!f.isFile())return;
    settings_.setValue(key(path),QVariantMap{{"time",pos},{"duration",duration},{"size",f.size()},{"modified",f.lastModified().toMSecsSinceEpoch()},{"saved",QDateTime::currentMSecsSinceEpoch()}});
    // Bound persisted history; pruning is amortized and only scans when full.
    settings_.beginGroup("resume");auto keys=settings_.childKeys();
    if(keys.size()>2000){
        QList<QPair<qint64,QString>> ages;for(const auto& k:keys)ages.append({settings_.value(k).toMap().value("saved").toLongLong(),k});
        std::sort(ages.begin(),ages.end());for(qsizetype i=0;i<ages.size()-1900;++i)settings_.remove(ages[i].second);
    }
    settings_.endGroup();
}
}
