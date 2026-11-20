#include "Native.hpp"
#include "../Core/Store.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QSettings>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <array>
#include <cstring>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#undef None
#undef Bool
#undef Status
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#endif

namespace native {
namespace {
std::string utf(const QString& s){return s.toUtf8().toStdString();}
QString executable() {return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());}
#if defined(Q_OS_WIN)
QString windowProcess(HWND hwnd) {
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!h)return {};
    wchar_t name[32768];DWORD n=32768;QString out;
    if(QueryFullProcessImageNameW(h,0,name,&n))out=QString::fromWCharArray(name,static_cast<int>(n));
    CloseHandle(h);return out;
}
UINT virtualKey(Qt::Key key){
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return static_cast<UINT>('A'+key-Qt::Key_A);
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return static_cast<UINT>('0'+key-Qt::Key_0);
    if(key>=Qt::Key_F1&&key<=Qt::Key_F24)return VK_F1+key-Qt::Key_F1;
    switch(key){case Qt::Key_Space:return VK_SPACE;case Qt::Key_Tab:return VK_TAB;case Qt::Key_Return:return VK_RETURN;case Qt::Key_Escape:return VK_ESCAPE;case Qt::Key_Insert:return VK_INSERT;case Qt::Key_Delete:return VK_DELETE;case Qt::Key_Home:return VK_HOME;case Qt::Key_End:return VK_END;case Qt::Key_Left:return VK_LEFT;case Qt::Key_Right:return VK_RIGHT;case Qt::Key_Up:return VK_UP;case Qt::Key_Down:return VK_DOWN;case Qt::Key_PageUp:return VK_PRIOR;case Qt::Key_PageDown:return VK_NEXT;default:return 0;}
}
QString runKey(){return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");}
#elif defined(Q_OS_LINUX)
Display* xdisplay(){return QGuiApplication::platformName()==QStringLiteral("xcb")?XOpenDisplay(nullptr):nullptr;}
QString xWindowProcess(Display* d,Window w){
    if(!w)return {};
    Atom atom=XInternAtom(d,"_NET_WM_PID",True);if(!atom)return {};
    Atom actual=0;int format=0;unsigned long count=0,remaining=0;unsigned char* bytes=nullptr;
    QString name;
    auto previous=XSetErrorHandler([](Display*,XErrorEvent*){return 0;});
    auto result=XGetWindowProperty(d,w,atom,0,1,False,XA_CARDINAL,&actual,&format,&count,&remaining,&bytes);
    XSync(d,False);XSetErrorHandler(previous);
    if(result==Success&&bytes&&format==32&&count==1){
        const auto pid=*reinterpret_cast<unsigned long*>(bytes);
        QFile file(QStringLiteral("/proc/%1/comm").arg(pid));if(file.open(QIODevice::ReadOnly))name=QString::fromUtf8(file.read(4096)).trimmed();
        auto target=QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();if(!target.isEmpty())name+=QStringLiteral(" ")+target;
    }
    if(bytes)XFree(bytes);
    return name;
}
KeySym keysym(Qt::Key key){
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return XK_A+key-Qt::Key_A;
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return XK_0+key-Qt::Key_0;
    if(key>=Qt::Key_F1&&key<=Qt::Key_F35)return XK_F1+key-Qt::Key_F1;
    switch(key){case Qt::Key_Space:return XK_space;case Qt::Key_Tab:return XK_Tab;case Qt::Key_Return:return XK_Return;case Qt::Key_Escape:return XK_Escape;case Qt::Key_Insert:return XK_Insert;case Qt::Key_Delete:return XK_Delete;case Qt::Key_Home:return XK_Home;case Qt::Key_End:return XK_End;case Qt::Key_Left:return XK_Left;case Qt::Key_Right:return XK_Right;case Qt::Key_Up:return XK_Up;case Qt::Key_Down:return XK_Down;case Qt::Key_PageUp:return XK_Page_Up;case Qt::Key_PageDown:return XK_Page_Down;default:return 0;}
}
int grabError=0;
int grabErrorHandler(Display*,XErrorEvent* e){grabError=e->error_code;return 0;}
unsigned int numLockMask(Display* display){
    auto code=XKeysymToKeycode(display,XK_Num_Lock);auto* map=XGetModifierMapping(display);
    unsigned result=0;
    if(map){for(int i=0;i<8;++i)for(int j=0;j<map->max_keypermod;++j)
        if(map->modifiermap[i*map->max_keypermod+j]==code&&code)result|=1u<<i;XFreeModifiermap(map);}
    return result;
}
QString desktopFile(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+QStringLiteral("/autostart/clipdeck.desktop");}
QString desktopExec(QString s){s.replace("\\","\\\\");s.replace("\"","\\\"");s.replace("`","\\`");s.replace("$","\\$");s.replace("%","%%");return "\""+s+"\" --background";}
#endif
}

