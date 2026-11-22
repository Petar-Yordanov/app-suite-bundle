#include "Native.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <AppKit/AppKit.h>
#include <AVFoundation/AVFoundation.h>
#include <QPointer>
#include <QMetaObject>
#include <Carbon/Carbon.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <climits>
#include <cmath>

namespace rec::native {
namespace {
int keyCode(Qt::Key key){
    static const int letters[]={kVK_ANSI_A,kVK_ANSI_B,kVK_ANSI_C,kVK_ANSI_D,kVK_ANSI_E,kVK_ANSI_F,kVK_ANSI_G,kVK_ANSI_H,kVK_ANSI_I,kVK_ANSI_J,kVK_ANSI_K,kVK_ANSI_L,kVK_ANSI_M,kVK_ANSI_N,kVK_ANSI_O,kVK_ANSI_P,kVK_ANSI_Q,kVK_ANSI_R,kVK_ANSI_S,kVK_ANSI_T,kVK_ANSI_U,kVK_ANSI_V,kVK_ANSI_W,kVK_ANSI_X,kVK_ANSI_Y,kVK_ANSI_Z};
    static const int digits[]={kVK_ANSI_0,kVK_ANSI_1,kVK_ANSI_2,kVK_ANSI_3,kVK_ANSI_4,kVK_ANSI_5,kVK_ANSI_6,kVK_ANSI_7,kVK_ANSI_8,kVK_ANSI_9};
    static const int fn[]={kVK_F1,kVK_F2,kVK_F3,kVK_F4,kVK_F5,kVK_F6,kVK_F7,kVK_F8,kVK_F9,kVK_F10,kVK_F11,kVK_F12,kVK_F13,kVK_F14,kVK_F15,kVK_F16,kVK_F17,kVK_F18,kVK_F19,kVK_F20};
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return letters[key-Qt::Key_A];
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return digits[key-Qt::Key_0];
    if(key>=Qt::Key_F1&&key<=Qt::Key_F20)return fn[key-Qt::Key_F1];
    switch(key){case Qt::Key_Space:return kVK_Space;case Qt::Key_Tab:return kVK_Tab;case Qt::Key_Return:return kVK_Return;case Qt::Key_Escape:return kVK_Escape;case Qt::Key_Left:return kVK_LeftArrow;case Qt::Key_Right:return kVK_RightArrow;case Qt::Key_Up:return kVK_UpArrow;case Qt::Key_Down:return kVK_DownArrow;case Qt::Key_Home:return kVK_Home;case Qt::Key_End:return kVK_End;case Qt::Key_PageUp:return kVK_PageUp;case Qt::Key_PageDown:return kVK_PageDown;default:return -1;}
}
std::atomic<unsigned> ids{1};
QRect rect(CGRect r){return {int(r.origin.x),int(r.origin.y),int(r.size.width),int(r.size.height)};}

}
void initialize(){}
bool wayland(){return false;}
bool requestPermission(){return CGPreflightScreenCaptureAccess()||CGRequestScreenCaptureAccess();}
void requestMicrophonePermission(QObject* context,std::function<void(bool)> done){
    auto state=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if(state==AVAuthorizationStatusAuthorized){done(true);return;}
    if(state!=AVAuthorizationStatusNotDetermined){done(false);return;}
    QPointer<QObject> guard(context);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL allowed){
        if(guard)QMetaObject::invokeMethod(guard.data(),[guard,done,allowed]{if(guard)done(bool(allowed));},Qt::QueuedConnection);
    }];
}
QString limits(){return "macOS 14+ requires Screen Recording permission. Protected or minimized windows may be unavailable.";}
QList<Target> screens(){
    QList<Target> out;CGDirectDisplayID ids[64];uint32_t count=0;
    if(CGGetActiveDisplayList(64,ids,&count)!=kCGErrorSuccess)return out;
    for(auto* s:QGuiApplication::screens())for(uint32_t i=0;i<count;++i){
        auto logical=rect(CGDisplayBounds(ids[i]));
        if(logical==s->geometry()||(count==1&&QGuiApplication::screens().size()==1)){
            out.append({ids[i],s->name(),s->geometry(),QRect(logical.topLeft(),QSize(int(CGDisplayPixelsWide(ids[i])),int(CGDisplayPixelsHigh(ids[i])))),false});break;
        }
    }return out;
}
QList<Target> windows(){@autoreleasepool {
    QList<Target> out;auto array=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,kCGNullWindowID);
    if(!array)return out;
    for(NSDictionary* item in (__bridge NSArray*)array){
        if([item[(__bridge NSString*)kCGWindowLayer] intValue]!=0||[item[(__bridge NSString*)kCGWindowOwnerPID] intValue]==[[NSProcessInfo processInfo] processIdentifier])continue;
        CGRect bounds{};if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)item[(__bridge NSString*)kCGWindowBounds],&bounds))continue;
        if(!validSize(rect(bounds).size()))continue;
        NSString* name=item[(__bridge NSString*)kCGWindowName];if(!name.length)name=item[(__bridge NSString*)kCGWindowOwnerName];
        if(!name.length)continue;
        auto logical=rect(bounds);double scale=1;
        for(auto* screen:QGuiApplication::screens())if(screen->geometry().contains(logical.center())){scale=screen->devicePixelRatio();break;}
        out.append({[item[(__bridge NSString*)kCGWindowNumber] unsignedIntValue],QString::fromUtf8(name.UTF8String),{},
            QRect(logical.topLeft(),QSize(int(logical.width()*scale),int(logical.height()*scale))),true});
    }CFRelease(array);return out;
}}
struct Hotkey::Impl{
    Hotkey* owner;unsigned id=ids.fetch_add(1);EventHotKeyRef ref=nullptr;EventHandlerRef handler=nullptr;
    explicit Impl(Hotkey* o):owner(o){}
    static OSStatus callback(EventHandlerCallRef,EventRef e,void* context){
        auto* self=static_cast<Impl*>(context);EventHotKeyID id{};
        if(GetEventParameter(e,kEventParamDirectObject,typeEventHotKeyID,nullptr,sizeof(id),nullptr,&id)==noErr&&id.signature==0x52454344&&id.id==self->id){emit self->owner->activated();return noErr;}return eventNotHandledErr;
    }
};
Hotkey::Hotkey(QObject* p):QObject(p),impl_(std::make_unique<Impl>(this)){EventTypeSpec event{kEventClassKeyboard,kEventHotKeyPressed};InstallApplicationEventHandler(&Impl::callback,1,&event,impl_.get(),&impl_->handler);}
Hotkey::~Hotkey(){unbind();if(impl_->handler)RemoveEventHandler(impl_->handler);}
void Hotkey::unbind(){if(impl_->ref){UnregisterEventHotKey(impl_->ref);impl_->ref=nullptr;}}
bool Hotkey::bind(const QKeySequence& seq,QString& error){
    unbind();if(!impl_->handler||seq.count()!=1||seq.isEmpty()){error="Choose one key combination.";return false;}
    auto k=seq[0];auto m=k.keyboardModifiers();UInt32 mods=0;
    if(m.testFlag(Qt::ControlModifier))mods|=cmdKey;if(m.testFlag(Qt::MetaModifier))mods|=controlKey;if(m.testFlag(Qt::AltModifier))mods|=optionKey;if(m.testFlag(Qt::ShiftModifier))mods|=shiftKey;
    int code=keyCode(k.key());if(code<0||!(mods&(cmdKey|controlKey|optionKey))||RegisterEventHotKey(UInt32(code),mods,{0x52454344,impl_->id},GetApplicationEventTarget(),0,&impl_->ref)!=noErr){error="Shortcut unsupported or already in use.";return false;}return true;
}
}
