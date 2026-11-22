#include "MainWindow.hpp"
#include <QQmlContext>
#include <QQmlError>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeySequence>
#include <QDebug>

namespace {
QQuickWidget* quickWidget(QWidget* parent,Player* player,const QString& source){
    auto* q=new QQuickWidget(parent);q->setResizeMode(QQuickWidget::SizeRootObjectToView);
    q->rootContext()->setContextProperty("player",player);
    q->setSource(QUrl("qrc:/UI/"+source+".qml"));
    if(q->status()==QQuickWidget::Error)for(const auto& e:q->errors())qWarning().noquote()<<e.toString();
    return q;
}
}
MainWindow::MainWindow(QString profile,bool forceSoftware,bool quiet){
    resize(1000,650);setMinimumSize(760,300);setAcceptDrops(true);setWindowTitle("Media Player");
    auto* central=new QWidget(this);setCentralWidget(central);
    auto* layout=new QVBoxLayout(central);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    content_=new QHBoxLayout();content_->setContentsMargins(0,0,0,0);content_->setSpacing(0);layout->addLayout(content_,1);
    surface_=new VideoSurface(forceSoftware,central);content_->addWidget(surface_,1);
    player_=std::make_unique<Player>(std::move(profile),surface_,this,quiet);
    footer_=quickWidget(central,player_.get(),"Controls");footer_->setFixedHeight(118);layout->addWidget(footer_);
    connect(player_.get(),&Player::stateChanged,this,[this]{setWindowTitle(player_->title()+" - Media Player");});
    connect(player_.get(),&Player::panelChanged,this,&MainWindow::updateSidebar);
    connect(player_.get(),&Player::shortcutsChanged,this,&MainWindow::rebind);
    connect(player_.get(),&Player::requestFullscreen,this,&MainWindow::toggleFullscreen);
    fullTimer_.setInterval(150);fullTimer_.setTimerType(Qt::CoarseTimer);
    connect(&fullTimer_,&QTimer::timeout,this,[this]{
        const auto cursor=mapFromGlobal(QCursor::pos());
        bool show=!player_->panel().isEmpty()||(rect().contains(cursor)&&cursor.y()>=height()-footer_->height()-15);
        if(footer_->isVisible()!=show)footer_->setVisible(show);
    });
    rebind();
}
MainWindow::~MainWindow(){
    fullTimer_.stop();
    // Destroy QML bindings before their context object, but release libVLC before
    // the native video surface is destroyed.
    delete sidebar_;sidebar_=nullptr;delete footer_;footer_=nullptr;player_.reset();
}
void MainWindow::rebind(){
    for(auto* s:shortcuts_){s->setEnabled(false);s->deleteLater();}shortcuts_.clear();
    const auto keys=player_->shortcuts();
    for(auto it=keys.cbegin();it!=keys.cend();++it){
        auto* shortcut=new QShortcut(QKeySequence::fromString(it.value(),QKeySequence::PortableText),this);
        shortcut->setContext(Qt::WindowShortcut);shortcut->setAutoRepeat(it.key()=="back"||it.key()=="forward"||it.key()=="volumeUp"||it.key()=="volumeDown");
        connect(shortcut,&QShortcut::activated,this,[this,action=it.key()]{player_->dispatch(action);});shortcuts_.append(shortcut);
    }
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);escape->setContext(Qt::WindowShortcut);
    connect(escape,&QShortcut::activated,this,[this]{if(isFullScreen())toggleFullscreen();else if(!player_->panel().isEmpty())player_->setPanel("");});shortcuts_.append(escape);
}
void MainWindow::updateSidebar(){
    bool show=!player_->panel().isEmpty();
    if(show&&!sidebar_){
        sidebar_=quickWidget(centralWidget(),player_.get(),"Sidebar");sidebar_->setFixedWidth(290);content_->addWidget(sidebar_);
    }
    if(sidebar_)sidebar_->setVisible(show);
}
void MainWindow::toggleFullscreen(){
    if(isFullScreen()){showNormal();return;}
    if(!player_->panel().isEmpty())player_->setPanel("");
    showFullScreen();
}
void MainWindow::changeEvent(QEvent* event){
    QMainWindow::changeEvent(event);
    if(event->type()==QEvent::WindowStateChange&&player_){
        bool full=isFullScreen();player_->setFullscreenState(full);
        if(full){fullTimer_.start();footer_->hide();}else{fullTimer_.stop();footer_->show();}
    }
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event){
    if(event->mimeData()->hasUrls())for(const auto& url:event->mimeData()->urls())if(!media::localVideo(url).isEmpty()){event->acceptProposedAction();return;}
}
void MainWindow::dropEvent(QDropEvent* event){if(event->mimeData()->hasUrls()){player_->openUrls(event->mimeData()->urls());event->acceptProposedAction();}}
