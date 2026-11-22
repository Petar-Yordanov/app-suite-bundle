#include "Controller.hpp"
#include <QFontDatabase>
#include <QDir>
#include <QKeySequence>
#include <QColor>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QFileInfo>
#include <QSet>

QVariantMap Controller::defaults(){
    return {{"theme","System"},{"font",QFontDatabase::systemFont(QFontDatabase::FixedFont).family()},
        {"fontSize",12},{"foreground","#d8dee9"},{"background","#171a20"},{"cursorColor","#a7c7f6"},{"cursor","Application"},{"blink",false},
        {"opacity",1.0},{"scrollback",10000},{"restoreTabs",true},{"restorePanes",true},{"inheritCwd",true},{"feed",""},
        {"newTabKey","Ctrl+Shift+T"},{"closePaneKey","Ctrl+Shift+W"},{"splitHKey","Ctrl+Shift+D"},{"splitVKey","Ctrl+Shift+E"},
        {"copyKey","Ctrl+Shift+C"},{"pasteKey","Ctrl+Shift+V"},{"findKey","Ctrl+Shift+F"},{"nextTabKey","Ctrl+Tab"},
        {"prevTabKey","Ctrl+Shift+Tab"},{"settingsKey","Ctrl+,"},{"duplicateKey","Ctrl+Shift+U"},{"renameKey","Ctrl+Shift+R"}};
}
Controller::Controller(QString path,bool restore,QObject* parent):QObject(parent),settings_(path,QSettings::IniFormat),options_(defaults()),shells_(detectShells()){
    for(auto key:options_.keys())if(settings_.contains("options/"+key))options_[key]=settings_.value("options/"+key);
    saveTimer_.setSingleShot(true);saveTimer_.setInterval(250);
    connect(&saveTimer_,&QTimer::timeout,this,&Controller::save);
    if(restore){
        auto saved=QJsonDocument::fromJson(settings_.value("layout").toByteArray()).object();
        auto entries=saved.value("tabs").toArray();auto index=qBound(0,saved.value("active").toInt(),qMax(0,int(entries.size())-1));
        bool restoreTabs=options_.value("restoreTabs").toBool(),restorePanes=options_.value("restorePanes").toBool();
        int remaining=64;
        if(restoreTabs||restorePanes)for(int i=0;i<entries.size()&&tabs_.size()<32&&remaining>0;++i){
            if(!restoreTabs&&i!=index)continue;
            auto item=entries[i].toObject();auto* root=build(item.value("root").toObject(),restorePanes,0,remaining);
            if(root)addTab(root,item.value("title").toString(),false);
        }
        if(!tabs_.isEmpty())selectTab(restoreTabs?qMin(index,int(tabs_.size())-1):0);
    }
    if(tabs_.isEmpty()&&restore)newTab({});
    if(shells_.isEmpty())say("No installed shells detected.");
}
Controller::~Controller(){closing_=true;saveTimer_.stop();}
void Controller::say(QString s){status_=std::move(s);emit statusChanged();}
QVariantList Controller::tabs()const{QVariantList list;for(auto* tab:tabs_)list.append(QVariant::fromValue(tab));return list;}
QVariantList Controller::shells()const{
    QVariantList list;for(auto& s:shells_)list.append(QVariantMap{{"id",s.id},{"name",s.name},{"executable",s.executable}});return list;
}
PaneNode* Controller::currentRoot()const{return current_>=0&&current_<tabs_.size()?tabs_[current_]->root():nullptr;}
const Shell* Controller::shell(QString id)const{
    for(auto& s:shells_)if(s.id==id)return &s;
    if(active_)for(auto& s:shells_)if(s.id==active_->shellId())return &s;
    return shells_.isEmpty()?nullptr:&shells_.first();
}
QString Controller::inherited(const Shell* target)const{
    if(!target||!active_||!options_.value("inheritCwd").toBool())return QDir::homePath();
    auto current=active_->shellId();
    if(current.startsWith("wsl:")||!target->distribution.isEmpty()){
        if(current!=target->id)return QDir::homePath();
    }
    return active_->cwd();
}
int Controller::paneCount()const{int count=0;for(auto t:tabs_)count+=t->root()->count();return count;}
void Controller::rebalanceBudget(){
    const auto bytes=size_t(128*1024*1024)/size_t(qMax(1,paneCount()));
    for(auto tab:tabs_)tab->root()->forEach([bytes](Session* s){s->setBudget(bytes);});
}
Session* Controller::makeSession(QString id,QString cwd){
    auto* s=shell(id);if(!s)return nullptr;
    auto* session=new Session(*s,cwd,this);session->apply(options_);return session;
}
PaneNode* Controller::build(const QJsonObject& o,bool panes,int depth,int& remaining){
    if(remaining<=0||depth>=12)return nullptr;
    if(o.contains("first")&&o.contains("second")){
        auto a=o.value("first").toObject(),b=o.value("second").toObject();
        auto* left=build(a,panes,depth+1,remaining);
        if(!left)return nullptr;
        if(!panes||remaining<=0)return left;
        auto* right=build(b,panes,depth+1,remaining);
        if(!right)return left;
        auto* root=new PaneNode(nullptr);root->attach(left,right,o.value("horizontal").toBool(true),o.value("ratio").toDouble(.5));return root;
    }
    auto* s=makeSession(o.value("shell").toString(),o.value("cwd").toString());
    if(!s)return nullptr;--remaining;return new PaneNode(s);
}
void Controller::addTab(PaneNode* root,QString title,bool select){
    if(!root)return;
    if(title.isEmpty())title=root->firstSession()->shellName();
    auto* tab=new TerminalTab(title.left(80),root,this);tabs_.append(tab);
    connect(root,&PaneNode::layoutEdited,this,&Controller::queueSave);
    connect(tab,&TerminalTab::titleChanged,this,&Controller::queueSave);
    if(select)selectTab(tabs_.size()-1);else emit tabsChanged();
    rebalanceBudget();
    queueSave();
}
void Controller::newTab(QString id){
    if(tabs_.size()>=32||paneCount()>=64){say("Tab or pane limit reached.");return;}
    auto* chosen=shell(id);if(!chosen){say("No installed shell found.");return;}
    auto* s=makeSession(chosen->id,inherited(chosen));if(s)addTab(new PaneNode(s),s->shellName());
}
void Controller::selectTab(int index){
    if(index<0||index>=tabs_.size())return;
    current_=index;
    if(!currentRoot()->find(active_))active_=currentRoot()->firstSession();
    emit tabsChanged();emit activeChanged();queueSave();
}
void Controller::closeTab(int index){
    if(index<0||index>=tabs_.size())return;
    auto* tab=tabs_.takeAt(index);
    tab->root()->forEach([this](Session* s){if(s==active_)active_=nullptr;s->deleteLater();});
    tab->deleteLater();
    if(tabs_.isEmpty()){current_=-1;emit tabsChanged();emit activeChanged();newTab({});}
    else selectTab(qBound(0,current_-(index<current_?1:0),int(tabs_.size())-1));
    rebalanceBudget();queueSave();
}
void Controller::renameTab(int index,QString name){if(index>=0&&index<tabs_.size())tabs_[index]->setTitle(name);}
void Controller::moveTab(int index,int to){
    if(index<0||index>=tabs_.size()||to<0||to>=tabs_.size()||index==to)return;
    auto* activeTab=tabs_[current_];tabs_.move(index,to);current_=tabs_.indexOf(activeTab);
    emit tabsChanged();queueSave();
}
void Controller::duplicateTab(int index){
    if(index<0||index>=tabs_.size()||tabs_.size()>=32)return;
    auto* tab=tabs_[index];int remaining=64-paneCount();
    if(remaining<tab->root()->count()){say("Not enough available panes to duplicate this tab.");return;}
    auto* root=build(tab->root()->serialize(),true,0,remaining);if(root)addTab(root,tab->title()+" copy");
}
void Controller::split(bool horizontal,QString id){
    auto* root=currentRoot();if(!root||!active_)return;
    if(paneCount()>=64||root->depth()>=12){say("Pane or nesting limit reached.");return;}
    auto* target=root->find(active_);auto* chosen=shell(id);
    if(!target||!chosen)return;
    auto* s=makeSession(chosen->id,inherited(chosen));if(!s)return;
    active_=s;target->split(s,horizontal);emit activeChanged();rebalanceBudget();queueSave();
}
void Controller::closePane(){
    auto* root=currentRoot();if(!root||!active_)return;
    if(root->leaf()){closeTab(current_);return;}
    auto* old=active_.data();
    if(root->remove(old)){active_=root->firstSession();emit activeChanged();old->deleteLater();rebalanceBudget();queueSave();}
}
void Controller::activate(Session* s){
    if(s&&s!=active_&&currentRoot()&&currentRoot()->find(s)){active_=s;emit activeChanged();}
}
void Controller::cycleTab(int delta){if(!tabs_.isEmpty())selectTab((current_+delta+tabs_.size())%tabs_.size());}
void Controller::queueSave(){if(!closing_)saveTimer_.start();}
void Controller::save(){
    QJsonArray entries;
    for(auto* t:tabs_)entries.append(QJsonObject{{"title",t->title()},{"root",t->root()->serialize()}});
    settings_.setValue("layout",QJsonDocument(QJsonObject{{"tabs",entries},{"active",current_}}).toJson(QJsonDocument::Compact));
    settings_.sync();
}
void Controller::setOption(QString key,QVariant value){
    if(!options_.contains(key))return;
    if(key=="scrollback")value=qBound(0,value.toInt(),100000);
    else if(key=="fontSize")value=qBound(8,value.toInt(),40);
    else if(key=="opacity")value=qBound(.25,value.toDouble(),1.0);
    else if(key=="foreground"||key=="background"||key=="cursorColor"){
        QColor color(value.toString());if(!color.isValid()){say("Invalid color.");return;}value=color.name();
    }else if(key.endsWith("Key")){
        QKeySequence seq(value.toString());if(seq.isEmpty()){say("Invalid shortcut.");return;}
        value=seq.toString(QKeySequence::PortableText);
        for(auto k:options_.keys())if(k!=key&&k.endsWith("Key")&&options_[k]==value){say("Shortcut already in use.");return;}
    }else if(key=="theme"&&!QStringList{"Light","Dark","System"}.contains(value.toString()))return;
    else if(key=="cursor"&&!QStringList{"Application","Block","Underline","Beam"}.contains(value.toString()))return;
    if(options_[key]==value)return;
    options_[key]=value;settings_.setValue("options/"+key,value);
    for(auto* tab:tabs_)tab->root()->forEach([this](Session* s){s->apply(options_);});
    emit optionsChanged();if(key=="feed")emit feedChanged();queueSave();
}
void Controller::refreshShells(){
    shells_=detectShells();emit shellsChanged();
#ifdef Q_OS_WIN
    auto wsl=QStandardPaths::findExecutable("wsl.exe");if(wsl.isEmpty()||wslQuery_)return;
    auto* proc=new QProcess(this);wslQuery_=proc;
    connect(proc,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,proc,wsl](int code,QProcess::ExitStatus status){
        if(code==0&&status==QProcess::NormalExit){
            auto found=wslShells(proc->readAllStandardOutput(),wsl);
            for(auto& s:found){bool exists=false;for(auto& old:shells_)if(old.id==s.id)exists=true;if(!exists)shells_.append(s);}
            emit shellsChanged();
        }
        wslQuery_=nullptr;proc->deleteLater();
    });
    connect(proc,&QProcess::errorOccurred,this,[this,proc](QProcess::ProcessError e){if(e==QProcess::FailedToStart){wslQuery_=nullptr;proc->deleteLater();}});
    QTimer::singleShot(3500,proc,[proc]{if(proc->state()!=QProcess::NotRunning)proc->kill();});
    proc->start(wsl,{"--list","--quiet"});
#endif
}
