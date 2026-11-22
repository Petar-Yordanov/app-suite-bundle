#pragma once
#include "../Core/Monitor.hpp"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QVariantMap>

class ProcessModel:public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {Pid=Qt::UserRole+1,Identity,Parent,Name,User,Cpu,Memory,VirtualMemory,DiskRead,DiskWrite,NetworkUp,NetworkDown,Threads,Uptime,Path,Command,State};
    explicit ProcessModel(QObject* parent=nullptr):QAbstractListModel(parent){}
    int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(rows_.size());}
    QVariant data(const QModelIndex&,int role)const override;
    QHash<int,QByteArray> roleNames()const override;
    void apply(const std::vector<sm::Process>&);
    QVariantMap lookup(sm::U pid,QString identity)const;
    sm::Snapshot treeSnapshot()const;
    const sm::Process* processAt(int row)const{return row>=0&&size_t(row)<rows_.size()?&rows_[row]:nullptr;}
private:
    std::vector<sm::Process> rows_;
};
class ProcessProxy:public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ProcessProxy(QObject* p=nullptr):QSortFilterProxyModel(p){setDynamicSortFilter(true);setSortRole(ProcessModel::Cpu);sort(0,Qt::DescendingOrder);}
    Q_INVOKABLE void query(QString text){if(text_!=text){text_=std::move(text);invalidateFilter();}}
    Q_INVOKABLE void order(QString role,bool descending);
protected:
    bool filterAcceptsRow(int,const QModelIndex&)const override;
    bool lessThan(const QModelIndex&,const QModelIndex&)const override;
private:QString text_;
};
class MetricsModel:public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {Label=Qt::UserRole+1,Value,Unit,Detail};
    explicit MetricsModel(QObject* parent=nullptr):QAbstractListModel(parent){}
    int rowCount(const QModelIndex& p={})const override{return p.isValid()?0:int(rows_.size());}
    QVariant data(const QModelIndex&,int role)const override;
    QHash<int,QByteArray> roleNames()const override{return {{Label,"label"},{Value,"value"},{Unit,"unit"},{Detail,"detail"}};}
    void apply(const std::vector<sm::Metric>&);
private:std::vector<sm::Metric> rows_;
};
