#pragma once
#include "../Core/Capture.hpp"
#include <QObject>
#include <QKeySequence>
#include <memory>

namespace shot::native {
void initialize();
bool wayland();
QList<Target> screens();
QList<Target> windows();
Result capture(const Target&,bool cursor); // worker thread; no GUI objects
bool requestPermission(); // GUI thread; only in response to a capture request
QString limits();
bool autostartEnabled();
bool setAutostart(bool,QString& error);

class Hotkey : public QObject {
    Q_OBJECT
public:
    explicit Hotkey(QObject* parent=nullptr);
    ~Hotkey() override;
    bool bind(const QKeySequence&,QString& error);
    void unbind();
signals:
    void activated();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
