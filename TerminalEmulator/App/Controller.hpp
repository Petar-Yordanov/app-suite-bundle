#pragma once
#include "Layout.hpp"
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QPointer>
#include <QTimer>
#include <QJsonArray>
#include <QProcess>

class Controller final:public QObject{
    Q_OBJECT
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY tabsChanged)
    Q_PROPERTY(PaneNode* currentRoot READ currentRoot NOTIFY tabsChanged)
    Q_PROPERTY(Session* active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList shells READ shells NOTIFY shellsChanged)
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit Controller(QString settingsPath,bool restore=true,QObject* parent=nullptr);
    ~Controller()override;
    QVariantList tabs()const;
    QVariantList shells()const;
    int currentIndex()const{return current_;}
    PaneNode* currentRoot()const;
    Session* active()const{return active_;}
    QVariantMap options()const{return options_;}
    QString status()const{return status_;}
    Q_INVOKABLE void newTab(QString shellId);
    Q_INVOKABLE void selectTab(int);
    Q_INVOKABLE void closeTab(int);
    Q_INVOKABLE void renameTab(int,QString);
    Q_INVOKABLE void moveTab(int,int);
    Q_INVOKABLE void duplicateTab(int);
    Q_INVOKABLE void split(bool horizontal,QString shellId);
    Q_INVOKABLE void closePane();
    Q_INVOKABLE void activate(Session*);
    Q_INVOKABLE void setOption(QString,QVariant);
    Q_INVOKABLE void refreshShells();
    Q_INVOKABLE void save();
    Q_INVOKABLE void cycleTab(int delta);
signals:
    void tabsChanged();void activeChanged();void shellsChanged();void optionsChanged();void statusChanged();void feedChanged();
private:
    static QVariantMap defaults();
    const Shell* shell(QString)const;
    Session* makeSession(QString id,QString cwd);
    PaneNode* build(const QJsonObject&,bool panes,int depth,int& remaining);
    void addTab(PaneNode*,QString,bool select=true);
    int paneCount()const;
    void queueSave();void say(QString);void rebalanceBudget();
    QString inherited(const Shell*)const;
    QSettings settings_;QVariantMap options_;
    QVector<Shell> shells_;QList<TerminalTab*> tabs_;
    int current_=-1;QPointer<Session> active_;QString status_;
    QTimer saveTimer_;QPointer<QProcess> wslQuery_;bool closing_=false;
};
