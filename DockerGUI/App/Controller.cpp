#include "Controller.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrl>
#include <QKeySequence>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSysInfo>
#include <QDateTime>
#include <algorithm>

namespace {
QString str(const QVariantMap& m,const char* k){return m.value(k).toString().trimmed();}
QString js(const QJsonObject& o,const char* k){
    auto v=o.value(k);if(v.isString())return v.toString();
    if(v.isDouble())return QString::number(v.toDouble(),'f',0);
    if(v.isArray()||v.isObject())return QString::fromUtf8((v.isArray()?QJsonDocument(v.toArray()):QJsonDocument(v.toObject())).toJson(QJsonDocument::Compact));
    return {};
}
QString bytes(qint64 n){
    if(n<0)return "Unknown";
    double v=n;QStringList units={"B","KiB","MiB","GiB","TiB"};int i=0;
    while(v>=1024&&i+1<units.size()){v/=1024;++i;}
    return QString::number(v,'f',i?1:0)+" "+units[i];
}
QString shellQuote(QString s){return "'"+s.replace("'","'\\''")+"'";}
bool validId(QString s){return !s.isEmpty()&&!s.startsWith('-')&&!s.contains(QChar::Null)&&!s.contains('\n');}
QStringList lines(QString s){QStringList r;for(auto x:s.split('\n'))if(!x.trimmed().isEmpty())r<<x.trimmed();return r;}
void cancelProcess(QProcess* p){if(p){p->setProperty("cancelRequested",true);p->kill();}}
}
Controller::Controller(QString path,QObject* parent,std::unique_ptr<Backend> backend):QObject(parent),settings_(path,QSettings::IniFormat),
    backend_(backend?std::move(backend):std::make_unique<DockerBackend>()),docker_(*backend_),model_(this){
    poll_.setInterval(qBound(2,interval(),60)*1000);poll_.setTimerType(Qt::CoarseTimer);
    connect(&poll_,&QTimer::timeout,this,&Controller::refresh);poll_.start();
    outputTimer_.setInterval(100);outputTimer_.setSingleShot(true);
    connect(&outputTimer_,&QTimer::timeout,this,[this]{
        output_+=pending_;pending_.clear();if(output_.size()>262144)output_=output_.right(262144);
        emit outputChanged();
    });
    QTimer::singleShot(0,this,&Controller::discover);
}
Controller::~Controller(){
    closing_=true;
    for(auto* p:docker_.findChildren<QProcess*>())if(p->state()!=QProcess::NotRunning){p->disconnect();p->kill();p->waitForFinished(500);}
}
void Controller::say(QString s){status_=std::move(s);emit changed();}
void Controller::append(QString s){pending_+=s;if(pending_.size()>262144)pending_=pending_.right(262144);if(!outputTimer_.isActive())outputTimer_.start();}
void Controller::clearOutput(){outputTimer_.stop();pending_.clear();output_.clear();emit outputChanged();}
void Controller::setTheme(QString s){if(QStringList{"System","Light","Dark"}.contains(s)){settings_.setValue("theme",s);emit changed();}}
void Controller::setInterval(int n){settings_.setValue("interval",qBound(2,n,60));poll_.setInterval((page_=="Storage"?qMax(interval(),30):interval())*1000);emit changed();}
void Controller::setRefreshKey(QString s){if(!QKeySequence(s).isEmpty()){settings_.setValue("refreshKey",s);emit changed();}}
void Controller::setSettingsKey(QString s){if(!QKeySequence(s).isEmpty()){settings_.setValue("settingsKey",s);emit changed();}}
void Controller::setFeed(QString s){settings_.setValue("feed",s);emit feedChanged();emit changed();}
void Controller::setAutoStart(bool b){settings_.setValue("autoStart",b);emit changed();}
void Controller::setVisible(bool b){visible_=b;if(b){poll_.start();refresh();}else{poll_.stop();stopOutput();}}
QString Controller::endpoint()const{
    for(auto v:contexts_)if(v.toMap().value("name").toString()==context_)return v.toMap().value("endpoint").toString();
    return {};
}
void Controller::discover(){
    if(discovering_||busy())return;
    if(docker_.executable().isEmpty()){say("Docker CLI not found. Install or expose your existing runtime's CLI in PATH.");return;}
    discovering_=true;
    // Context discovery itself is independent of the currently selected engine.
    DockerBackend::process(&docker_,docker_.executable(),{"context","ls","--format","{{json .}}"},[this](Result r){
        discovering_=false;if(closing_)return;
        if(!r.ok){say(r.error);return;}
        QVariantList found;QString current;
        for(auto v:DockerBackend::jsonRows(r.output)){
            auto o=v.toObject();auto name=js(o,"Name");
            if(name.endsWith(" *"))name.chop(2);
            if(name.isEmpty())continue;
            found.append(QVariantMap{{"name",name},{"endpoint",js(o,"DockerEndpoint")},{"description",js(o,"Description")}});
            if(o.value("Current").toBool()||js(o,"Current")=="true"||js(o,"Current")=="*")current=name;
        }
        if(qEnvironmentVariableIsSet("DOCKER_HOST")||qEnvironmentVariableIsSet("DOCKER_CONTEXT")){
            found.prepend(QVariantMap{{"name","(Environment)"},{"endpoint",qEnvironmentVariable("DOCKER_HOST")}});
            current="(Environment)";
        }
        contexts_=found;
        auto wanted=context_.isEmpty()?settings_.value("context",current).toString():context_;
        bool known=false;for(auto v:found)if(v.toMap().value("name")==wanted)known=true;
        if(!known)wanted=current.isEmpty()?(found.isEmpty()?QString():found.first().toMap().value("name").toString()):current;
        if(wanted.isEmpty()){say("No Docker contexts found. Configure an engine with Docker CLI.");return;}
        switchContext(wanted);
    });
}
void Controller::switchContext(QString name){
    if(busy()||starting_){say("Wait for the current operation before switching context.");return;}
    bool known=false;for(auto row:contexts_)if(row.toMap().value("name").toString()==name)known=true;
    if(!known)return;
    closeBrowser();stopOutput();clearOutput();++generation_;context_=name;settings_.setValue("context",name);
    docker_.setContext(name=="(Environment)"?QString():name,endpoint());
    model_.update({});online_=false;say("Connecting to "+name+"…");refresh();
}
void Controller::selectPage(QString p){
    if(!QStringList{"Containers","Images","Volumes","Networks","Compose","Storage","Statistics"}.contains(p)||p==page_)return;
    closeBrowser();stopOutput();++generation_;page_=p;poll_.setInterval((page_=="Storage"?qMax(interval(),30):interval())*1000);model_.update({});emit changed();refresh();
}
void Controller::refresh(){
    if(closing_||loading_||starting_||context_.isEmpty()||discovering_||busy())return;
    loading_=true;auto g=generation_;
    auto finish=[this,g](Result r){
        if(closing_)return;
        loading_=false;
        if(g!=generation_){refresh();return;}
        if(!r.ok){
            if(page_=="Containers"||(!online_&&page_!="Compose")||r.error.contains("Cannot connect")||r.error.contains("error during connect"))engineFailed(r.error);
            else say(r.error);return;
        }
        online_=true;updateRows(DockerBackend::jsonRows(r.output));say("Connected · "+context_);
    };
    if(page_=="Containers"){docker_.containers(finish);return;}
    if(page_=="Images"||page_=="Volumes"||page_=="Networks"){docker_.resources(page_,finish);return;}
    QStringList args;
    if(page_=="Compose")args={"compose","ls","--all"};
    else if(page_=="Storage")args={"system","df"};
    else args={"stats","--no-stream","--no-trunc","--all"};
    if(page_=="Compose")args<< "--format"<<"json";
    else args<<"--format"<<"{{json .}}";
    docker_.run(args,finish,{}, {}, page_=="Storage"?60000:30000);
}
void Controller::engineFailed(QString reason){
    online_=false;model_.update({});say(reason.isEmpty()?"Docker Engine unavailable.":reason);
    if(autoStart()&&!attempted_.contains(context_)){attempted_.insert(context_);startEngine({});}
}
void Controller::updateRows(QJsonArray rows){
    QVector<QVariantMap> result;result.reserve(rows.size());
    for(auto v:rows){
        auto o=v.toObject();QString id,title,sub,status,detail;
        if(page_=="Containers"){
            id=js(o,"Id");if(id.isEmpty())id=js(o,"ID");
            title=js(o,"Names");
            if(o.value("Names").isArray()){QStringList n;for(auto a:o.value("Names").toArray())n<<a.toString().remove(QRegularExpression("^/"));title=n.join(", ");}
            sub=js(o,"Image");status=js(o,"Status");detail=js(o,"Ports");
        }else if(page_=="Images"){
            if(o.contains("Id")){
                id=js(o,"Id");
                auto tags=o.value("RepoTags").toArray();if(tags.isEmpty())tags.append("<none>:<none>");
                for(auto tag:tags){
                    title=tag.toString();if(title=="<none>:<none>")title=id.left(19);
                    result.append(QVariantMap{{"key",id+"|"+title},{"id",id},{"title",title},{"sub",id.left(19)},{"status",bytes(o.value("Size").toInteger(-1))},{"detail",QDateTime::fromSecsSinceEpoch(o.value("Created").toInteger()).toString(Qt::ISODate)}});
                }
                continue;
            }
            id=js(o,"ID");title=js(o,"Repository")+":"+js(o,"Tag");sub=id.left(19);status=js(o,"Size");detail=js(o,"CreatedSince");
        }else if(page_=="Volumes"){
            id=title=js(o,"Name");sub=js(o,"Driver");status=js(o,"Scope");
        }else if(page_=="Networks"){
            id=js(o,"ID");if(id.isEmpty())id=js(o,"Id");title=js(o,"Name");sub=js(o,"Driver");status=js(o,"Scope");
        }else if(page_=="Compose"){
            id=title=js(o,"Name");status=js(o,"Status");detail=js(o,"ConfigFiles");
        }else if(page_=="Storage"){
            id=title=js(o,"Type");status=js(o,"Size");sub=js(o,"TotalCount")+" total · "+js(o,"Active")+" active";detail="Reclaimable "+js(o,"Reclaimable");
        }else{
            id=js(o,"ID");title=js(o,"Name");sub="CPU "+js(o,"CPUPerc")+" · RAM "+js(o,"MemUsage")+" ("+js(o,"MemPerc")+")";
            status="Network "+js(o,"NetIO");detail="Disk I/O "+js(o,"BlockIO")+" · PIDs "+js(o,"PIDs");
        }
        if(id.isEmpty())continue;
        auto key=page_=="Images"?id+"|"+title:id;
        result.append(QVariantMap{{"key",key},{"id",id},{"title",title.isEmpty()?id.left(12):title},{"sub",sub},{"status",status},{"detail",detail}});
    }
    model_.update(std::move(result));
}
void Controller::stopOutput(){++logGeneration_;if(logs_){cancelProcess(logs_);logs_=nullptr;}}
void Controller::execute(QStringList args,bool stream,QByteArray input,bool secret){
    if(busy()||starting_){say("Another operation is in progress.");return;}
    if(context_.isEmpty()){say("Choose a Docker context first.");return;}
    stopOutput();clearOutput();say("Running Docker operation…");
    auto done=[this,secret,args](Result r){
        job_=nullptr;if(closing_)return;
        if(!secret&&!r.output.isEmpty())append(QString::fromUtf8(r.output));
        if(!r.ok)append("\n"+(secret?QString("Registry authentication failed. Check the credentials and credential helper."):r.error));
        say(r.ok?"Operation completed.":secret?"Registry authentication failed.":r.error);
        if(r.ok&&args.value(0)=="context")discover();else refresh();
    };
    job_=docker_.run(args,done,stream?Stream([this](QString s){append(s);}):Stream{},std::move(input),stream?0:120000);
    emit changed();
}
QStringList Controller::composeArgs(const QVariantMap& row,const QVariantMap& f)const{
    QStringList args={"compose"};
    auto name=str(f,"project");if(name.isEmpty())name=str(row,"id");
    if(!name.isEmpty())args<<"--project-name"<<name;
    auto paths=lines(str(f,"files"));
    if(paths.isEmpty())paths=str(row,"detail").split(',',Qt::SkipEmptyParts);
    for(auto path:paths)if(!path.trimmed().isEmpty())args<<"-f"<<path.trimmed();
    return args;
}
bool Controller::credentialHelper(QString registry)const{
    QString dir=qEnvironmentVariable("DOCKER_CONFIG",QDir::homePath()+"/.docker");
    QFile file(dir+"/config.json");
    if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)return false;
    auto cfg=QJsonDocument::fromJson(file.readAll()).object();
    auto server=registry.isEmpty()?"https://index.docker.io/v1/":registry;
    auto helper=cfg.value("credHelpers").toObject().value(server).toString();
    if(helper.isEmpty())helper=cfg.value("credsStore").toString();
    if(!QRegularExpression("^[a-zA-Z0-9._-]+$").match(helper).hasMatch())return false;
    return !QStandardPaths::findExecutable("docker-credential-"+helper).isEmpty()||
        QFileInfo(QFileInfo(docker_.executable()).dir().filePath("docker-credential-"+helper
#ifdef Q_OS_WIN
        +".exe"
#endif
        )).isExecutable();
}
void Controller::action(QString verb,QVariantMap row,QVariantMap f){
    if(verb=="cancel"){cancelProcess(job_);return;}
    if(closing_||busy()||starting_){say("Wait for the current operation.");return;}
    auto id=str(row,"id"),name=str(f,"name"),image=str(f,"image");
    auto type=page_=="Containers"?"container":page_=="Images"?"image":page_=="Volumes"?"volume":page_=="Networks"?"network":"";
    auto require=[this](QString v){if(validId(v))return true;say("Enter a valid resource name.");return false;};
    if(verb=="inspect"||verb=="delete"){
        if(!QString(type).isEmpty()&&require(id)){
            // Delete the selected tag, not an entire multi-tagged image.
            QString target=id;
            if(page_=="Images"&&verb=="delete"&&str(row,"title").contains(':')&&!str(row,"title").contains("<none>")&&!str(row,"title").startsWith("sha256:"))target=str(row,"title");
            execute({type,verb=="inspect"?"inspect":"rm",target});
        }return;
    }
    if(QStringList{"start","stop","restart","pause","unpause"}.contains(verb)&&page_=="Containers"){
        if(require(id))execute({"container",verb,id});return;
    }
    if(verb=="logs"){
        if(!require(id))return;
        stopOutput();clearOutput();auto lg=logGeneration_;
        logs_=docker_.run({"logs","--tail","500","--timestamps","--follow",id},[this,lg](Result r){
            if(lg!=logGeneration_||closing_)return;logs_=nullptr;if(!r.ok)append("\n"+r.error);say("Log stream ended.");
        },[this,lg](QString s){if(lg==logGeneration_)append(s);},{},0);
        say("Following container logs.");return;
    }
    if(verb=="createContainer"){
        if(!require(image))return;
        QStringList a={"container",f.value("start").toBool()?"run":"create"};
        if(f.value("start").toBool())a<<"-d";
        if(!name.isEmpty()){if(!require(name))return;a<<"--name"<<name;}
        for(auto x:lines(str(f,"env")))a<<"--env"<<x;
        for(auto x:lines(str(f,"mounts")))a<<"--mount"<<x;
        for(auto x:lines(str(f,"ports")))a<<"--publish"<<x;
        auto restart=str(f,"restart");if(!restart.isEmpty())a<<"--restart"<<restart;
        a<<"--"<<image;for(auto x:lines(str(f,"command")))a<<x;
        execute(a,true);return;
    }
    if(verb=="configure"){
        if(!require(id))return;
        auto restart=str(f,"restart");
        if(restart.isEmpty()){say("Enter a restart policy.");return;}
        execute({"container","update","--restart",restart,id});return;
    }
    if(verb=="pull"){if(require(image))execute({"image","pull",image},true);return;}
    if(verb=="push"){auto tag=str(f,"tag");if(require(tag))execute({"image","push",tag},true);return;}
    if(verb=="tag"){auto tag=str(f,"tag");if(require(id)&&require(tag))execute({"image","tag",id,tag});return;}
    if(verb=="build"){
        auto dir=str(f,"directory"),file=str(f,"dockerfile"),tag=str(f,"tag");
        if(!QFileInfo(dir).isDir()){say("Select a local build-context directory.");return;}
        QStringList a={"build","--progress=plain"};
        if(!file.isEmpty())a<<"-f"<<file;if(!tag.isEmpty())a<<"-t"<<tag;
        a<<"--"<<dir;execute(a,true);return;
    }
    if(verb=="login"){
        auto registry=str(f,"registry"),user=str(f,"user");auto password=f.value("password").toString();
        if(!require(user)||password.isEmpty()||(!registry.isEmpty()&&!require(registry)))return;
        if(!credentialHelper(registry)){say("Configure a Docker credential helper (credsStore/credHelpers) before signing in. Plaintext credential storage is disabled.");return;}
        QStringList a={"login","--username",user,"--password-stdin"};if(!registry.isEmpty())a<<registry;
        auto data=password.toUtf8()+"\n";password.fill(QChar(u'\0'));execute(a,false,std::move(data),true);return;
    }
    if(verb=="logout"){auto registry=str(f,"registry");QStringList a={"logout"};if(!registry.isEmpty()){if(!require(registry))return;a<<registry;}execute(a);return;}
    if(verb=="createVolume"||verb=="createNetwork"){
        if(!require(name))return;
        QStringList a={verb=="createVolume"?"volume":"network","create"};auto driver=str(f,"driver");if(!driver.isEmpty())a<<"--driver"<<driver;
        a<<"--"<<name;execute(a);return;
    }
    if(verb=="prune"){
        auto resource=str(f,"resource");if(!QStringList{"container","image","volume","network","system"}.contains(resource))return;
        QStringList args={resource,"prune","--force"};
        if(f.value("all").toBool()&&(resource=="image"||resource=="volume"))args<<"--all";
        execute(args,true);return;
    }
    if(verb=="composeUp"||verb=="composeDown"||verb=="composeStart"||verb=="composeStop"||verb=="composeLogs"){
        auto a=composeArgs(row,f);
        if(!a.contains("-f")){say("Provide Compose file path(s) on this computer.");return;}
        if(verb=="composeUp")a<<"up"<<"-d";else if(verb=="composeDown")a<<"down";else if(verb=="composeStart")a<<"start";else if(verb=="composeStop")a<<"stop";else a<<"logs"<<"--tail"<<"500";
        execute(a,true);return;
    }
    if(verb=="browse"){
        browse(id,str(f,"path"));return;
    }
    if(verb=="copyOut"||verb=="copyIn"){
        if(!require(id))return;auto p=f.value("path").toString(),local=f.value("local").toString();
        if(p.isEmpty()||local.isEmpty()){say("Enter container and local paths.");return;}
        auto remote=id+":"+p;execute({"cp","--",verb=="copyOut"?remote:QFileInfo(local).absoluteFilePath(),verb=="copyOut"?QFileInfo(local).absoluteFilePath():remote},true);return;
    }
    if(verb=="writableSize"){if(require(id))execute({"container","inspect","--size","--format","Writable layer: {{.SizeRw}} bytes\nRoot filesystem: {{.SizeRootFs}} bytes",id});return;}
    if(verb=="createContext"){
        auto host=str(f,"host");if(!require(name)||!(host.startsWith("ssh://")||host.startsWith("unix://")||host.startsWith("npipe://"))){say("Use an SSH or local socket endpoint. Configure TLS contexts using Docker CLI.");return;}
        execute({"context","create",name,"--docker","host="+host});return;
    }
    say("Unsupported action for this resource.");
}
void Controller::startEngine(QString wsl){
    if(starting_||busy()||context_.isEmpty())return;
    auto ep=endpoint();
    QString program;QStringList args;
    // Never start a local runtime for a remote context.
    if(ep.startsWith("ssh://")||ep.startsWith("tcp://")||ep.startsWith("http")){
        say("Remote engine unavailable. Start it on its host.");return;
    }
    if(context_.startsWith("desktop-")){
        program=docker_.executable();args={"desktop","start"};
#ifdef Q_OS_LINUX
    }else if(ep.startsWith("unix://")&&ep.contains("/run/user/")){
        program=QStandardPaths::findExecutable("systemctl");args={"--user","start","docker.service"};
    }else if(ep=="unix:///var/run/docker.sock"||ep=="unix:///run/docker.sock"){
        program=QStandardPaths::findExecutable("systemctl");args={"--no-ask-password","start","docker.service"};
#elif defined(Q_OS_MACOS)
    }else if(ep.contains("/.colima/")&&ep.endsWith("/docker.sock")){
        program=QStandardPaths::findExecutable("colima");
        auto profile=QFileInfo(ep).dir().dirName();
        if(!QRegularExpression("^[A-Za-z0-9_-]+$").match(profile).hasMatch()){say("Invalid Colima profile.");return;}
        args={"start","--profile",profile};
#elif defined(Q_OS_WIN)
    }else if(!wsl.isEmpty()&&ep.startsWith("npipe://")){
        if(!validId(wsl)){say("Invalid WSL distribution.");return;}
        program=QStandardPaths::findExecutable("wsl.exe");args={"--distribution",wsl,"--user","root","--exec","service","docker","start"};
    }else if(ep.contains("/pipe/docker_engine")){
        program=QStandardPaths::findExecutable("sc.exe");args={"start","docker"};
#endif
    }
    if(program.isEmpty()){say("No supported startup mechanism for this context. Start its existing runtime or check service permissions.");return;}
    starting_=true;say("Starting existing Docker runtime…");
    DockerBackend::process(&docker_,program,args,[this](Result r){
        starting_=false;if(closing_)return;
        say(r.ok?"Runtime start requested. Reconnecting…":"Runtime start failed: "+r.error);
        QTimer::singleShot(2000,this,&Controller::refresh);
    },{}, {},120000);
}
void Controller::terminal(QString id,QString shell){
    if(!validId(id)||context_.isEmpty()||docker_.executable().isEmpty()){say("Select a container first.");return;}
    if(shell.isEmpty())shell="/bin/sh";
    if(!validId(shell)){say("Invalid container shell.");return;}
    QStringList cmd={docker_.executable()};cmd+=docker_.arguments({"exec","-it",id,shell});
    bool success=false;
#ifdef Q_OS_WIN
    auto wt=QStandardPaths::findExecutable("wt.exe");
    if(!wt.isEmpty()){
        QStringList a={"new-tab","--"};a+=cmd;success=QProcess::startDetached(wt,a);
    }
    if(!success){
        // A dedicated console host gives Docker a genuine terminal, not pipes.
        auto host=QStandardPaths::findExecutable("conhost.exe");
        if(!host.isEmpty())success=QProcess::startDetached(host,cmd);
    }
#elif defined(Q_OS_MACOS)
    QStringList escaped;for(auto s:cmd)escaped<<shellQuote(s);
    const QString script="on run argv\n tell application \"Terminal\"\n activate\n do script (item 1 of argv)\n end tell\n end run";
    success=QProcess::startDetached("/usr/bin/osascript",{"-e",script,escaped.join(' ')});
#else
    for(auto name:{"x-terminal-emulator","konsole","xterm","gnome-terminal","kitty","alacritty"}){
        auto exe=QStandardPaths::findExecutable(name);if(exe.isEmpty())continue;
        QStringList a={QString(name)=="gnome-terminal"?"--":"-e"};a+=cmd;
        if(QProcess::startDetached(exe,a)){success=true;break;}
    }
#endif
    say(success?"Interactive terminal opened.":"No supported native terminal found.");
}
void Controller::closeBrowser(){
    ++browseGeneration_;if(browser_){cancelProcess(browser_);browser_=nullptr;}
    files_.clear();fileStatus_.clear();emit filesChanged();
}
void Controller::browse(QString id,QString path){
    if(!validId(id)||context_.isEmpty())return;
    if(path.isEmpty())path="/";
    if(!path.startsWith('/')||path.contains(QChar::Null)){fileStatus_="Use an absolute Linux container path.";emit filesChanged();return;}
    // NUL separators preserve names with whitespace and newlines. Only metadata
    // leaves the container. No archive or file content is downloaded.
    closeBrowser();auto token=browseGeneration_;filePath_=path;
    fileStatus_="Loading directory…";emit filesChanged();
    const QString script="for x do if [ -d \"$x\" ]; then printf 'd\\000%s\\000' \"$x\"; else printf 'f\\000%s\\000' \"$x\"; fi; done";
    browser_=docker_.run({"exec",id,"find",path,"-mindepth","1","-maxdepth","1","-exec","sh","-c",script,"sh","{}","+"},[this,token](Result r){
        if(token!=browseGeneration_||closing_)return;
        browser_=nullptr;
        if(!r.ok){fileStatus_=r.error;emit filesChanged();return;}
        auto records=r.output.split('\0');
        QVariantList entries;
        for(qsizetype i=0;i+1<records.size()&&entries.size()<50000;i+=2){
            if(records[i]!="d"&&records[i]!="f")continue;
            auto path=QString::fromUtf8(records[i+1]);auto slash=path.lastIndexOf('/');
            entries.append(QVariantMap{{"path",path},{"name",path.mid(slash+1)},{"directory",records[i]=="d"}});
        }
        std::sort(entries.begin(),entries.end(),[](const QVariant& a,const QVariant& b){
            auto x=a.toMap(),y=b.toMap();if(x.value("directory")!=y.value("directory"))return x.value("directory").toBool();
            return x.value("name").toString().compare(y.value("name").toString(),Qt::CaseInsensitive)<0;
        });
        files_=entries;fileStatus_=QString::number(entries.size())+" entries"+(records.size()>100001?" (limited to 50,000)":"");emit filesChanged();
    },{}, {},30000);
}
