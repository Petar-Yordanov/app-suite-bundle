#include "ElevatedOperations.h"
#include "LocalFileOperations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStorageInfo>
#include <filesystem>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace ElevatedOperations {
namespace {
bool fail(QString* error,QString message){if(error)*error=std::move(message);return false;}
bool pathValid(const QString& path){
    const auto clean=QDir::fromNativeSeparators(path);
    if(clean.isEmpty()||clean.size()>32767||clean.contains(QChar(0))||clean.contains('\n')||clean.contains('\r')||!QDir::isAbsolutePath(clean)||QDir(clean).isRoot())return false;
    if(clean.startsWith(":")||clean.startsWith("//")||clean.split('/').contains("..")||clean.split('/').contains("."))return false;
#ifdef Q_OS_WIN
    if(clean.size()<4||!clean.at(0).isLetter()||clean.mid(1,2)!=":/"||clean.mid(2).contains(':'))return false;
#endif
    return true;
}
// Avoid an elevated traversal through user-controlled symlink ancestors.
bool safeParent(const QString& path){
    auto parent=QFileInfo(path).absolutePath();auto canonical=QFileInfo(parent).canonicalFilePath();
#ifdef Q_OS_WIN
    return !canonical.isEmpty()&&QDir::cleanPath(parent).compare(canonical,Qt::CaseInsensitive)==0;
#else
    return !canonical.isEmpty()&&QDir::cleanPath(parent)==canonical;
#endif
}
// Recursive privileged operations are intentionally restricted to ordinary
// files/directories on a single volume in a trusted, stable directory tree.
bool safeTree(const QString& root,QString* error){
    const QFileInfo rootInfo(root);
    const auto volume=QStorageInfo(rootInfo.isDir()?root:rootInfo.absolutePath());
    if(!volume.isValid()||!volume.isReady()||QDir::cleanPath(volume.rootPath()).compare(QDir::cleanPath(root),Qt::CaseInsensitive)==0)
        return fail(error,"Cannot process a filesystem/mount root as administrator.");
    struct Item{QString path;int depth;};
    QList<Item> queue{{root,0}};qsizetype visited=0;
    while(!queue.isEmpty()){
        const auto item=queue.takeLast();QFileInfo info(item.path);
        if(++visited>100000||item.depth>128)return fail(error,"Administrator tree exceeds the safety traversal limit.");
        if(info.isSymLink()||(!info.isDir()&&!info.isFile())||!safeParent(item.path))
            return fail(error,"Administrator tree contains a link, special entry or unsafe ancestor: "+item.path);
#ifdef Q_OS_WIN
        auto native=QDir::toNativeSeparators(item.path).toStdWString();auto flags=GetFileAttributesW(native.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_REPARSE_POINT))return fail(error,"Administrator tree contains a reparse point: "+item.path);
#endif
        if(!info.isDir())continue;
        const auto current=QStorageInfo(item.path);
        if(!current.isValid()||current.rootPath()!=volume.rootPath()||current.device()!=volume.device()||!info.isReadable())
            return fail(error,"Administrator tree contains a mount boundary or unreadable directory: "+item.path);
        auto children=QDir(item.path).entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::NoSort);
        if(visited+queue.size()+children.size()>100000)return fail(error,"Administrator tree exceeds the safety traversal limit.");
        for(const auto& child:children)queue.append({child.absoluteFilePath(),item.depth+1});
    }
    return true;
}
#ifdef Q_OS_WIN
QString winQuote(const QString& arg){
    QString out="\"";int slashes=0;
    for(auto c:arg){if(c=='\\'){++slashes;continue;}out+=QString(c=='"'?2*slashes+1:slashes,'\\');out+=c;slashes=0;}
    return out+QString(2*slashes,'\\')+'"';
}
#elif defined(Q_OS_MACOS)
QString shellQuote(QString value){value.replace("'","'\"'\"'");return "'"+value+"'";}
QString appleQuote(QString value){value.replace("\\","\\\\");value.replace("\"","\\\"");return "\""+value+"\"";}
#endif
}
bool validate(const QStringList& args,QString* error){
    if(args.size()<2)return fail(error,"Missing administrator operation/path.");
    const auto op=args.first();const bool two=op=="copy"||op=="rename";
    if(!two&&op!="create-file"&&op!="create-directory"&&op!="remove"&&op!="remove-tree")return fail(error,"Unsupported administrator operation.");
    if(args.size()!=(two?3:2))return fail(error,"Incorrect number of paths.");
    for(qsizetype i=1;i<args.size();++i)if(!pathValid(args[i]))return fail(error,"Use an absolute local path, not a root, device, relative or container path.");
    return true;
}
bool execute(const QStringList& args,QString* error){
    if(!validate(args,error))return false;
    for(qsizetype i=1;i<args.size();++i)if(!safeParent(args[i]))return fail(error,"Administrator operations require an existing, canonical parent directory (no symbolic-link aliases).");
    const auto op=args[0],source=args[1];QFileInfo info(source);LocalFileOperations files;
    if(op=="create-file")return files.createFile(source,error);
    if(op=="create-directory")return files.createDirectory(source,error);
    if(op=="copy"){
        if(!safeTree(source,error))return false;
        return files.copy(source,args[2],error);
    }
    if(op=="rename")return files.rename(source,args[2],error);
    if(op=="remove-tree"){
        if(!info.isDir()||info.isSymLink())return fail(error,"Recursive removal requires an ordinary directory.");
        // A mistaken selection must never recursively remove a top-level OS directory.
        if(QDir::cleanPath(info.absolutePath()).compare(QDir::cleanPath(QStorageInfo(source).rootPath()),Qt::CaseInsensitive)==0)
            return fail(error,"Recursive administrator removal of top-level volume directories is forbidden.");
        if(!safeTree(source,error))return false;
        // No automatic retry, trash fallback or following symbolic links.
        // This is an explicitly confirmed operation for trusted directories.
        std::error_code ec;
#ifdef Q_OS_WIN
        auto native=std::filesystem::path(source.toStdWString());
#else
        auto native=std::filesystem::path(QFile::encodeName(source).constData());
#endif
        const auto count=std::filesystem::remove_all(native,ec);
        return (!ec&&count>0)||fail(error,"Recursive removal failed; some entries may already have been removed.");
    }
    // Ordinary remove is never recursively destructive.
    if(info.isDir()&&!info.isSymLink())return QDir().rmdir(source)||fail(error,"Only an empty directory can be removed as administrator.");
    return ((info.isFile()||info.isSymLink())&&QFile::remove(source))||fail(error,"Cannot remove this file as administrator.");
}
bool request(const QStringList& args,QString* error){
    if(!validate(args,error))return false;
    const auto executable=QCoreApplication::applicationFilePath();const auto helper=QStringList{"--administrator-operation"}+args;
#ifdef Q_OS_WIN
    QStringList quoted;for(const auto& arg:helper)quoted<<winQuote(arg);
    auto path=QDir::toNativeSeparators(executable).toStdWString(),parameters=quoted.join(' ').toStdWString();
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";info.lpFile=path.c_str();info.lpParameters=parameters.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)||!info.hProcess)return fail(error,"Administrator authorization cancelled or denied.");
    auto status=WaitForSingleObject(info.hProcess,INFINITE);DWORD code=1;
    if(status==WAIT_OBJECT_0)GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);
    return code==0||fail(error,"Administrator operation failed. Check destination, permissions and file type.");
#else
    QProcess process;
    // Invoke fixed OS binaries, never a shell or PATH-selected command.
#ifdef Q_OS_MACOS
    QStringList quoted{shellQuote(executable)};for(const auto& arg:helper)quoted<<shellQuote(arg);
    process.setProgram("/usr/bin/osascript");process.setArguments({"-e","do shell script "+appleQuote(quoted.join(' '))+" with administrator privileges"});
#else
    if(!QFileInfo("/usr/bin/pkexec").isExecutable())return fail(error,"Install polkit (pkexec) to request administrator operations.");
    process.setProgram("/usr/bin/pkexec");process.setArguments(QStringList{"--disable-internal-agent",executable}+helper);
#endif
    process.start();if(!process.waitForStarted(3000))return fail(error,process.errorString());
    process.waitForFinished(-1);
    return (process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0)||fail(error,QString::fromUtf8(process.readAllStandardError()).trimmed().left(2048)+" Administrator operation failed or was cancelled.");
#endif
}
}
