#include "ui_smoke.hpp"
#include "archive_controller.hpp"
#include "archive_model.hpp"

#include <QAbstractItemModel>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <memory>

namespace {
enum class Phase { Opening, Testing, Extracting, Renaming, Copying, Removing, CreatingFolder, Finished };

QByteArray fileContents(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString pathAt(const ArchiveController& backend, int row) {
    // The exposed model is a folder proxy. Query its role, not an internal
    // ArchiveModel index: this also catches proxy mapping regressions.
    const auto* model = const_cast<ArchiveController&>(backend).rows();
    return model->index(row, 0).data(ArchiveModel::PathRole).toString();
}
}

int runUiSmoke(QGuiApplication& app, ArchiveController& backend,
               const QString& archivePath, const QString& fixturePath,
               const QString& extractionRoot) {
    auto phase = std::make_shared<Phase>(Phase::Opening);
    auto finished = std::make_shared<bool>(false);
    const auto originalBytes = fileContents(fixturePath);
    if (originalBytes.isEmpty()) {
        std::fprintf(stderr, "SMOKE FAIL: missing input fixture\n");
        return 10;
    }

    auto fail = [&app, finished](const QString& reason, int code) {
        if (*finished) return;
        *finished = true;
        const auto utf8 = reason.toUtf8();
        std::fprintf(stderr, "SMOKE FAIL: %s\n", utf8.constData());
        app.exit(code);
    };
    auto succeed = [&app, finished] {
        if (*finished) return;
        *finished = true;
        std::fprintf(stderr,
            "SMOKE PASS: QML loaded, open/test/extract/rename/copy/delete/new-folder succeeded\n");
        app.exit(0);
    };
    QObject::connect(&backend, &ArchiveController::failed, &app,
        [fail](const QString& error) { fail(error, 11); });

    // archiveChanged is emitted both on the *start* of loading (model cleared)
    // and when the index finishes. It also fires during batched indexing.
    // Never treat the initial empty model as a completed operation.
    QObject::connect(&backend, &ArchiveController::archiveChanged, &app,
        [&app, &backend, phase, finished, archivePath, originalBytes, fail, succeed] {
            if (*finished || backend.busy() || backend.archivePath() != archivePath) return;
            switch (*phase) {
            case Phase::Opening:
                if (backend.count() != 1 || pathAt(backend, 0) != "arky-smoke.txt") {
                    fail("opened ZIP has unexpected entries", 12);
                    return;
                }
                *phase = Phase::Testing;
                QTimer::singleShot(0, &backend, [&backend] { backend.testArchive(); });
                break;
            case Phase::Renaming:
                if (backend.count() != 1 || pathAt(backend, 0) != "arky-renamed.txt") {
                    fail("rename failed or new entry wasn't indexed", 13);
                    return;
                }
                backend.toggleVisible(0);
                if (backend.selectedCount() != 1) { fail("selection model failed", 14); return; }
                *phase = Phase::Copying;
                QTimer::singleShot(0, &backend, [&backend] { backend.copySelection(); });
                break;
            case Phase::Removing:
                if (backend.count() != 0) { fail("remove did not clear the archive", 15); return; }
                *phase = Phase::CreatingFolder;
                QTimer::singleShot(0, &backend, [&backend] { backend.newFolder("empty-folder"); });
                break;
            case Phase::CreatingFolder:
                if (backend.count() != 1 || pathAt(backend, 0) != "empty-folder") {
                    fail("new folder not present after editing", 16);
                    return;
                }
                succeed();
                break;
            default:
                break;
            }
        });

    QObject::connect(&backend, &ArchiveController::completed, &app,
        [&app, &backend, phase, finished, fixturePath, extractionRoot, originalBytes, fail](const QString&) {
            if (*finished) return;
            if (*phase == Phase::Testing) {
                *phase = Phase::Extracting;
                QTimer::singleShot(0, &backend, [&backend, extractionRoot] {
                    backend.extractTo(QUrl::fromLocalFile(extractionRoot), false);
                });
            } else if (*phase == Phase::Extracting) {
                const auto extracted = QDir(extractionRoot).filePath("arky-smoke.txt");
                if (fileContents(extracted) != originalBytes) {
                    fail("extracted file does not match original bytes", 17);
                    return;
                }
                backend.toggleVisible(0);
                *phase = Phase::Renaming;
                QTimer::singleShot(0, &backend, [&backend] {
                    backend.renameSelected("arky-renamed.txt");
                });
            }
        });

    QObject::connect(&backend, &ArchiveController::preparedChanged, &app,
        [&app, &backend, phase, finished, originalBytes, fail] {
            if (*finished || *phase != Phase::Copying || !backend.prepared()) return;
            QTimer::singleShot(0, &backend, [&backend, phase, finished, originalBytes, fail] {
                if (*finished || *phase != Phase::Copying) return;
                const auto* mime = QGuiApplication::clipboard()->mimeData();
                if (!mime || !mime->hasUrls() ||
                    !mime->hasFormat("application/vnd.arky.entries+json") ||
                    mime->urls().size() != 1 ||
                    fileContents(mime->urls().first().toLocalFile()) != originalBytes) {
                    fail("clipboard MIME/file URL roundtrip failed", 18);
                    return;
                }
                *phase = Phase::Removing;
                backend.removeSelected();
            });
        });

    QTimer::singleShot(0, &backend, [&backend, archivePath] {
        backend.openUrl(QUrl::fromLocalFile(archivePath));
    });
    QTimer::singleShot(90000, &app, [fail, phase] {
        fail(QString("timed out during stage %1").arg(static_cast<int>(*phase)), 19);
    });
    return app.exec();
}
