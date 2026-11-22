#pragma once
#include "../Core/Geometry.hpp"
#include <QList>
#include <QRect>
#include <QObject>
#include <QKeySequence>
#include <memory>
#include <functional>

namespace rec {
struct Target {
    quint64 id=0;
    QString name;
    QRect logical;
    QRect native;
    bool window=false;
    int hardwareIndex=-1; // DXGI output index on the default Windows adapter
};
inline bool validSize(QSize size) {
    return validPixels(size.width(),size.height());
}
namespace native {
void initialize();
bool wayland();
QList<Target> screens();
QList<Target> windows();
bool requestPermission();
#ifdef Q_OS_MACOS
void requestMicrophonePermission(QObject* context,std::function<void(bool)>);
#endif
QString limits();
bool autostartEnabled();
bool setAutostart(bool,QString&);
class Hotkey : public QObject {
    Q_OBJECT
public:
    explicit Hotkey(QObject* parent=nullptr);
    ~Hotkey() override;
    bool bind(const QKeySequence&,QString&);
    void unbind();
signals:
    void activated();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
}
