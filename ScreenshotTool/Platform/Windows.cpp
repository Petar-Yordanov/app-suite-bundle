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
#if __has_include(<winrt/Windows.Graphics.Capture.h>) && __has_include(<windows.graphics.capture.interop.h>)
#define SHOT_WINRT_CAPTURE 1
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <chrono>
#endif

namespace shot::native {
void initialize(){}
bool wayland(){return false;}
bool requestPermission(){return true;}
QString limits(){return "Protected content may be blank. Minimized or non-responsive windows may not be capturable.";}
namespace {
#ifdef SHOT_WINRT_CAPTURE
// Capture the selected window through the compositor, including accelerated
// windows. A single staging texture is read back into the final QImage.
Result graphicsCapture(HWND window,bool cursor,bool& denied){
    using namespace winrt;
    using namespace winrt::Windows::Graphics::Capture;
    using namespace winrt::Windows::Graphics::DirectX;
    using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
    try{init_apartment(apartment_type::multi_threaded);}catch(...){return {{}, "Graphics Capture apartment unavailable."};}
    struct Apartment{~Apartment(){uninit_apartment();}} apartment;
    try{
        if(!GraphicsCaptureSession::IsSupported())return {{}, "Graphics Capture unavailable."};
        com_ptr<ID3D11Device> dev;com_ptr<ID3D11DeviceContext> ctx;
        check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,dev.put(),nullptr,ctx.put()));
        auto dxgi=dev.as<IDXGIDevice>();com_ptr<::IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
        auto device=inspectable.as<IDirect3DDevice>();
        auto interop=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        check_hresult(interop->CreateForWindow(window,guid_of<GraphicsCaptureItem>(),put_abi(item)));
        auto size=item.Size();if(!validSize({size.Width,size.Height}))return {{}, "Capture exceeds the memory limit."};
        auto pool=Direct3D11CaptureFramePool::CreateFreeThreaded(device,DirectXPixelFormat::B8G8R8A8UIntNormalized,1,size);
        auto session=pool.CreateCaptureSession(item);
        struct Cleanup{GraphicsCaptureSession& session;Direct3D11CaptureFramePool& pool;~Cleanup(){try{session.Close();pool.Close();}catch(...){}}} cleanup{session,pool};
        session.IsCursorCaptureEnabled(cursor);session.StartCapture();
        Direct3D11CaptureFrame frame{nullptr};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1200);
        do{frame=pool.TryGetNextFrame();if(frame)break;Sleep(10);}while(std::chrono::steady_clock::now()<deadline);
        if(!frame)return {{}, "Compositor did not return a frame."};
        auto content=frame.ContentSize();if(!validSize({content.Width,content.Height}))return {{}, "Window resized beyond capture limits."};
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> source;check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),source.put_void()));
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if(UINT(content.Width)>desc.Width||UINT(content.Height)>desc.Height)return {{}, "Window resized during capture."};
        desc.Width=UINT(content.Width);desc.Height=UINT(content.Height);desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        com_ptr<ID3D11Texture2D> staging;check_hresult(dev->CreateTexture2D(&desc,nullptr,staging.put()));
        D3D11_BOX box{0,0,0,desc.Width,desc.Height,1};ctx->CopySubresourceRegion(staging.get(),0,0,0,0,source.get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE mapped{};check_hresult(ctx->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
        QImage result(content.Width,content.Height,QImage::Format_RGB32);
        if(!result.isNull())for(int y=0;y<result.height();++y){
            auto* src=reinterpret_cast<const quint32*>(static_cast<const uchar*>(mapped.pData)+size_t(y)*mapped.RowPitch);
            auto* dst=reinterpret_cast<quint32*>(result.scanLine(y));
            for(int x=0;x<result.width();++x)dst[x]=src[x]|0xff000000u;
        }
        ctx->Unmap(staging.get(),0);
        return {result,result.isNull()?QString("Cannot allocate captured image."):QString()};
    }catch(const hresult_error& error){denied=error.code()==E_ACCESSDENIED;return {{},QString::fromWCharArray(error.message().c_str())};}
    catch(...){return {{}, "Windows Graphics Capture failed."};}
}
#endif
QRect rect(RECT r){return {int(r.left),int(r.top),int(r.right-r.left),int(r.bottom-r.top)};}
QString str(const wchar_t* s){return QString::fromWCharArray(s);}
struct Monitor {HMONITOR handle;QString name;QRect bounds;};
BOOL CALLBACK monitorCallback(HMONITOR m,HDC,LPRECT,LPARAM user){
    MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(m,reinterpret_cast<MONITORINFO*>(&info)))reinterpret_cast<QList<Monitor>*>(user)->append({m,str(info.szDevice),rect(info.rcMonitor)});return TRUE;
}
BOOL CALLBACK windowCallback(HWND w,LPARAM user){
    if(!IsWindowVisible(w)||IsIconic(w)||GetWindow(w,GW_OWNER)||GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId())return TRUE;
    wchar_t title[1024]{};if(!GetWindowTextW(w,title,1024))return TRUE;RECT r{};if(!GetWindowRect(w,&r)||!validSize(rect(r).size()))return TRUE;
    reinterpret_cast<QList<Target>*>(user)->append({quintptr(w),str(title),{},rect(r),true});return TRUE;
}
void drawCursor(HDC dc,QPoint origin){
    CURSORINFO ci{};ci.cbSize=sizeof(ci);if(!GetCursorInfo(&ci)||!(ci.flags&CURSOR_SHOWING))return;
    ICONINFO info{};if(!GetIconInfo(ci.hCursor,&info))return;
    DrawIconEx(dc,ci.ptScreenPos.x-origin.x()-int(info.xHotspot),ci.ptScreenPos.y-origin.y()-int(info.yHotspot),ci.hCursor,0,0,0,nullptr,DI_NORMAL);
    if(info.hbmMask)DeleteObject(info.hbmMask);if(info.hbmColor)DeleteObject(info.hbmColor);
}
UINT vk(Qt::Key k){if(k>=Qt::Key_A&&k<=Qt::Key_Z)return UINT(k-Qt::Key_A+'A');if(k>=Qt::Key_0&&k<=Qt::Key_9)return UINT(k-Qt::Key_0+'0');if(k>=Qt::Key_F1&&k<=Qt::Key_F24)return VK_F1+k-Qt::Key_F1;
    switch(k){case Qt::Key_Print:return VK_SNAPSHOT;case Qt::Key_Space:return VK_SPACE;case Qt::Key_Insert:return VK_INSERT;case Qt::Key_Delete:return VK_DELETE;case Qt::Key_Home:return VK_HOME;case Qt::Key_End:return VK_END;case Qt::Key_Left:return VK_LEFT;case Qt::Key_Right:return VK_RIGHT;case Qt::Key_Up:return VK_UP;case Qt::Key_Down:return VK_DOWN;default:return 0;}}