#if !defined(Q_OS_MACOS)
quint64 clipboardSequence(){
#if defined(Q_OS_WIN)
    if(QGuiApplication::platformName()==QStringLiteral("windows"))return GetClipboardSequenceNumber();
#endif
    return 0;
}
ClipboardInfo clipboardInfo(){
    ClipboardInfo out;
#if defined(Q_OS_WIN)
    if(QGuiApplication::platformName()!=QStringLiteral("windows"))return out;
    out.sequence=clipboardSequence();
    // WM_CLIPBOARDUPDATE can arrive while the writer still holds the lock.
    // Never read potentially sensitive payloads before the policy check succeeds.
    if(!OpenClipboard(nullptr)){out.available=false;return out;}
    for(auto* marker:{L"Clipboard Viewer Ignore",L"CF_CLIPBOARD_VIEWER_IGNORE",L"ExcludeClipboardContentFromMonitorProcessing"})
        if(IsClipboardFormatAvailable(RegisterClipboardFormatW(marker)))out.secret=true;
    // These two formats are DWORD-valued policy flags, not presence-only markers.
    // Zero (or an unreadable/malformed value) remains a conservative exclusion.
    if(!out.secret)for(auto* marker:{L"CanIncludeInClipboardHistory",L"CanUploadToCloudClipboard"}){
        const auto format=RegisterClipboardFormatW(marker);
        if(!IsClipboardFormatAvailable(format))continue;
        auto handle=GetClipboardData(format);
        DWORD value=0;
        bool valid=false;
        if(handle&&GlobalSize(handle)>=sizeof(value)){
            if(auto* raw=GlobalLock(handle)){std::memcpy(&value,raw,sizeof(value));GlobalUnlock(handle);valid=true;}
        }
        if(!valid||value!=1)out.secret=true;
        else out.allowedPolicyFormats.push_back(QString::fromWCharArray(marker));
    }
    CloseClipboard();
    out.sequence=clipboardSequence();
    out.source=windowProcess(GetClipboardOwner());
    out.secret=out.secret||clip::passwordManager(utf(out.source))||clip::passwordManager(utf(windowProcess(GetForegroundWindow())));
#elif defined(Q_OS_LINUX)
    if(auto* d=xdisplay()){
        auto selection=XInternAtom(d,"CLIPBOARD",False);auto owner=XGetSelectionOwner(d,selection);
        out.source=xWindowProcess(d,owner);Window focus=0;int revert=0;XGetInputFocus(d,&focus,&revert);
        Window active=0;Atom actual=0;int format=0;unsigned long count=0,remaining=0;unsigned char* value=nullptr;
        auto activeAtom=XInternAtom(d,"_NET_ACTIVE_WINDOW",True);
        if(activeAtom&&XGetWindowProperty(d,DefaultRootWindow(d),activeAtom,0,1,False,XA_WINDOW,&actual,&format,&count,&remaining,&value)==Success&&value&&format==32&&count==1)
            active=*reinterpret_cast<Window*>(value);
        if(value)XFree(value);
        out.secret=clip::passwordManager(utf(out.source))||clip::passwordManager(utf(xWindowProcess(d,focus)))||clip::passwordManager(utf(xWindowProcess(d,active)));
        XCloseDisplay(d);
    }
#endif
    return out;
}
bool autostartEnabled(){
#if defined(Q_OS_WIN)
    return QSettings(runKey(),QSettings::NativeFormat).contains(QStringLiteral("ClipDeck"));
#elif defined(Q_OS_LINUX)
    return QFile::exists(desktopFile());
#else
    return false;
#endif
}
bool setAutostart(bool enabled,QString& error){
#if defined(Q_OS_WIN)
    QSettings settings(runKey(),QSettings::NativeFormat);
    if(enabled)settings.setValue(QStringLiteral("ClipDeck"),"\""+executable()+"\" --background"+(QCoreApplication::arguments().contains("--portable")?" --portable":""));
    else settings.remove(QStringLiteral("ClipDeck"));
    settings.sync();
    if(settings.status()!=QSettings::NoError){error=QStringLiteral("Could not update the Windows login entry.");return false;}
    return true;
#elif defined(Q_OS_LINUX)
    const auto file=desktopFile();
    if(!enabled){if(QFile::exists(file)&&!QFile::remove(file)){error=QStringLiteral("Could not remove autostart entry.");return false;}return true;}
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);if(!out.open(QIODevice::WriteOnly)){error=out.errorString();return false;}
    auto data=("[Desktop Entry]\nType=Application\nName=ClipDeck\nExec="+desktopExec(QCoreApplication::applicationFilePath())+(QCoreApplication::arguments().contains("--portable")?" --portable":"")+"\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8();
    if(out.write(data)!=data.size()||!out.commit()){error=out.errorString();return false;}return true;
#else
    Q_UNUSED(enabled);error=QStringLiteral("Unsupported startup platform.");return false;
#endif
}
QString platformLimitations(){
#if defined(Q_OS_LINUX)
    if(QGuiApplication::platformName().startsWith("wayland"))
        return QStringLiteral("Wayland limits background clipboard observation and source identification. If supported by your compositor, bind clipdeck --show as a system keyboard shortcut. Sensitive MIME markers are honored; unmarked source identity is not guaranteed.");
    return QStringLiteral("X11 clipboard ownership and application detection are best effort. Password-manager privacy markers are always excluded.");
#else
    return QStringLiteral("Clipboard privacy markers and source process detection are used when available. Unmarked content with an unidentifiable owner cannot be classified reliably.");
#endif
}

