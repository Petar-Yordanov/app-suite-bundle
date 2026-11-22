#include "archive_controller.hpp"
#include "ui_smoke.hpp"
#include "Updater.hpp"
#ifdef _WIN32
#include "windows_virtual_mime.hpp"
#endif

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QTemporaryDir>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QSettings>
#include <QStandardPaths>
#include <QFileOpenEvent>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cstdio>
#include <algorithm>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <filesystem>

namespace {
// A Qt Quick GUI executable built with MSVC's Windows subsystem may not have a
// readable console. Do not rely exclusively on stderr for QML-load errors.
QString startupLogPath() {
    return QDir::tempPath() + QStringLiteral("/Arky-startup.log");
}

void appendStartupLog(const QString& message) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    QFile log(startupLogPath());
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const QByteArray bytes = (message + QLatin1Char('\n')).toUtf8();
    log.write(bytes);
}

void qtStartupMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const QString severity = type == QtDebugMsg ? QStringLiteral("DEBUG")
        : type == QtInfoMsg ? QStringLiteral("INFO")
        : type == QtWarningMsg ? QStringLiteral("WARN")
        : type == QtCriticalMsg ? QStringLiteral("ERROR")
        : QStringLiteral("FATAL");
    appendStartupLog(severity + QStringLiteral(": ") + message);
    const QByteArray utf8 = message.toUtf8();
    std::fprintf(stderr, "Qt %s: %s\n", severity.toUtf8().constData(), utf8.constData());
    std::fflush(stderr);
}

QString startupLogTail() {
    QFile log(startupLogPath());
    if (!log.open(QIODevice::ReadOnly))
        return QStringLiteral("No startup log was captured.");

    const QStringList lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList important;
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("WARN:")) ||
            line.startsWith(QStringLiteral("ERROR:")) ||
            line.startsWith(QStringLiteral("FATAL:")) ||
            line.startsWith(QStringLiteral("QML:"))) {
            important.append(line);
        }
    }
    // QML import tracing can create thousands of DEBUG lines. Keep all of
    // those in the logfile, but show the actual errors in the startup dialog.
    const QStringList& selected = important.isEmpty() ? lines : important;
    const qsizetype first = selected.size() > 12 ? selected.size() - 12 : 0;
    return selected.mid(first).join(QLatin1Char('\n'));
}

void showStartupFailure(const QString& details) {
#ifdef _WIN32
    const std::wstring wide = details.toStdWString();
    MessageBoxW(nullptr, wide.c_str(), L"Arky could not start", MB_OK | MB_ICONERROR);
#else
    const QByteArray utf8 = details.toUtf8();
    std::fprintf(stderr, "%s\n", utf8.constData());
#endif
}

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto utf8 = path.toUtf8();
    return std::filesystem::path(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
#endif
}
class FileOpenRouter final : public QObject {
public:
    explicit FileOpenRouter(ArchiveController& backend) : backend_(backend) {}
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() != QEvent::FileOpen) return false;
        const auto* open = static_cast<QFileOpenEvent*>(event);
        if (open->url().isLocalFile()) {
            const auto url = open->url();
            QTimer::singleShot(0, &backend_, [this, url]{ backend_.openUrl(url); });
        }
        return true;
    }
private:
    ArchiveController& backend_;
};
}

int main(int argc, char* argv[]) {
    QElapsedTimer startup;
    startup.start();
    // Opt-in detailed QML import tracing for troubleshooting.
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--diagnose")) {
            qputenv("QML_IMPORT_TRACE", "1");
            break;
        }
    }
    // Truncate the previous run's log before installing Qt's diagnostic handler.
    {
        QFile log(startupLogPath());
        log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }
    qInstallMessageHandler(qtStartupMessageHandler);
    appendStartupLog(QStringLiteral("Arky startup initiated"));
    QGuiApplication app(argc, argv);
    appendStartupLog(QStringLiteral("Executable: ") + QCoreApplication::applicationFilePath());
#ifdef _WIN32
    // Converter lives for the whole app, including ongoing Shell data requests.
    WindowsVirtualMimeConverter shellVirtualFiles;
