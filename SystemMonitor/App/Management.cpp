#include "Management.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#include <QVariantMap>
#include <cstdio>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winsvc.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#include <cerrno>
#include <cstring>
#endif
namespace management {
namespace {
QString str(const std::string& s){return QString::fromStdString(s);}
sm::Result failure(QString s,bool permission=false){return {false,s.toStdString(),permission};}
sm::Result success(QString s){return {true,s.toStdString()};}
struct Command {bool ok;QString output,error;};
Command run(QString path,QStringList args,int timeout=10000){
    QProcess p;p.setProgram(path);p.setArguments(args);
    auto env=QProcessEnvironment::systemEnvironment();env.insert("LC_ALL","C");env.insert("SYSTEMD_COLORS","0");env.insert("SYSTEMD_PAGER","cat");p.setProcessEnvironment(env);
    p.start();if(!p.waitForStarted(3000))return {false,{},p.errorString()};
    if(!p.waitForFinished(timeout)){p.kill();p.waitForFinished(1000);return {false,{},"Command timed out"};}
    return {p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0,QString::fromUtf8(p.readAllStandardOutput()),QString::fromUtf8(p.readAllStandardError()).trimmed()};
}
QVariant item(QString id,QString name,QString state,QString location,bool editable=true){
    return QVariantMap{{"id",id},{"name",name},{"state",state},{"location",location},{"editable",editable}};
}
QVariant errorItem(QString s){return item({},s,"Unavailable",{},false);}
QString quote(QString s){
    // Only used for macOS Authorization Services through osascript.
    s.replace("'","'\"'\"'");return "'"+s+"'";
}
QString appleQuote(QString s){s.replace("\\","\\\\");s.replace("\"","\\\"");return "\""+s+"\"";}
QString winQuote(QString s){
    QString out="\"";int n=0;
    for(auto c:s){if(c=='\\'){++n;continue;}if(c=='"')out+=QString(2*n+1,'\\');else out+=QString(n,'\\');n=0;out+=c;}
    out+=QString(n*2,'\\');return out+"\"";
}
sm::Result elevateArgs(const QStringList& args){
    auto exe=QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    QStringList escaped;for(auto& a:args)escaped<<winQuote(a);
    auto path=exe.toStdWString(),params=escaped.join(' ').toStdWString();
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";info.lpFile=path.c_str();info.lpParameters=params.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info))return failure("Elevation cancelled or denied.");
    if(!info.hProcess)return failure("No elevated process handle.");
    auto wait=WaitForSingleObject(info.hProcess,120000);DWORD code=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);
    return code==0?success("Elevated operation completed."):failure("Elevated operation failed or timed out (exit "+QString::number(code)+").");
#elif defined(Q_OS_MACOS)
    QStringList q{quote(exe)};for(auto& a:args)q<<quote(a);
    auto r=run("/usr/bin/osascript",{"-e","do shell script "+appleQuote(q.join(' '))+" with administrator privileges"},120000);
    return r.ok?success("Elevated operation completed."):failure(r.error);
#else
    auto pk=QStandardPaths::findExecutable("pkexec");
    if(pk.isEmpty())return failure("pkexec is not installed. Use a trusted administrative session.");
    auto r=run(pk,QStringList{exe}+args,120000);return r.ok?success("Elevated operation completed."):failure(r.error.isEmpty()?"Authorization or helper failed.":r.error);
#endif
}
#ifdef Q_OS_WIN
const QString userRun="HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const QString machineRun="HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
QString runKey(QString id){return id.startsWith("user:")?userRun:id.startsWith("machine:")?machineRun:QString();}
QString backupKey(QString key){return key+"-SystemMonitorDisabled";}
QVariantList registryStartup(){
    QVariantList result;
    for(auto pair:{std::pair{userRun,QString("user:")},std::pair{machineRun,QString("machine:")}}){
        QSettings enabled(pair.first,QSettings::NativeFormat),disabled(backupKey(pair.first),QSettings::NativeFormat);
        for(auto& name:enabled.childKeys())result.append(item(pair.second+name,name,"Enabled",enabled.value(name).toString()));
        for(auto& name:disabled.childKeys())if(!enabled.contains(name))result.append(item(pair.second+name,name,"Disabled",disabled.value(name).toString()));
    }return result;
}
sm::Result registryChange(QString id,bool enable){
    auto key=runKey(id),name=id.section(':',1);if(key.isEmpty()||name.isEmpty()||name.contains('/')||name.contains('\\'))return failure("Invalid startup entry.");
    HKEY root=id.startsWith("machine:")?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER;
    std::wstring active=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",inactive=active+L"-SystemMonitorDisabled",n=name.toStdWString();
    struct Key{HKEY h=nullptr;~Key(){if(h)RegCloseKey(h);}} from,to;
    auto status=RegOpenKeyExW(root,(enable?inactive:active).c_str(),0,KEY_QUERY_VALUE|KEY_SET_VALUE,&from.h);
    if(status!=ERROR_SUCCESS)return failure("Cannot access startup registry entry.",status==ERROR_ACCESS_DENIED);
    status=RegCreateKeyExW(root,(enable?active:inactive).c_str(),0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&to.h,nullptr);
    if(status!=ERROR_SUCCESS)return failure("Cannot open backup registry key.",status==ERROR_ACCESS_DENIED);
    DWORD bytes=0,type=0;
    DWORD existing=0;
    if(RegQueryValueExW(to.h,n.c_str(),nullptr,nullptr,nullptr,&existing)!=ERROR_FILE_NOT_FOUND)return failure("A conflicting startup entry exists.");
    status=RegQueryValueExW(from.h,n.c_str(),nullptr,&type,nullptr,&bytes);
    if(status!=ERROR_SUCCESS||bytes>1024*1024||(type!=REG_SZ&&type!=REG_EXPAND_SZ))return failure("Unsupported or changed startup registry entry.");
    std::vector<BYTE> data(bytes);
    status=RegQueryValueExW(from.h,n.c_str(),nullptr,&type,data.data(),&bytes);
    if(status!=ERROR_SUCCESS)return failure("Cannot read startup entry.",status==ERROR_ACCESS_DENIED);
    status=RegSetValueExW(to.h,n.c_str(),0,type,data.data(),bytes);
    if(status!=ERROR_SUCCESS)return failure("Cannot write startup registry entry.",status==ERROR_ACCESS_DENIED);
    status=RegDeleteValueW(from.h,n.c_str());
    if(status!=ERROR_SUCCESS){RegDeleteValueW(to.h,n.c_str());return failure("Cannot remove original startup entry.",status==ERROR_ACCESS_DENIED);}
    return success("Startup entry updated.");
}
QString startupFolder(bool common){
    PWSTR path=nullptr;QString out;
    if(SUCCEEDED(SHGetKnownFolderPath(common?FOLDERID_CommonStartup:FOLDERID_Startup,KF_FLAG_DONT_VERIFY,nullptr,&path))&&path)out=QString::fromWCharArray(path);
    if(path)CoTaskMemFree(path);return out;
}
QVariantList folderStartup(){
    QVariantList out;
    for(bool common:{false,true}){
        auto dir=startupFolder(common);if(dir.isEmpty())continue;
        auto prefix=common?"common-folder:":"user-folder:";
        for(bool disabled:{false,true}){
            auto path=disabled?dir+".SystemMonitorDisabled":dir;
            for(auto& f:QDir(path).entryInfoList(QDir::Files|QDir::NoDotAndDotDot)){
                if(f.fileName().compare("desktop.ini",Qt::CaseInsensitive)==0)continue;
                if(disabled&&QFile::exists(dir+"/"+f.fileName()))continue;
                out.append(item(prefix+f.fileName(),f.fileName(),disabled?"Disabled":"Enabled",f.absoluteFilePath()));
            }
        }
    }return out;
}
sm::Result folderChange(QString id,bool enable){
    bool common=id.startsWith("common-folder:");if(!common&&!id.startsWith("user-folder:"))return failure("Invalid startup folder entry.");
    auto name=id.section(':',1);if(name.isEmpty()||name=="."||name==".."||name.contains('/')||name.contains('\\')||name.contains(':')||name.compare("desktop.ini",Qt::CaseInsensitive)==0)return failure("Invalid startup filename.");
    auto dir=startupFolder(common);if(dir.isEmpty())return failure("Startup folder unavailable.");
    auto backup=dir+".SystemMonitorDisabled";auto a=dir+"/"+name,b=backup+"/"+name;
    auto from=enable?b:a,to=enable?a:b;
    if(!QFile::exists(from)||QFile::exists(to))return failure("Entry changed or a conflicting file exists.");
    if(!QDir().mkpath(enable?dir:backup)||!QFile::rename(from,to))return failure("Cannot move startup entry.",common);
    return success("Startup folder updated.");
}
QVariantList windowsServices(){
    QVariantList result;SC_HANDLE scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_ENUMERATE_SERVICE);
    if(!scm)return {errorItem("Cannot enumerate services")};
    DWORD bytes=0,count=0,resume=0;std::vector<unsigned char> buf(256*1024);
    do{
        BOOL ok=EnumServicesStatusExW(scm,SC_ENUM_PROCESS_INFO,SERVICE_WIN32,SERVICE_STATE_ALL,buf.data(),DWORD(buf.size()),&bytes,&count,&resume,nullptr);
        if(!ok&&GetLastError()!=ERROR_MORE_DATA){result.append(errorItem("Service enumeration failed"));break;}
        auto* rows=reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
        for(DWORD i=0;i<count;++i){auto& row=rows[i];QString state;
            switch(row.ServiceStatusProcess.dwCurrentState){case SERVICE_RUNNING:state="Running";break;case SERVICE_STOPPED:state="Stopped";break;case SERVICE_PAUSED:state="Paused";break;default:state="Pending";}
            result.append(item(QString::fromWCharArray(row.lpServiceName),QString::fromWCharArray(row.lpDisplayName),state,QString::fromWCharArray(row.lpServiceName),true));}
        if(ok)break;
    }while(resume);
    CloseServiceHandle(scm);return result;
}
sm::Result windowsService(QString name,QString action){
    auto scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!scm)return failure("Cannot access service manager.",true);
    auto w=name.toStdWString();auto service=OpenServiceW(scm,w.c_str(),SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG|(action=="start"?SERVICE_START:SERVICE_STOP));
    if(!service){auto e=GetLastError();CloseServiceHandle(scm);return failure("OpenService error "+QString::number(e),e==ERROR_ACCESS_DENIED);}
    DWORD bytes=0;QueryServiceConfigW(service,nullptr,0,&bytes);std::vector<unsigned char> config(bytes);
    auto* cfg=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config.data());
    bool safe=bytes&&QueryServiceConfigW(service,cfg,bytes,&bytes)&&cfg->dwErrorControl<SERVICE_ERROR_SEVERE&&cfg->dwStartType!=SERVICE_BOOT_START&&cfg->dwStartType!=SERVICE_SYSTEM_START;
    BOOL ok=FALSE;DWORD e=ERROR_ACCESS_DENIED;
    if(safe){SERVICE_STATUS status{};ok=action=="start"?StartServiceW(service,0,nullptr):ControlService(service,SERVICE_CONTROL_STOP,&status);if(!ok)e=GetLastError();}
    CloseServiceHandle(service);CloseServiceHandle(scm);
    if(!safe)return failure("Boot/system-critical or unverified service protected.");
    return ok?success("Service action requested."):failure("Service error "+QString::number(e),e==ERROR_ACCESS_DENIED);
}
#elif defined(Q_OS_MACOS)
QString plistLabel(QString path){
    QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>1024*1024)return {};auto b=f.readAll();
    auto data=CFDataCreate(kCFAllocatorDefault,reinterpret_cast<const UInt8*>(b.constData()),b.size());
    if(!data)return {};auto plist=CFPropertyListCreateWithData(kCFAllocatorDefault,data,kCFPropertyListImmutable,nullptr,nullptr);CFRelease(data);
    QString label;
    if(plist&&CFGetTypeID(plist)==CFDictionaryGetTypeID()){
        auto value=CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist),CFSTR("Label"));
        if(value&&CFGetTypeID(value)==CFStringGetTypeID()){
            char out[2048];if(CFStringGetCString(static_cast<CFStringRef>(value),out,sizeof(out),kCFStringEncodingUTF8))label=QString::fromUtf8(out);
        }
    }if(plist)CFRelease(plist);return label;
}
QString guiDomain(){return "gui/"+QString::number(getuid());}
QVariantList macEntries(bool startupOnly){
    QVariantList out;
    QStringList dirs{QDir::homePath()+"/Library/LaunchAgents","/Library/LaunchAgents"};
    if(!startupOnly)dirs<<"/Library/LaunchDaemons"<<"/System/Library/LaunchDaemons";
    auto gui=run("/bin/launchctl",{"print-disabled",guiDomain()});auto system= startupOnly?Command{}:run("/bin/launchctl",{"print-disabled","system"});
    for(auto& dir:dirs)for(auto& file:QDir(dir).entryInfoList({"*.plist"},QDir::Files)){
        auto label=plistLabel(file.absoluteFilePath());if(label.isEmpty())continue;
        bool daemon=dir.endsWith("LaunchDaemons");
        auto text=daemon?system.output:gui.output;
        QRegularExpression disabled("\""+QRegularExpression::escape(label)+"\"\\s*=>\\s*(true|disabled)");
        bool off=disabled.match(text).hasMatch();
        auto target=(daemon?"system":guiDomain())+"/"+label;
        out.append(item(target,label,(daemon?system.ok:gui.ok)?(off?"Disabled":"Enabled"):"Unknown",file.absoluteFilePath(),!dir.startsWith("/System/")));
    }
    return out;
}
#else
QString userAutostart(){return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/autostart";}
QStringList autostartDirs(){QStringList paths{userAutostart()};for(auto& p:QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))if(p+"/autostart"!=paths.first())paths<<p+"/autostart";return paths;}
QVariantList linuxStartup(){
    QVariantList result;QSet<QString> seen;
    for(auto& dir:autostartDirs())for(auto& file:QDir(dir).entryInfoList({"*.desktop","*.desktop.sm-disabled"},QDir::Files)){
        bool disabled=file.fileName().endsWith(".sm-disabled");auto name=file.fileName();if(disabled)name.chop(12);
        if(seen.contains(name))continue;seen.insert(name);
        QSettings s(file.absoluteFilePath(),QSettings::IniFormat);QString title=s.value("Desktop Entry/Name",name).toString();
        bool hidden=s.value("Desktop Entry/Hidden",false).toBool();
        if(s.value("Desktop Entry/X-SystemMonitor-Override",false).toBool()){
            QString original=file.absoluteFilePath()+".sm-disabled";
            if(!QFile::exists(original))for(auto& candidate:autostartDirs())if(candidate!=dir&&QFile::exists(candidate+"/"+name)){original=candidate+"/"+name;break;}
            if(QFile::exists(original)){QSettings old(original,QSettings::IniFormat);title=old.value("Desktop Entry/Name",name).toString();}
        }
        result.append(item(name,title,disabled||hidden?"Disabled":"Enabled",file.absoluteFilePath(),dir==userAutostart()||!hidden));
    }return result;
}
sm::Result linuxStartupChange(QString name,bool enable){
    if(!QRegularExpression("^[^/\\\\]+\\.desktop$").match(name).hasMatch()||name.startsWith('.'))return failure("Invalid desktop entry.");
    auto dir=userAutostart();if(!QDir().mkpath(dir))return failure("Cannot create autostart directory.");
    auto path=dir+"/"+name,backup=path+".sm-disabled";
    if(enable){
        QFile f(path);bool owned=false;if(f.open(QIODevice::ReadOnly)){owned=f.read(4096).contains("X-SystemMonitor-Override=true");f.close();}
        if(QFile::exists(backup)){
            if(QFile::exists(path)&&(!owned||!QFile::remove(path)))return failure("Startup entry conflict.");
            return QFile::rename(backup,path)?success("Startup enabled."):failure("Cannot restore desktop entry.");
        }
        if(owned)return QFile::remove(path)?success("Startup enabled."):failure("Cannot remove override.");
        return failure("Only entries disabled by this application can be restored.");
    }
    bool backedUp=false;
    if(QFile::exists(path)){
        if(QFile::exists(backup))return failure("Backup exists.");
        if(!QFile::rename(path,backup))return failure("Cannot disable desktop entry.");
        backedUp=true;
    }
    bool found=false;for(auto& other:autostartDirs())if(other!=dir&&QFile::exists(other+"/"+name))found=true;
    if(!found&&!backedUp)return failure("Desktop entry no longer exists.");
    QSaveFile f(path);
    if(f.open(QIODevice::WriteOnly)){
        f.write("[Desktop Entry]\nType=Application\nName=Disabled by System Monitor\nHidden=true\nX-SystemMonitor-Override=true\n");
        if(f.commit())return success("Startup disabled for current user.");
    }
    if(backedUp)QFile::rename(backup,path);
    return failure("Cannot save override.");
}
QVariantList linuxServices(){
    auto r=run("/usr/bin/systemctl",{"list-units","--type=service","--all","--no-legend","--plain","--no-pager"});
    if(!r.ok)return {errorItem(r.error.isEmpty()?"systemd unavailable":r.error)};
    QVariantList out;QSet<QString> seen;
    for(auto& l:r.output.split('\n',Qt::SkipEmptyParts)){
        auto fields=l.simplified().split(' ');if(fields.size()<4)continue;auto name=fields[0];if(!name.endsWith(".service"))continue;
        seen.insert(name);out.append(item(name,name,fields[2]+" / "+fields[3],fields.mid(4).join(' ')));
    }
    auto files=run("/usr/bin/systemctl",{"list-unit-files","--type=service","--no-legend","--no-pager"});
    if(files.ok)for(auto& l:files.output.split('\n',Qt::SkipEmptyParts)){auto fields=l.simplified().split(' ');if(fields.size()>1&&fields[0].endsWith(".service")&&!seen.contains(fields[0]))out.append(item(fields[0],fields[0],fields[1],"Unit file"));}
    return out;
}
#endif
}
QStringList commandFor(const sm::Request& r){
    QStringList args{"--privileged-process",QString::number(r.pid),QString::number(r.identity),QString::number(int(r.action)),QString::number(r.priority)};
    for(auto core:r.cores)args<<QString::number(core);return args;
}
sm::Result elevated(const sm::Request& r){return elevateArgs(commandFor(r));}
sm::Result elevatedTree(const sm::Request& r){return elevateArgs({"--privileged-tree",QString::number(r.pid),QString::number(r.identity),QString::number(sm::ownPid())});}
sm::Result raisePriority(){
#ifdef Q_OS_WIN
    return sm::protectPriority()?success("Safe above-normal scheduling priority enabled."):failure("Cannot increase scheduling priority.");
#else
    auto id=sm::processIdentity(sm::ownPid());if(!id)return failure("Monitor process identity unavailable.");
    return elevateArgs({"--privileged-monitor-priority",QString::number(sm::ownPid()),QString::number(*id)});
#endif
}
int helper(const QStringList& args){
    if(args.size()==5&&args[1]=="--privileged-tree"){
        bool a,b,c;sm::Request r;r.pid=args[2].toULongLong(&a);r.identity=args[3].toULongLong(&b);auto gui=args[4].toULongLong(&c);r.action=sm::Action::Force;
        if(!a||!b||!c||r.pid==gui||r.pid<=1||!r.identity)return 2;
        sm::Sampler sampler;auto snapshot=sampler.sample(false);auto requests=sm::processTree(snapshot,r);if(requests.empty())return 3;
        bool ok=true;for(auto& request:requests){if(request.pid==gui||request.pid==sm::ownPid()){ok=false;continue;}ok=sm::operate(request).ok&&ok;}return ok?0:3;
    }
#ifndef Q_OS_WIN
    if(args.size()==4&&args[1]=="--privileged-monitor-priority"){
        bool a,b;auto pid=args[2].toULongLong(&a),identity=args[3].toULongLong(&b);
        if(!a||!b||pid<=1||!identity)return 2;
        auto executable=QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        auto raw=sm::collectNative(true);
        for(auto& p:raw.processes)if(p.p.pid==pid&&p.p.identity==identity&&QFileInfo(str(p.p.path)).canonicalFilePath()==executable&&!executable.isEmpty()){
            if(sm::processIdentity(pid)!=std::optional<sm::U>(identity))return 3;
            return setpriority(PRIO_PROCESS,id_t(pid),-5)==0?0:3;
        }return 3;
    }
#endif
    if(args.size()>=6&&args[1]=="--privileged-process"){
        bool a,b,c,d;sm::Request r;r.pid=args[2].toULongLong(&a);r.identity=args[3].toULongLong(&b);int action=args[4].toInt(&c);r.priority=args[5].toInt(&d);
        if(!a||!b||!c||!d||action<0||action>int(sm::Action::Affinity)||args.size()>1024)return 2;r.action=sm::Action(action);
        for(int i=6;i<args.size();++i){bool ok;int n=args[i].toInt(&ok);if(!ok)return 2;r.cores.push_back(n);}
        auto result=sm::operate(r);if(!result.ok)fprintf(stderr,"%s\n",result.message.c_str());return result.ok?0:3;
    }
#ifdef Q_OS_WIN
    if(args.size()==4&&args[1]=="--privileged-service"&&(args[3]=="start"||args[3]=="stop"))return windowsService(args[2],args[3]).ok?0:3;
    if(args.size()==4&&args[1]=="--privileged-startup"&&args[2].startsWith("machine:")&&(args[3]=="enable"||args[3]=="disable"))return registryChange(args[2],args[3]=="enable").ok?0:3;
    if(args.size()==4&&args[1]=="--privileged-startup"&&args[2].startsWith("common-folder:")&&(args[3]=="enable"||args[3]=="disable"))return folderChange(args[2],args[3]=="enable").ok?0:3;
#elif defined(Q_OS_MACOS)
    if(args.size()==4&&args[1]=="--privileged-service"&&args[2].startsWith("system/")&&(args[3]=="start"||args[3]=="stop")){
        // Only installed third-party daemons; system services are not eligible.
        auto rows=macEntries(false);for(auto& v:rows){auto m=v.toMap();if(m["id"]==args[2]&&m["location"].toString().startsWith("/Library/LaunchDaemons/")){
            auto r=args[3]=="start"?run("/bin/launchctl",{"kickstart",args[2]}):run("/bin/launchctl",{"kill","SIGTERM",args[2]});return r.ok?0:3;
        }}return 3;
    }
#endif
    return 2;
}
QVariantList startup(){
#ifdef Q_OS_WIN
    return registryStartup()+folderStartup();
#elif defined(Q_OS_MACOS)
    return macEntries(true);
#else
    return linuxStartup();
#endif
}
QVariantList services(){
#ifdef Q_OS_WIN
    return windowsServices();
#elif defined(Q_OS_MACOS)
    return macEntries(false);
#else
    return linuxServices();
#endif
}
sm::Result change(QString type,QString id,QString op){
    if(id.isEmpty())return failure("No entry selected.");
    if(type=="startup"){
        if(op!="enable"&&op!="disable")return failure("Invalid startup operation.");
#ifdef Q_OS_WIN
        auto r=id.startsWith("user-folder:")||id.startsWith("common-folder:")?folderChange(id,op=="enable"):registryChange(id,op=="enable");
        if(!r.ok&&r.permission&&(id.startsWith("machine:")||id.startsWith("common-folder:")))return elevateArgs({"--privileged-startup",id,op});return r;
#elif defined(Q_OS_MACOS)
        if(!id.startsWith(guiDomain()+"/"))return failure("Invalid launch-agent target.");
        auto r=run("/bin/launchctl",{op,id});return r.ok?success("Launch agent updated."):failure(r.error);
#else
        return linuxStartupChange(id,op=="enable");
#endif
    }
    if(type!="services"||(op!="start"&&op!="stop"))return failure("Invalid service operation.");
#ifdef Q_OS_WIN
    auto r=windowsService(id,op);if(!r.ok&&r.permission)return elevateArgs({"--privileged-service",id,op});return r;
#elif defined(Q_OS_MACOS)
    if(id.startsWith("system/"))return elevateArgs({"--privileged-service",id,op});
    if(!id.startsWith(guiDomain()+"/"))return failure("Invalid service target.");
    auto r=op=="start"?run("/bin/launchctl",{"kickstart",id}):run("/bin/launchctl",{"kill","SIGTERM",id});return r.ok?success("Service operation requested (KeepAlive jobs can restart)."):failure(r.error);
#else
    if(!QRegularExpression("^[A-Za-z0-9_.@:-]+\\.service$").match(id).hasMatch()||id.startsWith('-'))return failure("Invalid service ID.");
    if(QSet<QString>{"systemd-logind.service","systemd-journald.service","dbus.service","polkit.service","systemd-udevd.service"}.contains(id)&&op=="stop")return failure("Core service protected.");
    auto r=run("/usr/bin/systemctl",{op,"--",id},120000);return r.ok?success("Service action completed."):failure(r.error);
#endif
}
}
