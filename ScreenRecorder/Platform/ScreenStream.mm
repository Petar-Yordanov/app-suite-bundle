#include "Capture.hpp"
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cstring>
#include <vector>

@interface RecorderOutput : NSObject <SCStreamOutput,SCStreamDelegate> {
@public
    std::shared_ptr<rec::Mailbox> video;
    std::shared_ptr<rec::Mailbox> audio;
    int width;
    int height;
}
@end

@implementation RecorderOutput
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    if(video)video->fail(QString::fromUtf8(error.localizedDescription.UTF8String));
}
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)kind {
    @autoreleasepool {
        if(!CMSampleBufferIsValid(sample)||!CMSampleBufferDataIsReady(sample))return;
        if(kind==SCStreamOutputTypeScreen&&video){
            auto pixel=CMSampleBufferGetImageBuffer(sample);
            if(!pixel||CVPixelBufferGetWidth(pixel)!=size_t(width)||CVPixelBufferGetHeight(pixel)!=size_t(height))return;
            if(CVPixelBufferLockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess)return;
            auto* src=static_cast<const char*>(CVPixelBufferGetBaseAddress(pixel));
            const auto stride=CVPixelBufferGetBytesPerRow(pixel);
            if(src&&stride>=size_t(width)*4){
                QByteArray frame(qsizetype(width)*height*4,Qt::Uninitialized);
                for(int y=0;y<height;++y)std::memcpy(frame.data()+qsizetype(y)*width*4,src+y*stride,size_t(width)*4);
                video->video(std::move(frame));
            }
            CVPixelBufferUnlockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
        }else if(kind==SCStreamOutputTypeAudio&&audio){
            auto desc=CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
            if(!desc||desc->mFormatID!=kAudioFormatLinearPCM||
                !(desc->mFormatFlags&kAudioFormatFlagIsFloat)||desc->mBitsPerChannel!=32||
                desc->mChannelsPerFrame!=2||desc->mSampleRate!=48000){
                audio->fail("Unexpected ScreenCaptureKit audio format.");return;
            }
            const auto count=CMSampleBufferGetNumSamples(sample);
            if(count<=0||count>48000)return;
            struct {UInt32 mNumberBuffers; AudioBuffer mBuffers[2];} storage{};
            auto* list=reinterpret_cast<AudioBufferList*>(&storage);
            CMBlockBufferRef block=nullptr;
            auto status=CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample,nullptr,list,
                sizeof(storage),kCFAllocatorDefault,kCFAllocatorDefault,
                kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,&block);
            if(status!=noErr){if(block)CFRelease(block);return;}
            if(list->mNumberBuffers==1&&list->mBuffers[0].mData&&list->mBuffers[0].mDataByteSize>=count*8)
                audio->audio(static_cast<const char*>(list->mBuffers[0].mData),count*8);
            else if(list->mNumberBuffers==2&&list->mBuffers[0].mData&&list->mBuffers[1].mData&&
                list->mBuffers[0].mDataByteSize>=count*4&&list->mBuffers[1].mDataByteSize>=count*4){
                const auto* left=static_cast<const float*>(list->mBuffers[0].mData);
                const auto* right=static_cast<const float*>(list->mBuffers[1].mData);
                std::vector<float> stereo(size_t(count)*2);
                for(CMItemCount i=0;i<count;++i){stereo[size_t(i)*2]=left[i];stereo[size_t(i)*2+1]=right[i];}
                audio->audio(reinterpret_cast<const char*>(stereo.data()),count*8);
            }
            if(block)CFRelease(block);
        }
    }
}
@end

namespace rec::native {
namespace {
struct StreamState {
    std::atomic<bool> stopped=false;
    SCStream* stream=nil;
    RecorderOutput* receiver=nil;
    dispatch_queue_t control=dispatch_queue_create("org.appsuite.recorder.control",DISPATCH_QUEUE_SERIAL);
};
class StreamCapture final:public Capture {
    std::shared_ptr<StreamState> state_;
public:
    explicit StreamCapture(std::shared_ptr<StreamState> state):state_(std::move(state)){}
    ~StreamCapture() override {
        auto state=state_;state->stopped=true;
        dispatch_async(state->control,^{
            if(state->stream)[state->stream stopCaptureWithCompletionHandler:^(NSError*){}];
            state->stream=nil;state->receiver=nil;
        });
    }
};
}
std::unique_ptr<Capture> screenStream(const Target& target,QRect region,Size output,int fps,bool cursor,
                                     std::shared_ptr<Mailbox> video,std::shared_ptr<Mailbox> audio){
    auto state=std::make_shared<StreamState>();
    const Target source=target;
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
        completionHandler:^(SCShareableContent* content,NSError* error){
        dispatch_async(state->control,^{
            @autoreleasepool {
                if(state->stopped)return;
                if(!content){video->fail(error?QString::fromUtf8(error.localizedDescription.UTF8String):QString("Screen Recording permission denied."));return;}
                SCContentFilter* filter=nil;
                if(source.window){
                    for(SCWindow* w in content.windows)if(w.windowID==source.id){
                        filter=[[SCContentFilter alloc]initWithDesktopIndependentWindow:w];break;
                    }
                }else for(SCDisplay* d in content.displays)if(d.displayID==source.id){
                    filter=[[SCContentFilter alloc]initWithDisplay:d excludingWindows:@[]];break;
                }
                if(!filter){video->fail("Selected capture source is unavailable.");return;}
                SCStreamConfiguration* config=[[SCStreamConfiguration alloc]init];
                config.width=output.w;config.height=output.h;
                config.pixelFormat=kCVPixelFormatType_32BGRA;
                config.minimumFrameInterval=CMTimeMake(1,fps);
                config.queueDepth=3;config.showsCursor=cursor;
                if(@available(macOS 14.0,*))config.ignoreShadowsSingleWindow=YES;
                if(!source.window&&!region.isEmpty()){
                    // Source rectangle uses display-relative points, not Retina pixels.
                    const double sx=double(source.logical.width())/source.native.width();
                    const double sy=double(source.logical.height())/source.native.height();
                    config.sourceRect=CGRectMake(region.x()*sx,region.y()*sy,region.width()*sx,region.height()*sy);
                }
                config.capturesAudio=bool(audio);
                config.sampleRate=48000;config.channelCount=2;
                config.excludesCurrentProcessAudio=YES;
                RecorderOutput* receiver=[[RecorderOutput alloc]init];
                receiver->video=video;receiver->audio=audio;receiver->width=output.w;receiver->height=output.h;
                state->receiver=receiver;
                state->stream=[[SCStream alloc]initWithFilter:filter configuration:config delegate:receiver];
                NSError* failure=nil;
                auto queue=dispatch_queue_create("org.appsuite.recorder.samples",DISPATCH_QUEUE_SERIAL);
                if(![state->stream addStreamOutput:receiver type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&failure]||
                   (audio&&![state->stream addStreamOutput:receiver type:SCStreamOutputTypeAudio sampleHandlerQueue:queue error:&failure])){
                    video->fail(failure?QString::fromUtf8(failure.localizedDescription.UTF8String):QString("Cannot initialize ScreenCaptureKit output."));return;
                }
                [state->stream startCaptureWithCompletionHandler:^(NSError* e){
                    if(e&&!state->stopped)video->fail(QString::fromUtf8(e.localizedDescription.UTF8String));
                }];
            }
        });
    }];
    return std::make_unique<StreamCapture>(state);
}
}
