#include "TerminalItem.hpp"
#include <QPainter>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QUrl>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QHoverEvent>
#include <QTextBoundaryFinder>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace {constexpr qreal padding=5;}
TerminalItem::TerminalItem(QQuickItem* parent):QQuickPaintedItem(parent){
    setAcceptedMouseButtons(Qt::AllButtons);setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod,true);setActiveFocusOnTab(false);
    setAntialiasing(false);setOpaquePainting(false);setClip(true);
    font_=QFontDatabase::systemFont(QFontDatabase::FixedFont);font_.setPointSize(12);font_.setKerning(false);
    resizeTimer_.setSingleShot(true);resizeTimer_.setInterval(40);
    connect(&resizeTimer_,&QTimer::timeout,this,[this]{
        if(session_&&width()>0&&height()>0)session_->resize(int((height()-2*padding)/cellH_),int((width()-2*padding)/cellW_));
        update();
    });
    blinkTimer_.setInterval(500);
    connect(&blinkTimer_,&QTimer::timeout,this,[this]{blinkOn_=!blinkOn_;update();});
    connect(this,&QQuickItem::visibleChanged,this,&TerminalItem::updateBlink);
    updateGeometry();
}
void TerminalItem::setSession(Session* s){
    if(session_==s)return;
    if(session_)disconnect(session_,nullptr,this,nullptr);
    session_=s;clearSelection();
    if(s){previousAlt_=s->emulator().alternate();connect(s,&Session::screenChanged,this,&TerminalItem::refresh);connect(s,&Session::statusChanged,this,&TerminalItem::updateBlink);}
    updateGeometry();updateBlink();emit sessionChanged();update();
}
void TerminalItem::setOptions(QVariantMap o){
    if(options_==o)return;options_=std::move(o);
    font_=QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto family=options_.value("font").toString();if(!family.isEmpty())font_.setFamily(family);
    font_.setPointSize(qBound(8,options_.value("fontSize",12).toInt(),40));
    font_.setFixedPitch(true);font_.setStyleHint(QFont::Monospace);font_.setKerning(false);
    updateGeometry();updateBlink();emit optionsChanged();update();
}
void TerminalItem::updateGeometry(){
    QFontMetricsF m(font_);cellW_=qMax<qreal>(1,m.horizontalAdvance('M'));
    cellH_=qMax<qreal>(1,std::ceil(m.height()));baseline_=m.ascent();resizeTimer_.start();
}
void TerminalItem::geometryChange(const QRectF& a,const QRectF& b){QQuickPaintedItem::geometryChange(a,b);if(a.size()!=b.size())resizeTimer_.start();}
void TerminalItem::updateBlink(){
    bool enabled=options_.value("blink",false).toBool()&&hasActiveFocus()&&isVisible()&&session_&&session_->running();
    blinkOn_=true;if(enabled){if(!blinkTimer_.isActive())blinkTimer_.start();}else blinkTimer_.stop();update();
}
void TerminalItem::refresh(){
    if(!session_)return;
    auto& emu=session_->emulator();
    if(previousAlt_!=emu.alternate()){previousAlt_=emu.alternate();clearSelection();}
    if(selected_&&(std::min(anchor_.row,end_.row)<qint64(emu.historyBase())))clearSelection();
    if(isVisible())update();
}
bool TerminalItem::highlight(qint64 row,int col)const{
    if(!selected_)return false;
    Pos a=anchor_,b=end_;if(less(b,a))std::swap(a,b);
    Pos p{row,col};return !less(p,a)&&!less(b,p);
}
void TerminalItem::paint(QPainter* p){
    QColor defaultBg(options_.value("background","#171a20").toString()),defaultFg(options_.value("foreground","#d8dee9").toString());
    if(!defaultBg.isValid())defaultBg="#171a20";if(!defaultFg.isValid())defaultFg="#d8dee9";
    defaultBg.setAlphaF(qBound(0.25,options_.value("opacity",1.0).toDouble(),1.0));
    p->setCompositionMode(QPainter::CompositionMode_Source);p->fillRect(boundingRect(),defaultBg);
    p->setCompositionMode(QPainter::CompositionMode_SourceOver);p->setRenderHint(QPainter::TextAntialiasing,true);
    if(!session_)return;
    const auto& emu=session_->emulator();
    const int top=session_->top(),rows=emu.rows(),cols=emu.cols();
    const auto base=qint64(emu.historyBase());
    p->setClipRect(boundingRect());
    struct Style {QColor fg,bg;bool bold=false,italic=false,under=false,strike=false,conceal=false;bool operator==(const Style&)const=default;};
    auto style=[&](const term::Cell& cell,int r,int c){
        const auto& v=cell.value;
        QColor fg=VTERM_COLOR_IS_DEFAULT_FG(&v.fg)?defaultFg:QColor::fromRgb(emu.color(v.fg));
        QColor bg=VTERM_COLOR_IS_DEFAULT_BG(&v.bg)?defaultBg:QColor::fromRgb(emu.color(v.bg));
        if(v.attrs.reverse)std::swap(fg,bg);
        if(highlight(base+r,c)){bg=QColor("#3a6296");fg=Qt::white;}
        else if(r==session_->matchRow()&&c>=session_->matchStart()&&c<session_->matchEnd()){bg=QColor("#d3a24a");fg=QColor("#171a20");}
        return Style{fg,bg,bool(v.attrs.bold),bool(v.attrs.italic),bool(v.attrs.underline||cell.link),bool(v.attrs.strike),bool(v.attrs.conceal)};
    };
    QFont lastFont;bool haveFont=false;
    // Metrics depend only on the bold/italic face, not colors or decorations.
    // Compute each face once per paint rather than once per colored text run.
    std::array<bool,4> gridFace{};
    for(int i=0;i<4;++i){QFont f=font_;f.setBold(i&1);f.setItalic(i&2);gridFace[i]=std::abs(QFontMetricsF(f).horizontalAdvance('M')-cellW_)<0.03;}
    std::vector<Style> styles;styles.reserve(cols);
    for(int row=0;row<rows;++row){
        auto cells=emu.line(size_t(top+row));
        auto y=padding+row*cellH_;
        int limit=std::min(cols,int(cells.size()));
        styles.clear();for(int c=0;c<limit;++c)styles.push_back(style(cells[c],top+row,c));
        // Draw background runs (including empty cells with ANSI background colors).
        for(int i=0;i<limit;){
            auto bg=styles[i].bg;int end=i+1;
            while(end<limit&&styles[end].bg==bg)++end;
            if(bg!=defaultBg)p->fillRect(QRectF(padding+i*cellW_,y,(end-i)*cellW_,cellH_),bg);
            i=end;
        }
        // Batch contiguous ASCII cells of identical style; Unicode grapheme
        // clusters are placed in their exact terminal cells with fallback fonts.
        for(int i=0;i<limit;){
            auto& v=cells[i].value;
            if(term::Emulator::continuation(v)){++i;continue;}
            const auto& st=styles[i];
            auto text=Session::cellText(v);
            int end=std::min(limit,i+std::max(1,int(v.width)));
            bool ascii=v.width==1&&v.chars[0]<128&&!v.chars[1];
            if(ascii){
                // Limit each shaping run; spaces and ASCII share the fixed grid.
                while(end<limit&&end-i<128){
                    auto& next=cells[end].value;
                    if(next.width!=1||next.chars[0]>=128||next.chars[1]||!(styles[end]==st))break;
                    text+=Session::cellText(next);++end;
                }
            }else{
                // Some VT engines split an emoji ZWJ/flag sequence over cells.
                // Shape the cluster together, without changing the terminal grid.
                while(end<limit&&end-i<8&&text.size()<64){
                    const auto& next=cells[end].value;
                    if(term::Emulator::continuation(next)||!(styles[end]==st))break;
                    auto suffix=Session::cellText(next);
                    if(suffix.isEmpty()||text.size()+suffix.size()>64)break;
                    auto joined=text+suffix;
                    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,joined);
                    finder.setPosition(text.size());if(finder.isAtBoundary())break;
                    text=std::move(joined);end=std::min(limit,end+std::max(1,int(next.width)));
                }
            }
            if(!st.conceal&&!text.trimmed().isEmpty()){
                QFont f=font_;f.setBold(st.bold);f.setItalic(st.italic);f.setUnderline(st.under);f.setStrikeOut(st.strike);
                if(!haveFont||f!=lastFont){p->setFont(f);lastFont=f;haveFont=true;}
                p->setPen(st.fg);
                // Explicit cell positions avoid proportional fallback glyphs moving later cells.
                if(!ascii){
                    p->save();p->setClipRect(QRectF(padding+i*cellW_,y,(end-i)*cellW_,cellH_),Qt::IntersectClip);
                    p->drawText(QPointF(padding+i*cellW_,y+baseline_),text);p->restore();
                }else{
                    // QPainter text runs use font advance; use individual glyph positions
                    // if the bold/italic face has a non-grid advance.
                    if(gridFace[int(st.bold)|int(st.italic)*2])
                        p->drawText(QPointF(padding+i*cellW_,y+baseline_),text);
                    else for(int c=i;c<end;++c)p->drawText(QPointF(padding+c*cellW_,y+baseline_),Session::cellText(cells[c].value));
                }
            }
            i=end;
        }
    }
    if(!session_->offset()&&emu.cursorVisible()&&hasActiveFocus()&&blinkOn_){
        auto cur=emu.cursor();auto x=padding+cur.col*cellW_,y=padding+cur.row*cellH_;
        auto shape=options_.value("cursor","Application").toString();
        if(shape=="Application")shape=emu.cursorShape()==2?"Underline":emu.cursorShape()==3?"Beam":"Block";
        QColor cursor(options_.value("cursorColor","#a7c7f6").toString());if(!cursor.isValid())cursor="#a7c7f6";
        if(shape=="Underline")p->fillRect(QRectF(x,y+cellH_-2,cellW_,2),cursor);
        else if(shape=="Beam")p->fillRect(QRectF(x,y,2,cellH_),cursor);
        else{cursor.setAlpha(125);p->fillRect(QRectF(x,y,cellW_,cellH_),cursor);}
        if(!preedit_.isEmpty()){p->setFont(font_);p->setPen(defaultFg);p->drawText(QPointF(x,y+baseline_),preedit_);}
    }
}
VTermModifier TerminalItem::modifiers(Qt::KeyboardModifiers m){
    int result=0;
    if(m&Qt::ShiftModifier)result|=VTERM_MOD_SHIFT;
    if(m&Qt::AltModifier)result|=VTERM_MOD_ALT;
#ifdef Q_OS_MACOS
    if(m&Qt::MetaModifier)result|=VTERM_MOD_CTRL; // physical Control
#else
    if(m&Qt::ControlModifier)result|=VTERM_MOD_CTRL;
#endif
    return VTermModifier(result);
}
void TerminalItem::keyPressEvent(QKeyEvent* e){
    if(!session_){e->ignore();return;}
    auto m=modifiers(e->modifiers());auto key=e->key();
    if(key==Qt::Key_PageUp&&(e->modifiers()&Qt::ShiftModifier)){session_->scroll(session_->emulator().rows());e->accept();return;}
    if(key==Qt::Key_PageDown&&(e->modifiers()&Qt::ShiftModifier)){session_->scroll(-session_->emulator().rows());e->accept();return;}
    VTermKey v=VTERM_KEY_NONE;
    switch(key){
    case Qt::Key_Return:case Qt::Key_Enter:v=VTERM_KEY_ENTER;break;
    case Qt::Key_Tab:case Qt::Key_Backtab:v=VTERM_KEY_TAB;if(key==Qt::Key_Backtab)m=VTermModifier(m|VTERM_MOD_SHIFT);break;
    case Qt::Key_Backspace:v=VTERM_KEY_BACKSPACE;break;
    case Qt::Key_Escape:v=VTERM_KEY_ESCAPE;break;
    case Qt::Key_Up:v=VTERM_KEY_UP;break;case Qt::Key_Down:v=VTERM_KEY_DOWN;break;
    case Qt::Key_Left:v=VTERM_KEY_LEFT;break;case Qt::Key_Right:v=VTERM_KEY_RIGHT;break;
    case Qt::Key_Insert:v=VTERM_KEY_INS;break;case Qt::Key_Delete:v=VTERM_KEY_DEL;break;
    case Qt::Key_Home:v=VTERM_KEY_HOME;break;case Qt::Key_End:v=VTERM_KEY_END;break;
    case Qt::Key_PageUp:v=VTERM_KEY_PAGEUP;break;case Qt::Key_PageDown:v=VTERM_KEY_PAGEDOWN;break;
    default:if(key>=Qt::Key_F1&&key<=Qt::Key_F35)v=static_cast<VTermKey>(VTERM_KEY_FUNCTION(key-Qt::Key_F1+1));break;
    }
    clearSelection();
    if(v!=VTERM_KEY_NONE){session_->key(v,m);e->accept();return;}
    if((m&VTERM_MOD_CTRL)&&key>=Qt::Key_A&&key<=Qt::Key_Z){
        session_->character(uint32_t('a'+key-Qt::Key_A),m);e->accept();return;
    }
    if((m&VTERM_MOD_CTRL)&&key==Qt::Key_Space){session_->character(' ',m);e->accept();return;}
    if(!e->text().isEmpty()){
        auto text=e->text().toUcs4();
        auto mods=VTermModifier(m&~VTERM_MOD_SHIFT);
        for(auto cp:text){
            if(cp<32&&!(m&VTERM_MOD_ALT))session_->character(cp,VTERM_MOD_NONE);
            else session_->character(cp,mods);
        }
        e->accept();return;
    }
    e->ignore();
}
TerminalItem::Pos TerminalItem::position(QPointF p)const{
    if(!session_)return {};
    auto& emu=session_->emulator();
    int r=qBound(0,int(std::floor((p.y()-padding)/cellH_)),emu.rows()-1);
    int c=qBound(0,int(std::floor((p.x()-padding)/cellW_)),emu.cols()-1);
    return {qint64(emu.historyBase())+session_->top()+r,c};
}
bool TerminalItem::reportMouse(Qt::KeyboardModifiers m)const{return session_&&!session_->offset()&&session_->emulator().mouseMode()!=0&&!(m&Qt::ShiftModifier);}
bool TerminalItem::openLink(Pos pos){
    auto& emu=session_->emulator();auto row=emu.line(size_t(pos.row-qint64(emu.historyBase())));
    if(pos.col>=int(row.size()))return false;
    auto id=row[pos.col].link;QString uri=QString::fromStdString(emu.hyperlink(id));
    if(uri.isEmpty()){
        QVector<int> cols;auto text=Session::lineText(row,&cols);
        static const QRegularExpression regex(R"(https?://[^\s<>"']+)");
        auto i=regex.globalMatch(text);
        while(i.hasNext()){
            auto m=i.next();auto a=m.capturedStart(),b=m.capturedEnd()-1;
            if(a>=0&&b<cols.size()&&cols[a]<=pos.col&&cols[b]>=pos.col){uri=m.captured();while(uri.endsWith('.')||uri.endsWith(',')||uri.endsWith(')')||uri.endsWith(';'))uri.chop(1);break;}
        }
    }
    QUrl url(uri);if(!url.isValid()||url.host().isEmpty()||!url.userInfo().isEmpty()||(url.scheme()!="http"&&url.scheme()!="https"))return false;
    return QDesktopServices::openUrl(url);
}
void TerminalItem::mousePressEvent(QMouseEvent* e){
    forceActiveFocus();emit activated();
    if(!session_){e->ignore();return;}
    auto pos=position(e->position());
    if(e->button()==Qt::LeftButton&&(e->modifiers()&Qt::ControlModifier)&&openLink(pos)){e->accept();return;}
    if(reportMouse(e->modifiers())){
        mouseButton_=e->button()==Qt::LeftButton?1:e->button()==Qt::MiddleButton?2:3;mouseSent_=true;
        auto& emu=session_->emulator();emu.mouseMove(int(pos.row-qint64(emu.historyBase())-session_->history()),pos.col,modifiers(e->modifiers()));
        emu.mouseButton(mouseButton_,true,modifiers(e->modifiers()));e->accept();return;
    }
    if(e->button()==Qt::LeftButton){anchor_=end_=pos;dragging_=true;selected_=false;emit selectionChanged();update();}
    else if(e->button()==Qt::MiddleButton)paste();
    e->accept();
}
void TerminalItem::mouseMoveEvent(QMouseEvent* e){
    if(!session_)return;
    if(mouseSent_){
        auto pos=position(e->position());auto& emu=session_->emulator();
        emu.mouseMove(int(pos.row-qint64(emu.historyBase())-session_->history()),pos.col,modifiers(e->modifiers()));
    }else if(dragging_){
        if(e->position().y()<0)session_->scroll(1);else if(e->position().y()>height())session_->scroll(-1);
        end_=position(e->position());selected_=true;emit selectionChanged();update();
    }
    e->accept();
}
void TerminalItem::mouseReleaseEvent(QMouseEvent* e){
    if(session_&&mouseSent_){session_->emulator().mouseButton(mouseButton_,false,modifiers(e->modifiers()));mouseSent_=false;}
    dragging_=false;e->accept();
}
void TerminalItem::hoverMoveEvent(QHoverEvent* e){
    if(session_&&reportMouse(e->modifiers())){
        auto pos=position(e->position());auto& emu=session_->emulator();
        emu.mouseMove(int(pos.row-qint64(emu.historyBase())-session_->history()),pos.col,modifiers(e->modifiers()));
    }
}
void TerminalItem::mouseDoubleClickEvent(QMouseEvent* e){
    if(!session_||e->button()!=Qt::LeftButton||reportMouse(e->modifiers()))return;
    auto pos=position(e->position());auto& emu=session_->emulator();
    auto row=emu.line(size_t(pos.row-qint64(emu.historyBase())));
    if(pos.col>=int(row.size()))return;
    auto word=[&](int c){auto t=Session::cellText(row[c].value);return !t.isEmpty()&&!t.at(0).isSpace();};
    anchor_=end_=pos;
    while(anchor_.col>0&&word(anchor_.col-1))--anchor_.col;
    while(end_.col+1<int(row.size())&&word(end_.col+1))++end_.col;
    selected_=true;emit selectionChanged();update();e->accept();
}
void TerminalItem::wheelEvent(QWheelEvent* e){
    if(!session_)return;
    wheelRemainder_+=e->angleDelta().y();int steps=wheelRemainder_/120;wheelRemainder_%=120;
    if(!steps&&!e->pixelDelta().isNull())steps=e->pixelDelta().y()>0?1:-1;
    if(reportMouse(e->modifiers())){
        auto& emu=session_->emulator();auto pos=position(e->position());
        emu.mouseMove(int(pos.row-qint64(emu.historyBase())-session_->history()),pos.col,modifiers(e->modifiers()));
        for(int n=0;n<qMin(10,std::abs(steps));++n){int b=steps>0?4:5;emu.mouseButton(b,true,modifiers(e->modifiers()));emu.mouseButton(b,false,modifiers(e->modifiers()));}
    }else session_->scroll(steps*3);
    e->accept();
}
QString TerminalItem::selection()const{
    if(!session_||!selected_)return {};
    auto& emu=session_->emulator();auto base=qint64(emu.historyBase());
    Pos a=anchor_,b=end_;if(less(b,a))std::swap(a,b);
    if(a.row<base||b.row>=base+session_->history()+emu.rows())return {};
    QString result;
    for(auto row=a.row;row<=b.row;++row){
        auto cells=emu.line(size_t(row-base));QString line;
        int start=row==a.row?a.col:0,end=row==b.row?b.col:int(cells.size())-1;
        for(int c=start;c<=end&&c<int(cells.size());++c)line+=Session::cellText(cells[c].value);
        while(line.endsWith(' '))line.chop(1);result+=line;
        if(row!=b.row)result+='\n';
        if(result.size()>4*1024*1024)break;
    }
    return result;
}
void TerminalItem::copy(){auto s=selection();if(!s.isEmpty())QGuiApplication::clipboard()->setText(s);}
void TerminalItem::paste(){if(session_)session_->sendText(QGuiApplication::clipboard()->text());clearSelection();}
void TerminalItem::selectAll(){
    if(!session_)return;auto& emu=session_->emulator();auto base=qint64(emu.historyBase());
    anchor_={base,0};end_={base+session_->history()+emu.rows()-1,emu.cols()-1};selected_=true;emit selectionChanged();update();
}
void TerminalItem::clearSelection(){if(selected_){selected_=false;emit selectionChanged();update();}}
void TerminalItem::send(QString text){if(session_)session_->sendText(text);}
void TerminalItem::focusInEvent(QFocusEvent* e){if(session_)session_->inputFocus(true);updateBlink();emit activated();QQuickPaintedItem::focusInEvent(e);}
void TerminalItem::focusOutEvent(QFocusEvent* e){if(session_)session_->inputFocus(false);preedit_.clear();updateBlink();QQuickPaintedItem::focusOutEvent(e);}
void TerminalItem::inputMethodEvent(QInputMethodEvent* e){
    preedit_=e->preeditString();
    if(session_&&!e->commitString().isEmpty())for(auto cp:e->commitString().toUcs4())session_->character(cp,VTERM_MOD_NONE);
    e->accept();update();
}
QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery query)const{
    if(query==Qt::ImEnabled)return true;
    if(query==Qt::ImCursorRectangle&&session_){auto c=session_->emulator().cursor();return QRectF(padding+c.col*cellW_,padding+c.row*cellH_,cellW_,cellH_);}
    if(query==Qt::ImSurroundingText)return QString();
    return QQuickPaintedItem::inputMethodQuery(query);
}