std::atomic<int> ids{0x5300};
}
QList<Target> screens(){
    QList<Monitor> monitors;EnumDisplayMonitors(nullptr,nullptr,monitorCallback,reinterpret_cast<LPARAM>(&monitors));QList<Target> out;
    for(auto* s:QGuiApplication::screens()){
        auto name=s->name();const Monitor* m=nullptr;for(const auto& n:monitors)if(n.name.compare(name,Qt::CaseInsensitive)==0){m=&n;break;}
        if(!m&&monitors.size()==1)m=&monitors.first();if(!m)continue;
        out.append({quintptr(m->handle),name,s->geometry(),m->bounds,false});
    }return out;
}
QList<Target> windows(){QList<Target> out;EnumWindows(windowCallback,reinterpret_cast<LPARAM>(&out));return out;}
Result capture(const Target& target,bool cursor){
    QRect bounds=target.native;HWND window=target.window?reinterpret_cast<HWND>(quintptr(target.id)):nullptr;
    if(window){RECT r{};if(!IsWindow(window)||IsIconic(window)||!GetWindowRect(window,&r))return {{},"Window is no longer available."};bounds=rect(r);}
    else {MONITORINFO info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(reinterpret_cast<HMONITOR>(quintptr(target.id)),&info))return {{},"Monitor was disconnected."};bounds=rect(info.rcMonitor);}
    if(!validSize(bounds.size()))return {{},"Capture exceeds the memory limit."};
    if(window){DWORD affinity=0;if(GetWindowDisplayAffinity(window,&affinity)&&affinity!=WDA_NONE)return {{},"Window content is protected from capture."};}
#ifdef SHOT_WINRT_CAPTURE
    if(window){
        // Keep existing PrintWindow fallback for unsupported capture APIs.
        bool denied=false;auto captured=graphicsCapture(window,cursor,denied);if(denied||!captured.image.isNull())return captured;
    }
#endif
    HDC screen=GetDC(nullptr);if(!screen)return {{},"Cannot acquire the desktop device context."};
    HDC memory=CreateCompatibleDC(screen);BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=bounds.width();bitmap.bmiHeader.biHeight=-bounds.height();bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP dib=CreateDIBSection(screen,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!memory||!dib||!pixels){if(dib)DeleteObject(dib);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);return {{},"Cannot allocate the capture buffer."};}
    HGDIOBJ previous=SelectObject(memory,dib);bool ok=false;
    if(window){ // PrintWindow uses the requested window, never a desktop crop.
        DWORD_PTR ignored=0;if(SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG,500,&ignored))ok=PrintWindow(window,memory,2)!=FALSE;
    }else ok=BitBlt(memory,0,0,bounds.width(),bounds.height(),screen,bounds.x(),bounds.y(),SRCCOPY|CAPTUREBLT)!=FALSE;
    if(ok&&cursor)drawCursor(memory,bounds.topLeft());
    GdiFlush();
    QImage image;if(ok){image=QImage(bounds.size(),QImage::Format_RGB32);if(!image.isNull()){
        auto* src=static_cast<const quint32*>(pixels);for(int y=0;y<image.height();++y){auto* dst=reinterpret_cast<quint32*>(image.scanLine(y));for(int x=0;x<image.width();++x)dst[x]=src[qint64(y)*image.width()+x]|0xff000000u;}
    }}SelectObject(memory,previous);DeleteObject(dib);DeleteDC(memory);ReleaseDC(nullptr,screen);
    return {image,image.isNull()?QString("Capture failed. The window may be protected or unavailable."):QString()};
}
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