struct Hotkey::Impl : QAbstractNativeEventFilter {
    Hotkey* owner;
#if defined(Q_OS_WIN)
    bool registered=false;
#elif defined(Q_OS_LINUX)
    Display* display=nullptr;
    Window root=0;
    unsigned mods=0,num=0;
    int code=0;
    std::unique_ptr<QSocketNotifier> notifier;
#endif
    explicit Impl(Hotkey* o):owner(o){}
    bool nativeEventFilter(const QByteArray&,void* message,qintptr*) override{
#if defined(Q_OS_WIN)
        auto* m=static_cast<MSG*>(message);if(m&&m->message==WM_HOTKEY&&m->wParam==0x4350){emit owner->activated();return true;}
#else
        Q_UNUSED(message);
#endif
        return false;
    }
};
Hotkey::Hotkey(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){
#if defined(Q_OS_WIN)
    QCoreApplication::instance()->installNativeEventFilter(impl_.get());
#endif
}
Hotkey::~Hotkey(){
    unregisterShortcut();
#if defined(Q_OS_WIN)
    QCoreApplication::instance()->removeNativeEventFilter(impl_.get());
#endif
}
void Hotkey::unregisterShortcut(){
#if defined(Q_OS_WIN)
    if(impl_->registered)UnregisterHotKey(nullptr,0x4350);impl_->registered=false;
#elif defined(Q_OS_LINUX)
    if(impl_->display){impl_->notifier.reset();XCloseDisplay(impl_->display);impl_->display=nullptr;}
#endif
}
bool Hotkey::registerShortcut(const QKeySequence& sequence,QString& error){
    unregisterShortcut();
    if(sequence.count()!=1||sequence.isEmpty()){error=QStringLiteral("Choose one key combination.");return false;}
    auto combo=sequence[0];auto mods=combo.keyboardModifiers();auto key=combo.key();
    if(!mods.testFlag(Qt::ControlModifier)&&!mods.testFlag(Qt::AltModifier)&&!mods.testFlag(Qt::MetaModifier)){
        error=QStringLiteral("Use Ctrl, Alt or Meta to avoid capturing ordinary typing.");return false;
    }
#if defined(Q_OS_WIN)
    UINT m=MOD_NOREPEAT;if(mods.testFlag(Qt::ControlModifier))m|=MOD_CONTROL;if(mods.testFlag(Qt::AltModifier))m|=MOD_ALT;
    if(mods.testFlag(Qt::ShiftModifier))m|=MOD_SHIFT;if(mods.testFlag(Qt::MetaModifier))m|=MOD_WIN;
    auto vk=virtualKey(key);if(!vk||!RegisterHotKey(nullptr,0x4350,m,vk)){error=QStringLiteral("Shortcut is unsupported or already registered.");return false;}
    impl_->registered=true;return true;
#elif defined(Q_OS_LINUX)
    auto* d=xdisplay();if(!d){error=QStringLiteral("Use your Wayland compositor's custom shortcut to run clipdeck --show.");return false;}
    auto sym=keysym(key);auto code=sym?XKeysymToKeycode(d,sym):0;
    if(!code){XCloseDisplay(d);error=QStringLiteral("Unsupported shortcut key.");return false;}
    unsigned m=0;if(mods.testFlag(Qt::ControlModifier))m|=ControlMask;if(mods.testFlag(Qt::AltModifier))m|=Mod1Mask;
    if(mods.testFlag(Qt::ShiftModifier))m|=ShiftMask;if(mods.testFlag(Qt::MetaModifier))m|=Mod4Mask;
    auto root=DefaultRootWindow(d);auto num=numLockMask(d);
    XSync(d,False);grabError=0;auto old=XSetErrorHandler(grabErrorHandler);
    for(unsigned mask:{0u,static_cast<unsigned>(LockMask),num,num|LockMask})XGrabKey(d,code,m|mask,root,False,GrabModeAsync,GrabModeAsync);
    XSync(d,False);XSetErrorHandler(old);
    if(grabError){XCloseDisplay(d);error=QStringLiteral("Shortcut is already in use.");return false;}
    impl_->display=d;impl_->root=root;impl_->mods=m;impl_->num=num;impl_->code=code;
    impl_->notifier=std::make_unique<QSocketNotifier>(ConnectionNumber(d),QSocketNotifier::Read,this);
    connect(impl_->notifier.get(),&QSocketNotifier::activated,this,[this]{
        auto* d=impl_->display;if(!d)return;
        while(XPending(d)){XEvent event;XNextEvent(d,&event);
            if(event.type==2 && event.xkey.keycode==static_cast<unsigned>(impl_->code)
                && (event.xkey.state&~(LockMask|impl_->num))==impl_->mods)emit activated();}
    });
    return true;
#else
    Q_UNUSED(key);error=QStringLiteral("Unsupported shortcut platform.");return false;
#endif
}
#endif
}
