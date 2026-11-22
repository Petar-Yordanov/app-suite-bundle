#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>
class ResourceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum { EntryRole=Qt::UserRole+1 };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& p={}) const override {return p.isValid()?0:rows_.size();}
    QVariant data(const QModelIndex& i,int role) const override {
        return i.isValid()&&i.row()>=0&&i.row()<rows_.size()&&role==EntryRole?QVariant(rows_[i.row()]):QVariant();
    }
    QHash<int,QByteArray> roleNames() const override {return {{EntryRole,"entry"}};}
    void update(QVector<QVariantMap> next) {
        if(next.isEmpty()){
            if(!rows_.isEmpty()){beginRemoveRows({},0,rows_.size()-1);rows_.clear();endRemoveRows();}
            return;
        }
        // Maintain stable order by ID across refreshes, avoid resetting delegates.
        QHash<QString,QVariantMap> incoming;
        for(const auto& row:next)incoming.insert(row.value("key").toString(),row);
        for(int i=rows_.size()-1;i>=0;--i){
            auto id=rows_[i].value("key").toString();
            if(!incoming.contains(id)){
                int first=i;
                while(first>0&&!incoming.contains(rows_[first-1].value("key").toString()))--first;
                beginRemoveRows({},first,i);rows_.remove(first,i-first+1);endRemoveRows();i=first;
            }
            else {
                auto row=incoming.take(id);
                if(rows_[i]!=row){rows_[i]=row;emit dataChanged(index(i),index(i),{EntryRole});}
            }
        }
        QVector<QVariantMap> extra;
        for(const auto& row:next)if(incoming.remove(row.value("key").toString()))extra.append(row);
        if(!extra.isEmpty()){auto n=rows_.size();beginInsertRows({},n,n+extra.size()-1);rows_+=extra;endInsertRows();}
    }
    Q_INVOKABLE QVariantMap at(int i) const {return i>=0&&i<rows_.size()?rows_[i]:QVariantMap{};}
private:
    QVector<QVariantMap> rows_;
};
