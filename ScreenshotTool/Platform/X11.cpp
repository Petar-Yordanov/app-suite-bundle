#include "Native.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QSocketNotifier>
#include <QPainter>
#include <QMutex>
#include <QMutexLocker>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <algorithm>
#include <bit>
#include <unistd.h>

namespace shot::native {
namespace {
QMutex xmutex;int xerror=0;
int onError(Display*,XErrorEvent* e){xerror=e->error_code;return 0;}
struct XConnection {
    QMutexLocker<QMutex> lock{&xmutex};Display* d=XOpenDisplay(nullptr);XErrorHandler previous=nullptr;
    XConnection(){if(d){XSync(d,False);xerror=0;previous=XSetErrorHandler(onError);}}
    ~XConnection(){if(d){XSync(d,False);XCloseDisplay(d);XSetErrorHandler(previous);}}
};
QList<unsigned long> property(Display* d,Window w,const char* key,Atom type){
    Atom actual=0;int format=0;unsigned long count=0,rest=0;unsigned char* bytes=nullptr;QList<unsigned long> out;
    if(XGetWindowProperty(d,w,XInternAtom(d,key,False),0,65536,False,type,&actual,&format,&count,&rest,&bytes)==Success&&format==32&&bytes){auto* p=reinterpret_cast<unsigned long*>(bytes);for(unsigned long i=0;i<count;++i)out.append(p[i]);}if(bytes)XFree(bytes);return out;
}
QString title(Display* d,Window w){
    Atom actual=0;int format=0;unsigned long n=0,rest=0;unsigned char* data=nullptr;QString out;
    if(XGetWindowProperty(d,w,XInternAtom(d,"_NET_WM_NAME",False),0,1024,False,XInternAtom(d,"UTF8_STRING",False),&actual,&format,&n,&rest,&data)==Success&&data&&format==8)out=QString::fromUtf8(reinterpret_cast<char*>(data),qsizetype(n));if(data)XFree(data);
    if(out.isEmpty()){char* name=nullptr;if(XFetchName(d,w,&name)&&name){out=QString::fromLocal8Bit(name);XFree(name);}}return out;
}
QRect windowRect(Display* d,Window w){XWindowAttributes a{};Window child=0;int x=0,y=0;if(!XGetWindowAttributes(d,w,&a)||a.map_state!=IsViewable||!XTranslateCoordinates(d,w,DefaultRootWindow(d),0,0,&x,&y,&child))return {};return {x,y,a.width,a.height};}
unsigned channel(unsigned long p,unsigned long mask){if(!mask)return 0;auto shift=std::countr_zero(mask);auto max=mask>>shift;return unsigned(((p&mask)>>shift)*255/max);}
void cursorOverlay(Display* d,QImage& image,QPoint origin){int ev=0,err=0;if(!XFixesQueryExtension(d,&ev,&err))return;auto* c=XFixesGetCursorImage(d);if(!c)return;
    if(c->width&&c->height&&c->width<1024&&c->height<1024){QImage cur(c->width,c->height,QImage::Format_ARGB32_Premultiplied);for(int y=0;y<cur.height();++y){auto* dest=reinterpret_cast<QRgb*>(cur.scanLine(y));for(int x=0;x<cur.width();++x)dest[x]=QRgb(c->pixels[y*cur.width()+x]);}QPainter p(&image);p.drawImage(QPoint(c->x-c->xhot,c->y-c->yhot)-origin,cur);}XFree(c);
}
KeySym keySym(Qt::Key k){if(k>=Qt::Key_A&&k<=Qt::Key_Z)return XK_A+k-Qt::Key_A;if(k>=Qt::Key_0&&k<=Qt::Key_9)return XK_0+k-Qt::Key_0;if(k>=Qt::Key_F1&&k<=Qt::Key_F35)return XK_F1+k-Qt::Key_F1;switch(k){case Qt::Key_Print:return XK_Print;case Qt::Key_Space:return XK_space;case Qt::Key_Insert:return XK_Insert;case Qt::Key_Delete:return XK_Delete;case Qt::Key_Home:return XK_Home;case Qt::Key_End:return XK_End;case Qt::Key_Left:return XK_Left;case Qt::Key_Right:return XK_Right;case Qt::Key_Up:return XK_Up;case Qt::Key_Down:return XK_Down;default:return 0;}}
unsigned numLock(Display* d){auto code=XKeysymToKeycode(d,XK_Num_Lock);auto* map=XGetModifierMapping(d);unsigned result=0;if(map){for(int i=0;i<8;++i)for(int j=0;j<map->max_keypermod;++j)if(code&&map->modifiermap[i*map->max_keypermod+j]==code)result|=1u<<i;XFreeModifiermap(map);}return result;}
}
void initialize(){XInitThreads();}
bool wayland(){return QGuiApplication::platformName().startsWith("wayland");}
bool requestPermission(){return true;}
QString limits(){return wayland()?"Wayland: the desktop portal controls monitor/window/region selection and cursor inclusion. Choose a single source in its dialog. If global shortcuts are unsupported, bind screenshottool --region, --screen or --window in desktop settings.":"X11: obscured or off-screen window content may be unavailable. Cursor inclusion requires XFixes.";}
QList<Target> screens(){QList<Target> out;if(wayland())return out;XConnection c;if(!c.d)return out;auto d=c.d;auto root=DefaultRootWindow(d);auto* resources=XRRGetScreenResourcesCurrent(d,root);
    for(auto* s:QGuiApplication::screens()){
        QRect native;quint64 id=0;if(resources)for(int i=0;i<resources->noutput;++i){auto output=resources->outputs[i];auto* info=XRRGetOutputInfo(d,resources,output);if(!info)continue;auto name=QString::fromUtf8(info->name,info->nameLen);if(name==s->name()&&info->crtc){auto* crtc=XRRGetCrtcInfo(d,resources,info->crtc);if(crtc){native=QRect(crtc->x,crtc->y,int(crtc->width),int(crtc->height));id=output;XRRFreeCrtcInfo(crtc);}}XRRFreeOutputInfo(info);if(id)break;}
        if(native.isEmpty()&&QGuiApplication::screens().size()==1){native=QRect(0,0,DisplayWidth(d,DefaultScreen(d)),DisplayHeight(d,DefaultScreen(d)));id=root;}
        if(!native.isEmpty())out.append({id,s->name(),s->geometry(),native,false});
    }if(resources)XRRFreeScreenResources(resources);return out;
}
QList<Target> windows(){QList<Target> out;if(wayland())return out;XConnection c;if(!c.d)return out;auto d=c.d;auto root=DefaultRootWindow(d);auto list=property(d,root,"_NET_CLIENT_LIST_STACKING",XA_WINDOW);if(list.isEmpty())list=property(d,root,"_NET_CLIENT_LIST",XA_WINDOW);
    if(list.isEmpty()){Window retRoot=0,parent=0,*children=nullptr;unsigned n=0;if(XQueryTree(d,root,&retRoot,&parent,&children,&n)){for(unsigned i=0;i<n;++i)list.append(children[i]);}if(children)XFree(children);}
    for(auto w:list){auto pids=property(d,w,"_NET_WM_PID",XA_CARDINAL);if(!pids.isEmpty()&&pids.first()==static_cast<unsigned long>(getpid()))continue;auto r=windowRect(d,w);auto name=title(d,w);if(validSize(r.size())&&!name.isEmpty())out.append({quint64(w),name,{},r,true});}return out;
}
Result capture(const Target& t,bool cursor){XConnection c;if(!c.d)return {{},"X11 display unavailable."};auto d=c.d;auto w=t.window?Window(t.id):DefaultRootWindow(d);auto r=t.window?windowRect(d,w):t.native;if(!validSize(r.size()))return {{},"Capture source is unavailable or too large."};
    if(!t.window&& !QRect(0,0,DisplayWidth(d,DefaultScreen(d)),DisplayHeight(d,DefaultScreen(d))).contains(r))return {{},"Monitor geometry changed."};
    auto* source=XGetImage(d,w,t.window?0:r.x(),t.window?0:r.y(),unsigned(r.width()),unsigned(r.height()),AllPlanes,ZPixmap);XSync(d,False);if(!source||xerror){if(source)XDestroyImage(source);return {{},"X11 could not read this capture source."};}
    QImage image(r.size(),QImage::Format_RGB32);if(!image.isNull())for(int y=0;y<r.height();++y){auto* dst=reinterpret_cast<QRgb*>(image.scanLine(y));if(source->bits_per_pixel==32&&source->byte_order==LSBFirst&&source->red_mask==0xff0000&&source->green_mask==0xff00&&source->blue_mask==0xff){auto* src=reinterpret_cast<const quint32*>(source->data+y*source->bytes_per_line);for(int x=0;x<r.width();++x)dst[x]=src[x]|0xff000000u;}else for(int x=0;x<r.width();++x){auto p=XGetPixel(source,x,y);dst[x]=qRgb(channel(p,source->red_mask),channel(p,source->green_mask),channel(p,source->blue_mask));}}
    XDestroyImage(source);if(cursor&&!image.isNull())cursorOverlay(d,image,r.topLeft());return {image,image.isNull()?QString("Cannot allocate capture image."):QString()};
}
struct Hotkey::Impl{Display* d=nullptr;unsigned mods=0,num=0;int code=0;std::unique_ptr<QSocketNotifier> notifier;};
Hotkey::Hotkey(QObject* p):QObject(p),impl_(std::make_unique<Impl>()){}Hotkey::~Hotkey(){unbind();}
void Hotkey::unbind(){impl_->notifier.reset();if(impl_->d){XCloseDisplay(impl_->d);impl_->d=nullptr;}}
bool Hotkey::bind(const QKeySequence& seq,QString& error){unbind();if(wayland()){error="Use the Wayland compositor's shortcut settings.";return false;}if(seq.count()!=1||seq.isEmpty()){error="Choose one key combination.";return false;}
    auto k=seq[0];auto m=k.keyboardModifiers();unsigned mods=0;if(m.testFlag(Qt::ControlModifier))mods|=ControlMask;if(m.testFlag(Qt::AltModifier))mods|=Mod1Mask;if(m.testFlag(Qt::MetaModifier))mods|=Mod4Mask;if(m.testFlag(Qt::ShiftModifier))mods|=ShiftMask;
    if(!(mods&(ControlMask|Mod1Mask|Mod4Mask))){error="Use Ctrl, Alt or Meta.";return false;}QMutexLocker guard(&xmutex);auto* d=XOpenDisplay(nullptr);if(!d){error="X11 unavailable.";return false;}auto sym=keySym(k.key());auto code=sym?XKeysymToKeycode(d,sym):0;if(!code){XCloseDisplay(d);error="Unsupported key.";return false;}
    auto num=numLock(d);xerror=0;auto old=XSetErrorHandler(onError);for(unsigned mask:{0u,unsigned(LockMask),num,num|LockMask})XGrabKey(d,code,mods|mask,DefaultRootWindow(d),False,GrabModeAsync,GrabModeAsync);XSync(d,False);XSetErrorHandler(old);
    if(xerror){XCloseDisplay(d);error="Shortcut already in use.";return false;}impl_->d=d;impl_->mods=mods;impl_->num=num;impl_->code=code;
    impl_->notifier=std::make_unique<QSocketNotifier>(ConnectionNumber(d),QSocketNotifier::Read,this);connect(impl_->notifier.get(),&QSocketNotifier::activated,this,[this]{auto* d=impl_->d;if(!d)return;while(XPending(d)){XEvent e;XNextEvent(d,&e);if(e.type==KeyPress&&int(e.xkey.keycode)==impl_->code&&(e.xkey.state&~(LockMask|impl_->num))==impl_->mods)emit activated();}});return true;
}
}
