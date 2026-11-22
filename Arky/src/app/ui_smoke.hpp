#pragma once

class QGuiApplication;
class ArchiveController;
class QString;

// Executes a non-interactive Qt Quick + controller workflow using the real
// QML window. Returns the Qt event-loop exit code (zero on success).
int runUiSmoke(QGuiApplication& app, ArchiveController& backend,
               const QString& archivePath, const QString& fixturePath,
               const QString& extractionRoot);
