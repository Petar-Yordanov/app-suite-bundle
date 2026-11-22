#include "Native.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <atomic>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace rec::native {
void initialize(){}
bool wayland(){return false;}
bool requestPermission(){return true;}
QString limits(){return "Protected content may be blank. Minimized or non-responsive windows may not be capturable.";}
namespace {
QRect rect(RECT r){return {int(r.left),int(r.top),int(r.right-r.left),int(r.bottom-r.top)};}
QString str(const wchar_t* s){return QString::fromWCharArray(s);}
struct Monitor {HMONITOR handle;QString name;QRect bounds;};
int dxgiIndex(HMONITOR monitor){
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))return -1;
    ComPtr<IDXGIAdapter1> adapter;
    if(FAILED(factory->EnumAdapters1(0,adapter.GetAddressOf())))return -1;
    for(UINT i=0;;++i){
        ComPtr<IDXGIOutput> output;
        if(FAILED(adapter->EnumOutputs(i,output.GetAddressOf())))break;
        DXGI_OUTPUT_DESC desc{};
        if(SUCCEEDED(output->GetDesc(&desc))&&desc.Monitor==monitor&&desc.AttachedToDesktop)return int(i);
    }
    return -1;
}
BOOL CALLBACK monitorCallback(HMONITOR m,HDC,LPRECT,LPARAM user){
    MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(m,reinterpret_cast<MONITORINFO*>(&info)))reinterpret_cast<QList<Monitor>*>(user)->append({m,str(info.szDevice),rect(info.rcMonitor)});return TRUE;
}
BOOL CALLBACK windowCallback(HWND w,LPARAM user){
    if(!IsWindowVisible(w)||IsIconic(w)||GetWindow(w,GW_OWNER)||GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId())return TRUE;
    wchar_t title[1024]{};if(!GetWindowTextW(w,title,1024))return TRUE;RECT r{};if(!GetWindowRect(w,&r)||!validSize(rect(r).size()))return TRUE;
    reinterpret_cast<QList<Target>*>(user)->append({quintptr(w),str(title),{},rect(r),true});return TRUE;
}
UINT vk(Qt::Key k){if(k>=Qt::Key_A&&k<=Qt::Key_Z)return UINT(k-Qt::Key_A+'A');if(k>=Qt::Key_0&&k<=Qt::Key_9)return UINT(k-Qt::Key_0+'0');if(k>=Qt::Key_F1&&k<=Qt::Key_F24)return VK_F1+k-Qt::Key_F1;
    switch(k){case Qt::Key_Print:return VK_SNAPSHOT;case Qt::Key_Space:return VK_SPACE;case Qt::Key_Insert:return VK_INSERT;case Qt::Key_Delete:return VK_DELETE;case Qt::Key_Home:return VK_HOME;case Qt::Key_End:return VK_END;case Qt::Key_Left:return VK_LEFT;case Qt::Key_Right:return VK_RIGHT;case Qt::Key_Up:return VK_UP;case Qt::Key_Down:return VK_DOWN;default:return 0;}}
std::atomic<int> ids{0x5200};
}
QList<Target> screens(){
    QList<Monitor> monitors;EnumDisplayMonitors(nullptr,nullptr,monitorCallback,reinterpret_cast<LPARAM>(&monitors));QList<Target> out;
    for(auto* s:QGuiApplication::screens()){
        auto name=s->name();const Monitor* m=nullptr;for(const auto& n:monitors)if(n.name.compare(name,Qt::CaseInsensitive)==0){m=&n;break;}
        if(!m&&monitors.size()==1)m=&monitors.first();if(!m)continue;
        out.append({quintptr(m->handle),name,s->geometry(),m->bounds,false,dxgiIndex(m->handle)});
    }return out;
}
QList<Target> windows(){QList<Target> out;EnumWindows(windowCallback,reinterpret_cast<LPARAM>(&out));return out;}
struct Hotkey::Impl:QAbstractNativeEventFilter{
    Hotkey* owner;int id=ids.fetch_add(1);bool registered=false;
    explicit Impl(Hotkey* o):owner(o){}
    bool nativeEventFilter(const QByteArray&,void* msg,qintptr*) override{auto* m=static_cast<MSG*>(msg);if(m&&m->message==WM_HOTKEY&&int(m->wParam)==id){emit owner->activated();return true;}return false;}
};
Hotkey::Hotkey(QObject* p):QObject(p),impl_(std::make_unique<Impl>(this)){QCoreApplication::instance()->installNativeEventFilter(impl_.get());}
Hotkey::~Hotkey(){unbind();QCoreApplication::instance()->removeNativeEventFilter(impl_.get());}
void Hotkey::unbind(){if(impl_->registered)UnregisterHotKey(nullptr,impl_->id);impl_->registered=false;}
bool Hotkey::bind(const QKeySequence& seq,QString& error){
    unbind();if(seq.count()!=1||seq.isEmpty()){error="Choose one key combination.";return false;}auto k=seq[0];auto m=k.keyboardModifiers();UINT mods=MOD_NOREPEAT;
    if(m.testFlag(Qt::ControlModifier))mods|=MOD_CONTROL;if(m.testFlag(Qt::AltModifier))mods|=MOD_ALT;if(m.testFlag(Qt::MetaModifier))mods|=MOD_WIN;if(m.testFlag(Qt::ShiftModifier))mods|=MOD_SHIFT;
    if((mods&(MOD_CONTROL|MOD_ALT|MOD_WIN))==0||!vk(k.key())||!RegisterHotKey(nullptr,impl_->id,mods,vk(k.key()))){error="Shortcut unsupported or already in use.";return false;}impl_->registered=true;return true;
}
}
