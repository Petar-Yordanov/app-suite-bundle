#pragma once

#include <QString>

// Disk-backed exports are real files that another process can read *after*
// Arky switches archives or exits. Nothing is loaded into QMimeData.
// Exports are owned by Arky and expire after a configurable retention
// window; do not remove them as soon as the clipboard changes.
class ExportStore final {
public:
    static QString root();
    static QString create();
    static void discard(const QString& directory);
    static int prune(int olderThanDays = 7);
};
