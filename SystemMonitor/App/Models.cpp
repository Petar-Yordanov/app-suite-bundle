#include "Models.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <iterator>
namespace {
QVariant val(sm::Number n){return n&&std::isfinite(*n)?QVariant(*n):QVariant();}
QString str(const std::string& s){return QString::fromStdString(s);}
QVariant value(const sm::Process& p,int r){
    switch(r){
        case ProcessModel::Pid:return QVariant::fromValue(qulonglong(p.pid));
        case ProcessModel::Identity:return QString::number(p.identity);
        case ProcessModel::Parent:return QVariant::fromValue(qulonglong(p.parent));
        case ProcessModel::Name:return str(p.name);
        case ProcessModel::User:return str(p.user);
        case ProcessModel::Cpu:return val(p.cpu);
        case ProcessModel::Memory:return val(p.memory);
        case ProcessModel::VirtualMemory:return val(p.virtualMemory);
        case ProcessModel::DiskRead:return val(p.read);
        case ProcessModel::DiskWrite:return val(p.write);
        case ProcessModel::NetworkUp:return val(p.upload);
        case ProcessModel::NetworkDown:return val(p.download);
        case ProcessModel::Threads:return val(p.threads);
        case ProcessModel::Uptime:return val(p.uptime);
        case ProcessModel::Path:return str(p.path);
        case ProcessModel::Command:return str(p.command);
        case ProcessModel::State:return str(p.state);
        default:return {};
    }
}
}
QHash<int,QByteArray> ProcessModel::roleNames()const{return {{Pid,"pid"},{Identity,"identity"},{Parent,"parentPid"},{Name,"processName"},{User,"user"},{Cpu,"cpu"},{Memory,"memory"},{VirtualMemory,"virtualMemory"},{DiskRead,"diskRead"},{DiskWrite,"diskWrite"},{NetworkUp,"networkUp"},{NetworkDown,"networkDown"},{Threads,"threads"},{Uptime,"uptime"},{Path,"path"},{Command,"command"},{State,"state"}};}
QVariant ProcessModel::data(const QModelIndex& i,int role)const{if(auto p=processAt(i.row());i.isValid()&&p)return value(*p,role);return {};}
QVariantMap ProcessModel::lookup(sm::U pid,QString identity)const{
    for(auto& p:rows_)if(p.pid==pid&&QString::number(p.identity)==identity){
        QVariantMap map;auto names=roleNames();for(auto i=names.begin();i!=names.end();++i)map[QString::fromLatin1(i.value())]=value(p,i.key());return map;
    }return {};
}
sm::Snapshot ProcessModel::treeSnapshot()const{
    sm::Snapshot snapshot;snapshot.processes.reserve(rows_.size());
    for(const auto& row:rows_){sm::Process p;p.pid=row.pid;p.parent=row.parent;p.identity=row.identity;snapshot.processes.push_back(std::move(p));}
    return snapshot;
}
void ProcessModel::apply(const std::vector<sm::Process>& current){
    std::unordered_map<sm::U,const sm::Process*> incoming;incoming.reserve(current.size());
    for(auto& p:current)incoming.emplace(p.pid,&p);
    auto stale=[&](int i){auto it=incoming.find(rows_[i].pid);return it==incoming.end()||it->second->identity!=rows_[i].identity;};
    // Preserve model indices for surviving process identities. Group removals.
    for(int i=int(rows_.size())-1;i>=0;){
        if(!stale(i)){--i;continue;}int end=i;while(i>=0&&stale(i))--i;
        beginRemoveRows({},i+1,end);rows_.erase(rows_.begin()+i+1,rows_.begin()+end+1);endRemoveRows();
    }
    std::unordered_set<sm::U> kept;kept.reserve(rows_.size());
    // Coalesce changed rows into ranges without resetting the model.
    int begin=-1,end=-1;
    for(int i=0;i<int(rows_.size());++i){
        auto& row=rows_[i];kept.insert(row.pid);const auto& next=*incoming.at(row.pid);
        if(row!=next){row=next;if(begin<0)begin=i;end=i;}
    }
    if(begin>=0)emit dataChanged(index(begin),index(end));
    std::vector<sm::Process> added;
    for(auto& p:current)if(!kept.contains(p.pid))added.push_back(p);
    if(!added.empty()){int start=int(rows_.size());beginInsertRows({},start,start+int(added.size())-1);rows_.insert(rows_.end(),std::make_move_iterator(added.begin()),std::make_move_iterator(added.end()));endInsertRows();}
}
void ProcessProxy::order(QString role,bool descending){
    auto names=sourceModel()->roleNames();for(auto i=names.begin();i!=names.end();++i)if(QString::fromLatin1(i.value())==role){setSortRole(i.key());sort(0,descending?Qt::DescendingOrder:Qt::AscendingOrder);return;}
}
bool ProcessProxy::filterAcceptsRow(int row,const QModelIndex& parent)const{
    if(text_.isEmpty())return true;auto i=sourceModel()->index(row,0,parent);
    for(auto role:{ProcessModel::Name,ProcessModel::Pid,ProcessModel::User,ProcessModel::Path,ProcessModel::Command})
        if(i.data(role).toString().contains(text_,Qt::CaseInsensitive))return true;
    return false;
}
bool ProcessProxy::lessThan(const QModelIndex& a,const QModelIndex& b)const{
    auto x=a.data(sortRole()),y=b.data(sortRole());
    if(x.isNull()!=y.isNull())return sortOrder()==Qt::AscendingOrder?!x.isNull():x.isNull();
    if(x.isNull())return a.data(ProcessModel::Pid).toULongLong()<b.data(ProcessModel::Pid).toULongLong();
    if(sortRole()==ProcessModel::Name||sortRole()==ProcessModel::User||sortRole()==ProcessModel::State||sortRole()==ProcessModel::Path||sortRole()==ProcessModel::Command)
        return QString::compare(x.toString(),y.toString(),Qt::CaseInsensitive)<0;
    return x.toDouble()<y.toDouble();
}
QVariant MetricsModel::data(const QModelIndex& i,int role)const{
    if(!i.isValid()||i.row()<0||size_t(i.row())>=rows_.size())return {};
    auto& m=rows_[i.row()];switch(role){case Label:return str(m.name);case Value:return val(m.value);case Unit:return str(m.unit);case Detail:return str(m.detail);default:return {};}
}
void MetricsModel::apply(const std::vector<sm::Metric>& metrics){
    bool same=metrics.size()==rows_.size();
    if(same)for(size_t i=0;i<rows_.size();++i)if(metrics[i].name!=rows_[i].name){same=false;break;}
    if(!same){beginResetModel();rows_=metrics;endResetModel();}
    else{rows_=metrics;if(!rows_.empty())emit dataChanged(index(0),index(int(rows_.size())-1));}
}
