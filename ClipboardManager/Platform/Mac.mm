#include "Native.hpp"
#include "../Core/Store.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStandardPaths>
#include <QXmlStreamWriter>
#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

namespace native {
namespace {
QString agentPath(){return QDir::homePath()+"/Library/LaunchAgents/org.clipdeck.agent.plist";}
int keyCode(Qt::Key key){
    static const int letters[]={kVK_ANSI_A,kVK_ANSI_B,kVK_ANSI_C,kVK_ANSI_D,kVK_ANSI_E,kVK_ANSI_F,kVK_ANSI_G,kVK_ANSI_H,kVK_ANSI_I,kVK_ANSI_J,kVK_ANSI_K,kVK_ANSI_L,kVK_ANSI_M,kVK_ANSI_N,kVK_ANSI_O,kVK_ANSI_P,kVK_ANSI_Q,kVK_ANSI_R,kVK_ANSI_S,kVK_ANSI_T,kVK_ANSI_U,kVK_ANSI_V,kVK_ANSI_W,kVK_ANSI_X,kVK_ANSI_Y,kVK_ANSI_Z};
    static const int digits[]={kVK_ANSI_0,kVK_ANSI_1,kVK_ANSI_2,kVK_ANSI_3,kVK_ANSI_4,kVK_ANSI_5,kVK_ANSI_6,kVK_ANSI_7,kVK_ANSI_8,kVK_ANSI_9};
    static const int fn[]={kVK_F1,kVK_F2,kVK_F3,kVK_F4,kVK_F5,kVK_F6,kVK_F7,kVK_F8,kVK_F9,kVK_F10,kVK_F11,kVK_F12,kVK_F13,kVK_F14,kVK_F15,kVK_F16,kVK_F17,kVK_F18,kVK_F19,kVK_F20};
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return letters[key-Qt::Key_A];
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return digits[key-Qt::Key_0];
    if(key>=Qt::Key_F1&&key<=Qt::Key_F20)return fn[key-Qt::Key_F1];
    switch(key){case Qt::Key_Space:return kVK_Space;case Qt::Key_Tab:return kVK_Tab;case Qt::Key_Return:return kVK_Return;case Qt::Key_Escape:return kVK_Escape;case Qt::Key_Left:return kVK_LeftArrow;case Qt::Key_Right:return kVK_RightArrow;case Qt::Key_Up:return kVK_UpArrow;case Qt::Key_Down:return kVK_DownArrow;case Qt::Key_Home:return kVK_Home;case Qt::Key_End:return kVK_End;case Qt::Key_PageUp:return kVK_PageUp;case Qt::Key_PageDown:return kVK_PageDown;default:return -1;}
}
}
quint64 clipboardSequence(){
    if(QGuiApplication::platformName()!=QStringLiteral("cocoa"))return 0;
    @autoreleasepool {return static_cast<quint64>([[NSPasteboard generalPasteboard] changeCount]);}
}
ClipboardInfo clipboardInfo(){
    @autoreleasepool {
        if(QGuiApplication::platformName()!=QStringLiteral("cocoa"))return {};
        ClipboardInfo out;NSPasteboard* p=[NSPasteboard generalPasteboard];
        out.sequence=static_cast<quint64>([p changeCount]);
        for(NSString* type in [p types]){
            QString t=QString::fromUtf8([type UTF8String]);out.formats.push_back(t);
            if(clip::sensitiveMarker(t.toUtf8().toStdString()))out.secret=true;
        }
        NSRunningApplication* app=[[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString* bundle=[app bundleIdentifier];NSString* name=[app localizedName];
        out.source=QString::fromUtf8(bundle?[bundle UTF8String]:"")+" "+QString::fromUtf8(name?[name UTF8String]:"");
        out.secret=out.secret||clip::passwordManager(out.source.toUtf8().toStdString());
        return out;
    }
}
QString platformLimitations(){
    return QStringLiteral("macOS uses pasteboard change-count polling while in the background. Privacy markers are honored; the frontmost app is a heuristic, not a guaranteed pasteboard writer identity.");
}
bool autostartEnabled(){return QFile::exists(agentPath());}
bool setAutostart(bool enabled,QString& error){
    const auto file=agentPath();
    if(!enabled){if(QFile::exists(file)&&!QFile::remove(file)){error=QStringLiteral("Could not remove login agent.");return false;}return true;}
    QDir().mkpath(QFileInfo(file).absolutePath()); QSaveFile out(file);
    if(!out.open(QIODevice::WriteOnly)){error=out.errorString();return false;}
    QXmlStreamWriter x(&out);x.setAutoFormatting(true);x.writeStartDocument();
    x.writeDTD("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
    x.writeStartElement("plist");x.writeAttribute("version","1.0");x.writeStartElement("dict");
    x.writeTextElement("key","Label");x.writeTextElement("string","org.clipdeck.agent");
    x.writeTextElement("key","ProgramArguments");x.writeStartElement("array");
    x.writeTextElement("string",QCoreApplication::applicationFilePath());x.writeTextElement("string","--background");
    if(QCoreApplication::arguments().contains("--portable"))x.writeTextElement("string","--portable");
    x.writeEndElement();x.writeTextElement("key","RunAtLoad");x.writeEmptyElement("true");
    x.writeEndElement();x.writeEndElement();x.writeEndDocument();
    if(x.hasError()||!out.commit()){error=out.errorString();return false;}return true;
}
struct Hotkey::Impl {
    Hotkey* owner;
    EventHotKeyRef ref=nullptr;
    EventHandlerRef handler=nullptr;
    explicit Impl(Hotkey* o):owner(o){}
    static OSStatus callback(EventHandlerCallRef,EventRef event,void* context){
        EventHotKeyID id{};
        if(GetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,nullptr,sizeof(id),nullptr,&id)==noErr&&id.signature==0x434c5044&&id.id==1){
            emit static_cast<Impl*>(context)->owner->activated();return noErr;
        }return eventNotHandledErr;
    }
};
Hotkey::Hotkey(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){
    EventTypeSpec event{kEventClassKeyboard,kEventHotKeyPressed};
    InstallApplicationEventHandler(&Impl::callback,1,&event,impl_.get(),&impl_->handler);
}
Hotkey::~Hotkey(){unregisterShortcut();if(impl_->handler)RemoveEventHandler(impl_->handler);}
void Hotkey::unregisterShortcut(){if(impl_->ref){UnregisterEventHotKey(impl_->ref);impl_->ref=nullptr;}}
bool Hotkey::registerShortcut(const QKeySequence& sequence,QString& error){
    unregisterShortcut();
    if(!impl_->handler||sequence.count()!=1||sequence.isEmpty()){error=QStringLiteral("Choose a single key combination.");return false;}
    auto combo=sequence[0];auto m=combo.keyboardModifiers();auto code=keyCode(combo.key());
    UInt32 mods=0;
    // Qt maps Control to Command and Meta to physical Control on macOS.
    if(m.testFlag(Qt::ControlModifier))mods|=cmdKey;
    if(m.testFlag(Qt::MetaModifier))mods|=controlKey;
    if(m.testFlag(Qt::AltModifier))mods|=optionKey;
    if(m.testFlag(Qt::ShiftModifier))mods|=shiftKey;
    if(code<0||!(mods&(cmdKey|controlKey|optionKey))||RegisterEventHotKey(static_cast<UInt32>(code),mods,{0x434c5044,1},GetApplicationEventTarget(),0,&impl_->ref)!=noErr){
        error=QStringLiteral("Shortcut is unsupported or already registered.");return false;
    }return true;
}
}
