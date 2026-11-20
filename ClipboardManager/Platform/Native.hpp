#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QKeySequence>
#include <memory>

namespace native {
struct ClipboardInfo {
    quint64 sequence = 0;
    bool secret = false;
    QString source;
    QStringList formats;
};
ClipboardInfo clipboardInfo();
bool setAutostart(bool enabled, QString& error);
bool autostartEnabled();
QString platformLimitations();

class Hotkey : public QObject {
    Q_OBJECT
public:
    explicit Hotkey(QObject* parent=nullptr);
    ~Hotkey() override;
    bool registerShortcut(const QKeySequence&, QString& error);
    void unregisterShortcut();
signals:
    void activated();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
