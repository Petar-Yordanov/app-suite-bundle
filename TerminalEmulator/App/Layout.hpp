#pragma once
#include "Session.hpp"
#include <QObject>
#include <QPointer>
#include <QJsonObject>

class PaneNode:public QObject{
    Q_OBJECT
    Q_PROPERTY(Session* session READ session NOTIFY changed)
    Q_PROPERTY(PaneNode* first READ first NOTIFY changed)
    Q_PROPERTY(PaneNode* second READ second NOTIFY changed)
    Q_PROPERTY(bool leaf READ leaf NOTIFY changed)
    Q_PROPERTY(bool horizontal READ horizontal NOTIFY changed)
    Q_PROPERTY(double ratio READ ratio WRITE setRatio NOTIFY ratioChanged)
public:
    explicit PaneNode(Session* s,QObject* parent=nullptr):QObject(parent),session_(s){}
    Session* session()const{return session_;}
    PaneNode* first()const{return first_;}PaneNode* second()const{return second_;}
    bool leaf()const{return first_==nullptr;}
    bool horizontal()const{return horizontal_;}
    double ratio()const{return ratio_;}
    void setRatio(double);
    void split(Session*,bool);
    void attach(PaneNode*,PaneNode*,bool,double);
    bool remove(Session*);
    PaneNode* find(Session*);
    Session* firstSession()const;
    int count()const;
    int depth()const;
    QJsonObject serialize()const;
    void forEach(const std::function<void(Session*)>& fn)const;
signals:
    void changed();void ratioChanged();void layoutEdited();
private:
    void connectChild(PaneNode*);
    void absorb(PaneNode*);
    Session* session_=nullptr;
    PaneNode* first_=nullptr;PaneNode* second_=nullptr;
    bool horizontal_=true;double ratio_=0.5;
};
class TerminalTab:public QObject{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(PaneNode* root READ root CONSTANT)
public:
    TerminalTab(QString title,PaneNode* root,QObject* parent=nullptr):QObject(parent),title_(std::move(title)),root_(root){root_->setParent(this);}
    QString title()const{return title_;}
    void setTitle(QString);
    PaneNode* root()const{return root_;}
signals:
    void titleChanged();
private:
    QString title_;PaneNode* root_;
};
