#pragma once
#include "Player.hpp"
#include <QMainWindow>
#include <QQuickWidget>
#include <QShortcut>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <memory>

class MainWindow : public QMainWindow {
public:
    MainWindow(QString profile,bool forceSoftware=false,bool quiet=false);
    ~MainWindow() override;
    Player& player(){return *player_;}
    bool valid() const{return footer_&&footer_->status()==QQuickWidget::Ready;}
    bool panelValid() const{return sidebar_&&sidebar_->status()==QQuickWidget::Ready;}
protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void changeEvent(QEvent*) override;
private:
    VideoSurface* surface_=nullptr;
    std::unique_ptr<Player> player_;
    QQuickWidget *footer_=nullptr,*sidebar_=nullptr;
    QHBoxLayout* content_=nullptr;
    QList<QShortcut*> shortcuts_;
    QTimer fullTimer_;
    void rebind();
    void updateSidebar();
    void toggleFullscreen();
};
