#pragma once
#include <QQuickPaintedItem>
#include <QPointer>
#include <QFont>
#include <QTimer>
#include <QVariantMap>
#include "Session.hpp"

class TerminalItem:public QQuickPaintedItem{
    Q_OBJECT
    Q_PROPERTY(Session* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap options READ options WRITE setOptions NOTIFY optionsChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
public:
    explicit TerminalItem(QQuickItem* parent=nullptr);
    Session* session()const{return session_;}
    void setSession(Session*);
    QVariantMap options()const{return options_;}void setOptions(QVariantMap);
    bool hasSelection()const{return selected_;}
    void paint(QPainter*)override;
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void send(QString text);
signals:
    void sessionChanged();void optionsChanged();void selectionChanged();
    void activated();
protected:
    void geometryChange(const QRectF&,const QRectF&)override;
    void keyPressEvent(QKeyEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseDoubleClickEvent(QMouseEvent*)override;
    void hoverMoveEvent(QHoverEvent*)override;
    void wheelEvent(QWheelEvent*)override;
    void focusInEvent(QFocusEvent*)override;
    void focusOutEvent(QFocusEvent*)override;
    void inputMethodEvent(QInputMethodEvent*)override;
    QVariant inputMethodQuery(Qt::InputMethodQuery)const override;
private:
    struct Pos{qint64 row=0;int col=0;};
    static bool less(Pos a,Pos b){return a.row<b.row||(a.row==b.row&&a.col<b.col);}
    Pos position(QPointF)const;
    bool highlight(qint64,int)const;
    bool reportMouse(Qt::KeyboardModifiers)const;
    bool openLink(Pos);
    void updateGeometry();void updateBlink();void refresh();
    QString selection()const;
    static VTermModifier modifiers(Qt::KeyboardModifiers);
    QPointer<Session> session_;
    QVariantMap options_;QFont font_;
    qreal cellW_=9,cellH_=18,baseline_=14;
    QTimer resizeTimer_,blinkTimer_;
    bool blinkOn_=true,selected_=false,dragging_=false,mouseSent_=false,previousAlt_=false;
    int mouseButton_=0,wheelRemainder_=0;
    Pos anchor_,end_;
    QString preedit_;
};
