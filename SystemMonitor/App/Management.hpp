#pragma once
#include "../Core/Monitor.hpp"
#include <QString>
#include <QStringList>
#include <QVariantList>
namespace management {
QVariantList startup();
QVariantList services();
sm::Result change(QString type,QString id,QString operation);
sm::Result elevated(const sm::Request&);
sm::Result elevatedTree(const sm::Request&);
sm::Result raisePriority();
QStringList commandFor(const sm::Request&);
int helper(const QStringList&);
}