#endif
    QGuiApplication::setOrganizationName("Arky");
    QGuiApplication::setApplicationName("Arky");
    QGuiApplication::setApplicationDisplayName("Arky");
    QGuiApplication::setApplicationVersion("0.9.0");
    // Qt Quick Controls uses qrc:/qtquickcontrols2.conf (Basic style).

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#152234"));
    palette.setColor(QPalette::WindowText, QColor("#e7f0fb"));
    palette.setColor(QPalette::Base, QColor("#111e2e"));
    palette.setColor(QPalette::AlternateBase, QColor("#1b2a3e"));
    palette.setColor(QPalette::Text, QColor("#e7f0fb"));
    palette.setColor(QPalette::Button, QColor("#223750"));
    palette.setColor(QPalette::ButtonText, QColor("#e7f0fb"));
    palette.setColor(QPalette::Highlight, QColor("#58abf5"));
    palette.setColor(QPalette::HighlightedText, QColor("#071421"));
    app.setPalette(palette);

    const bool smoke = app.arguments().contains("--smoke-test");
    const bool benchmark = app.arguments().contains("--benchmark-ui");
    // This mode validates that Qt Quick can load the actual UI, and that the
    // QObject/QML bridge can asynchronously index a real ZIP archive.
    // Run on the target OS: xmake run Arky --smoke-test
    QTemporaryDir testDir;
    QString testArchive;
    if (smoke) {
        if (!testDir.isValid()) {
            std::fprintf(stderr, "SMOKE FAIL: could not create temporary directory\n");
            return 2;
        }
        const auto source = testDir.filePath("arky-smoke.txt");
        QFile file(source);
        if (!file.open(QIODevice::WriteOnly) || file.write("Arky Qt/QML smoke fixture\n") < 0) {
            std::fprintf(stderr, "SMOKE FAIL: could not write test file\n");
            return 2;
        }
        file.close();
        testArchive = testDir.filePath("arky-smoke.zip");
        const auto result = arky::ArchiveEngine::create(nativePath(testArchive), {nativePath(source)});
        if (!result.ok) {
            std::fprintf(stderr, "SMOKE FAIL: could not create ZIP: %s\n", result.error.c_str());
            return 2;
        }
    }

    if (smoke || benchmark || app.arguments().contains("--portable")) {
        QDir data(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
        if (data.dirName() == "MacOS") { data.cdUp(); data.cdUp(); data.cdUp(); }
#endif
        const auto profile = (smoke || benchmark) ? testDir.filePath("profile") : data.filePath("Arky-data");
        if (!QDir().mkpath(profile)) return 2;
        qputenv("ARKY_DATA_DIR", profile.toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile);
    }
    ArchiveController backend;
    if (smoke) backend.setDeletePolicy(2);
    const auto data = qEnvironmentVariableIsSet("ARKY_DATA_DIR") ? qEnvironmentVariable("ARKY_DATA_DIR") :
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    Updater updater(QDir(data).filePath("updates"));
    QString configured = backend.updateUrl();
    if (!smoke && !benchmark) updater.configure(configured);
    QObject::connect(&backend, &ArchiveController::settingsChanged, &app, [&] {
        if (configured == backend.updateUrl()) return;
        configured = backend.updateUrl();
        if (!smoke && !benchmark) updater.configure(configured);
    });
    FileOpenRouter openEvents(backend);
    app.installEventFilter(&openEvents);
    QQmlApplicationEngine engine;
    // Some Windows deployment layouts place QML imports in a 'qml' folder
    // next to the executable. Qt does not always search that folder by default.
    const QString localQml = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
    if (QDir(localQml).exists())
        engine.addImportPath(localQml);
#ifdef _WIN32
    // With Xmake's cached Qt SDK, the actual Qt6Core.dll may live in an
    // installed SDK whose prefix differs from Qt's compiled-in import path.
    // Resolve the *loaded* QtCore binary (not an assumed username/cache path).
    if (HMODULE coreModule = GetModuleHandleW(L"Qt6Core.dll")) {
        std::wstring loadedPath(32768, L'\0');
        const DWORD pathLength = GetModuleFileNameW(coreModule, loadedPath.data(),
                                                     static_cast<DWORD>(loadedPath.size()));
        if (pathLength > 0 && pathLength < loadedPath.size()) {
            loadedPath.resize(pathLength);
            const QString binDirectory = QFileInfo(QString::fromStdWString(loadedPath)).absolutePath();
            const QString sdkQml = QDir(binDirectory).absoluteFilePath(QStringLiteral("../qml"));
            if (QDir(sdkQml).exists()) {
                engine.addImportPath(sdkQml);
                appendStartupLog(QStringLiteral("Loaded-Qt SDK QML imports: ") + sdkQml);
            }
        }
    }
#endif
    appendStartupLog(QStringLiteral("Qt QML import directory: ") + QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    appendStartupLog(QStringLiteral("Built-in QML resource present: ") +
                     (QFile::exists(QStringLiteral(":/qml/main.qml")) ? QStringLiteral("yes") : QStringLiteral("no")));
    appendStartupLog(QStringLiteral("QML search paths: ") + engine.importPathList().join(QStringLiteral("; ")));
    for (const QString& required : {QStringLiteral("QtQuick/Controls/qmldir"),
                                    QStringLiteral("QtQuick/Dialogs/qmldir"),
                                    QStringLiteral("QtQuick/Layouts/qmldir")}) {
        bool located = false;
        for (const QString& root : engine.importPathList()) {
            if (QFile::exists(QDir(root).filePath(required))) {
                located = true;
                break;
            }
        }
        appendStartupLog(QStringLiteral("QML module ") + required +
                         (located ? QStringLiteral(": found") : QStringLiteral(": not found in filesystem search paths")));
    }
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            const auto text = warning.toString().toUtf8();
            std::fprintf(stderr, "QML: %s\n", text.constData());
            appendStartupLog(QStringLiteral("QML: ") + warning.toString());
        }
    });
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("updater", &updater);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        appendStartupLog(QStringLiteral("ERROR: QML main window could not be loaded"));
        const QString details = QStringLiteral(
            "Qt failed to load Arky's main QML window.\n\n"
            "Warnings and errors:\n%1\n\n"
            "Full log: %2")
            .arg(startupLogTail(), startupLogPath());
        if (!smoke)
            showStartupFailure(details);
        std::fprintf(stderr, "Arky startup failure. Log: %s\n", startupLogPath().toUtf8().constData());
        return 1;
    }
    appendStartupLog(QStringLiteral("QML main window loaded successfully"));

    if (smoke) {
        // The smoke harness drives the same QML-backed controller as the UI.
        // It checks each finished async operation, not intermediate model resets.
        const auto extracted = testDir.filePath("extracted");
        if (!QDir().mkpath(extracted)) {
            std::fprintf(stderr, "SMOKE FAIL: cannot create extraction destination\n");
            return 2;
        }
        return runUiSmoke(app, backend, testArchive,
                          testDir.filePath("arky-smoke.txt"), extracted);
    }

    const auto arguments = app.arguments();
    if (benchmark) {
        const auto index = arguments.indexOf("--benchmark-ui");
        if (index + 2 >= arguments.size()) return 2;
        const auto archive = QFileInfo(arguments[index+1]).absoluteFilePath();
        const auto output = QFileInfo(arguments[index+2]).absoluteFilePath();
        const qint64 uiReady = startup.elapsed();
        qint64 firstBatch = -1, began = startup.elapsed(), previous = began, maxGap = 0, samples = 0;
        QTimer probe;
        probe.setTimerType(Qt::PreciseTimer);
        probe.setInterval(16);
        QObject::connect(&probe, &QTimer::timeout, &app, [&] {
            const auto now = startup.elapsed();
            maxGap = std::max(maxGap, now - previous);
            previous = now; ++samples;
        });
        QObject::connect(&backend, &ArchiveController::failed, &app, [&app](const QString&) { app.exit(1); });
        QObject::connect(&backend, &ArchiveController::archiveChanged, &app, [&] {
            if (backend.count() > 0 && firstBatch < 0) firstBatch = startup.elapsed()-began;
            if (backend.busy() || backend.archivePath() != archive) return;
            probe.stop();
            maxGap = std::max(maxGap, startup.elapsed()-previous);
            QJsonObject result{{"uiReadyMs", static_cast<double>(uiReady)}, {"firstBatchMs", static_cast<double>(firstBatch)},
                               {"listingMs", static_cast<double>(startup.elapsed()-began)}, {"maxEventLoopGapMs", static_cast<double>(maxGap)},
                               {"timerSamples", static_cast<double>(samples)}, {"rows", backend.count()}};
            QSaveFile file(output);
            if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson()) < 0 || !file.commit()) { app.exit(2); return; }
            app.quit();
        });
        probe.start();
        QTimer::singleShot(0, &backend, [&backend, archive]{ backend.openUrl(QUrl::fromLocalFile(archive)); });
        return app.exec();
    }
    if (arguments.contains("--extract-here")) {
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = QObject::connect(&backend, &ArchiveController::archiveChanged, &app, [&backend, connection] {
            if (backend.busy() || backend.archivePath().isEmpty()) return;
            QObject::disconnect(*connection);
            QTimer::singleShot(0, &backend, [&backend]{ backend.extractHere(); });
        });
    }
    for (int i = 1; i < arguments.size(); ++i)
        if (!arguments.at(i).startsWith(QStringLiteral("--")))
            backend.openUrl(QUrl::fromLocalFile(QFileInfo(arguments.at(i)).absoluteFilePath()));
    return app.exec();
}
