#pragma once
#include <QQuickItem>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include "../App/Browser.hpp"
#include "../App/Downloads.hpp"
class IOSView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(Tab* tab READ tab WRITE setTab NOTIFY changed)
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
public:
    explicit IOSView(QQuickItem* p=nullptr);
    ~IOSView() override;
    static void initialize(Browser*,Downloads*);
    static IOSView* lookup(QString);
    Tab* tab()const{return tab_;}
    void setTab(Tab*);
    bool shown()const{return shown_;}
    void setShown(bool);
    bool canGoBack()const;
    bool canGoForward()const;
    bool loading()const;
    int loadProgress()const;
    QString errorText()const{return error_;}
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void exitPageFullScreen(){}
    void report(bool committed,QString error={});
    void rebuild(void* configuration=nullptr);
    void* native() const;
    void sync();
signals:
    void changed();
protected:
    void geometryChange(const QRectF&,const QRectF&) override;
private:
    void load(QUrl);
    void configureRules();
    struct State;
    State* state_;
    QPointer<Tab> tab_;
    QTimer timer_;
    QString error_;
    bool shown_=false;
};
