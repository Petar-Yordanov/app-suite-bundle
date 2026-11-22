#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include "../Core/Pty.hpp"

struct Shell {
    QString id,name,executable;
    QStringList arguments;
    QString distribution;
};
QVector<Shell> detectShells();
QVector<Shell> wslShells(const QByteArray& output,QString executable);
term::Launch launchFor(const Shell&,QString cwd,int rows,int cols);
