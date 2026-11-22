#include "Capture.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <thread>
#include <chrono>

namespace rec::native {
namespace {
using Microsoft::WRL::ComPtr;
class Loopback final:public Capture {
    std::shared_ptr<Mailbox> audio_;
    std::jthread worker_;
    void run(std::stop_token stop){
        auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(hr)){audio_->fail("WASAPI COM initialization failed.");return;}
        struct Cleanup{~Cleanup(){CoUninitialize();}} cleanup;
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> endpoint;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> reader;
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;
        format.nSamplesPerSec=48000;format.wBitsPerSample=32;
        format.nBlockAlign=8;format.nAvgBytesPerSec=384000;
        hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                            IID_PPV_ARGS(enumerator.GetAddressOf()));
        if(SUCCEEDED(hr))hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,endpoint.GetAddressOf());
        if(SUCCEEDED(hr))hr=endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
                                               reinterpret_cast<void**>(client.GetAddressOf()));
        if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                     AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                     2000000,0,&format,nullptr);
        if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(reader.GetAddressOf()));
        if(SUCCEEDED(hr))hr=client->Start();
        if(FAILED(hr)){audio_->fail(QString("Unable to capture default Windows playback device (0x%1).").arg(quint32(hr),8,16,QChar('0')));return;}
        while(!stop.stop_requested()){
            UINT32 packets=0;
            hr=reader->GetNextPacketSize(&packets);
            while(SUCCEEDED(hr)&&packets&&!stop.stop_requested()){
                BYTE* data=nullptr;UINT32 frames=0;DWORD flags=0;
                hr=reader->GetBuffer(&data,&frames,&flags,nullptr,nullptr);
                if(FAILED(hr))break;
                const auto n=qsizetype(frames)*8;
                if(flags&AUDCLNT_BUFFERFLAGS_SILENT){QByteArray silence(n,'\0');audio_->audio(silence.constData(),n);}
                else if(data)audio_->audio(reinterpret_cast<const char*>(data),n);
                hr=reader->ReleaseBuffer(frames);
                if(SUCCEEDED(hr))hr=reader->GetNextPacketSize(&packets);
            }
            if(FAILED(hr)){audio_->fail("Windows loopback device was disconnected or failed.");break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        client->Stop();
    }
public:
    explicit Loopback(std::shared_ptr<Mailbox> audio):audio_(std::move(audio)),worker_([this](std::stop_token token){run(token);}){}
};
}
std::unique_ptr<Capture> loopback(std::shared_ptr<Mailbox> audio){return std::make_unique<Loopback>(std::move(audio));}
}
