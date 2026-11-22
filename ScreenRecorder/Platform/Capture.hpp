#pragma once
#include "Native.hpp"
#include "../Core/Feed.hpp"
#include "../Core/RecordingPlan.hpp"
#include <memory>

namespace rec::native {
// One native stream per segment; ownership keeps callbacks alive until stop.
class Capture {
public:
    virtual ~Capture()=default;
};
#ifdef Q_OS_WIN
std::unique_ptr<Capture> loopback(std::shared_ptr<Mailbox> audio);
#endif
#ifdef Q_OS_MACOS
std::unique_ptr<Capture> screenStream(const Target&,QRect region,Size output,int fps,bool cursor,
                                     std::shared_ptr<Mailbox> video,std::shared_ptr<Mailbox> audio);
#endif
}
