#include "archive_controller.hpp"
#include "mime_bridge.hpp"
#include "export_store.hpp"
#include "transfer_plan.hpp"
#ifdef _WIN32
#include "windows_virtual_mime.hpp"
#include <Windows.h>
#endif

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QLocale>
#include <QTimer>
#include <QMimeData>
#include <QPointer>
#include <QQuickWindow>
#include <QSemaphore>
#include <QDrag>
#include <QFile>
#include <QKeySequence>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
std::string utf8(const QString& s) {
    const auto data = s.toUtf8();
    return {data.constData(), static_cast<std::size_t>(data.size())};
}
std::filesystem::path nativePath(const QString& s) {
#ifdef _WIN32
    return std::filesystem::path(s.toStdWString());
#else
    return std::filesystem::path(utf8(s));
#endif
}
QString errorOf(const arky::Result& result) { return QString::fromUtf8(result.error.c_str()); }
QString prettyBytes(std::uint64_t n) {
    return QLocale().formattedDataSize(static_cast<qint64>((std::min<std::uint64_t>)(n, static_cast<std::uint64_t>((std::numeric_limits<qint64>::max)()))));
}
}

void ArchiveFolderProxy::setFolder(const QString& folder) {
    if (folder_ == folder) return;
    folder_ = folder;
    invalidateFilter();
}
void ArchiveFolderProxy::setSearch(const QString& search) {
    if (search_ == search) return;
    search_ = search;
    invalidateFilter();
}
bool ArchiveFolderProxy::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const auto path = sourceModel()->index(row, 0, parent).data(ArchiveModel::PathRole).toString();
    const auto slash = path.lastIndexOf('/');
    const auto currentParent = slash < 0 ? QString() : path.left(slash);
    if (!tree_ && search_.isEmpty() && currentParent != folder_) return false;
    if (search_.isEmpty()) return true;
    return sourceModel()->index(row, 0, parent).data(ArchiveModel::SearchRole).toString()
        .contains(search_, Qt::CaseInsensitive);
}

bool ArchiveFolderProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (tree_) return left.data(ArchiveModel::PathRole).toString() < right.data(ArchiveModel::PathRole).toString();
    const bool a = left.data(ArchiveModel::KindRole).toString() == "Folder";
    const bool b = right.data(ArchiveModel::KindRole).toString() == "Folder";
    if (a != b) return a;
    return QSortFilterProxyModel::lessThan(left, right);
}
ArchiveController::ArchiveController(QObject* parent) : QObject(parent) {
    ExportStore::prune();
    proxy_.setSourceModel(&model_);
    proxy_.setDynamicSortFilter(true);
    proxy_.setOrdering(ArchiveModel::NameRole, false);
    connect(&proxy_, &QAbstractItemModel::modelReset, this, &ArchiveController::visibleCountChanged);
    connect(&proxy_, &QAbstractItemModel::rowsInserted, this, &ArchiveController::visibleCountChanged);
    connect(&proxy_, &QAbstractItemModel::rowsRemoved, this, &ArchiveController::visibleCountChanged);

    connect(&model_, &ArchiveModel::selectionChanged, this, [this] {
        emit selectionChanged();
        // Keeping already exported files available makes external drag robust.
    });
}
ArchiveController::~ArchiveController() {
    shuttingDown_ = true;
    pending_.clear();
    cancel();
    if (thread_) thread_->wait(); // Protect worker callbacks from object destruction.
    for (const auto& tab : tabs_) if (!tab.temporaryDirectory.isEmpty()) ExportStore::discard(tab.temporaryDirectory);
}
QString ArchiveController::archiveName() const {
    return archivePath_.isEmpty() ? QStringLiteral("No archive open") : QFileInfo(archivePath_).fileName();
}
bool ArchiveController::editable() const {
    return activeTab_ >= 0 && activeTab_ < static_cast<int>(tabs_.size()) && tabs_[activeTab_].editable && !loadingArchive_;
}
QStringList ArchiveController::tabs() const {
    QStringList names;
    for (const auto& tab : tabs_) names << QFileInfo(tab.path).fileName();
    return names;
}
QString ArchiveController::properties() const {
    if (archivePath_.isEmpty()) return "No archive open";
    std::uint64_t total = 0, compressed = 0;
    bool known = !loadingArchive_, compressedKnown = !loadingArchive_;
    for (const auto& item : model_.allEntries()) {
        if (item.type != arky::EntryType::Regular) continue;
        if (item.sizeKnown) total += item.size; else known = false;
        if (item.compressedSizeKnown) compressed += item.compressedSize; else compressedKnown = false;
    }
    return QString("%1\nArchive file size: %2\nUncompressed file data: %3\nCompressed file data: %4\nIndexed entries: %5\nModification: %6")
        .arg(archivePath_, prettyBytes(QFileInfo(archivePath_).size()),
             known ? prettyBytes(total) : QString("Unknown"),
             compressedKnown ? prettyBytes(compressed) : QString("Unknown"))
        .arg(count()).arg(editable() ? "ZIP transaction" : "Read-only");
}
void ArchiveController::setDeletePolicy(int value) {
    if (value >= 0 && value <= 2) { settings_.setValue("deleteOriginal", value); emit settingsChanged(); }
}
void ArchiveController::setCompressionThreads(int value) {
    if (value >= 1 && value <= 16) { settings_.setValue("compressionThreads", value); emit settingsChanged(); }
}
void ArchiveController::setTheme(const QString& value) {
    if (value == "Light" || value == "Dark" || value == "System") { settings_.setValue("theme", value); emit settingsChanged(); }
}
QVariantMap ArchiveController::shortcuts() const {
    QVariantMap map{{"open","Ctrl+O"},{"copy","Ctrl+C"},{"paste","Ctrl+V"},{"rename","F2"},{"remove","Delete"},{"selectAll","Ctrl+A"},{"extract","Ctrl+E"},{"create","Ctrl+N"}};
    for (auto it = map.begin(); it != map.end(); ++it)
        it.value() = settings_.value("shortcuts/" + it.key(), it.value());
    return map;
}
void ArchiveController::setShortcut(const QString& action, const QString& value) {
    const auto all = shortcuts();
    if (!all.contains(action)) return;
    const auto sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
    if (sequence.isEmpty()) { fail("Invalid keyboard shortcut"); return; }
    auto normalized = sequence.toString(QKeySequence::PortableText);
    for (auto it = all.begin(); it != all.end(); ++it)
        if (it.key() != action && it.value().toString() == normalized) { fail("Shortcut already assigned"); return; }
    settings_.setValue("shortcuts/" + action, normalized);
    emit settingsChanged();
}
void ArchiveController::setSort(int role, bool descending) {
    const int roles[] = {ArchiveModel::NameRole, ArchiveModel::SizeRole, ArchiveModel::KindRole, ArchiveModel::ModifiedRole};
    if (role >= 0 && role < 4) proxy_.setOrdering(roles[role], descending);
}
void ArchiveController::storeTab() {
    if (activeTab_ < 0 || activeTab_ >= static_cast<int>(tabs_.size())) return;
    auto& tab = tabs_[activeTab_];
    tab.entries = model_.takeEntries();
    tab.folder = currentFolder_;
}
void ArchiveController::selectTab(int index) {
    if (busy_ || index < 0 || index >= static_cast<int>(tabs_.size()) || index == activeTab_) return;
    storeTab();
    activeTab_ = index;
    auto& tab = tabs_[index];
    archivePath_ = tab.path;
    currentFolder_ = tab.folder;
    proxy_.setFolder(currentFolder_);
    model_.restoreEntries(std::move(tab.entries));
    clearPrepared();
    emit archiveChanged(); emit folderChanged(); emit tabsChanged();
}
void ArchiveController::closeTab(int index) {
    if (busy_ || index < 0 || index >= static_cast<int>(tabs_.size())) return;
    if (!tabs_[index].temporaryDirectory.isEmpty()) ExportStore::discard(tabs_[index].temporaryDirectory);
    if (index == activeTab_) {
        model_.restoreEntries({});
        activeTab_ = -1;
        archivePath_.clear(); currentFolder_.clear();
    } else if (index < activeTab_) --activeTab_;
    tabs_.erase(tabs_.begin()+index);
    if (activeTab_ == -1 && !tabs_.empty()) selectTab(std::min(index, static_cast<int>(tabs_.size())-1));
    if (activeTab_ == -1) { clearPrepared(); proxy_.setFolder({}); emit archiveChanged(); emit folderChanged(); }
    emit tabsChanged();
}
void ArchiveController::setFilter(const QString& filter) {
    proxy_.setSearch(filter);
    emit visibleCountChanged();
}
void ArchiveController::toggleVisible(int row) {
    const auto index = proxy_.index(row, 0);
    if (index.isValid()) model_.toggle(proxy_.mapToSource(index).row());
}
void ArchiveController::selectAll() {
    QList<int> visible;
    for (int i = 0; i < proxy_.rowCount(); ++i) visible.push_back(proxy_.mapToSource(proxy_.index(i, 0)).row());
    model_.setSelectedRows(visible);
}
void ArchiveController::enterVisibleFolder(int row) {
    const auto index = proxy_.index(row, 0);
    if (!index.isValid()) return;
    const auto* entry = model_.entryAt(proxy_.mapToSource(index).row());
    if (!entry || entry->type != arky::EntryType::Directory) return;
    currentFolder_ = QString::fromUtf8(entry->path.c_str());
    proxy_.setFolder(currentFolder_);
    model_.clearSelection();
    emit folderChanged();
}
void ArchiveController::upFolder() {
    if (currentFolder_.isEmpty()) return;
    const auto separator = currentFolder_.lastIndexOf('/');
    currentFolder_ = separator < 0 ? QString() : currentFolder_.left(separator);
    proxy_.setFolder(currentFolder_);
    model_.clearSelection();
    emit folderChanged();
}
void ArchiveController::rootFolder() {
    if (currentFolder_.isEmpty()) return;
    currentFolder_.clear();
    proxy_.setFolder(currentFolder_);
    model_.clearSelection();
    emit folderChanged();
}
void ArchiveController::clearSelection() { model_.clearSelection(); }
void ArchiveController::setStatus(const QString& text) { status_ = text; emit statusChanged(); }
void ArchiveController::clearPrepared() {
    preparedUrls_.clear();
    preparedEntries_.clear();
    exportDirectory_.clear();
    preparedPaths_.clear();
    emit preparedChanged();
}
void ArchiveController::fail(const QString& message) {
    setStatus(message);
    emit failed(message);
}
std::filesystem::path ArchiveController::localPath(const QUrl& url) {
    return nativePath(url.toLocalFile());
}
std::vector<std::filesystem::path> ArchiveController::parsePaths(const QVariantList& urls) {
    std::vector<std::filesystem::path> result;
    for (const auto& value : urls) {
        QUrl url = value.toUrl();
        if (!url.isValid() || !url.isLocalFile()) continue;
        if (QFileInfo::exists(url.toLocalFile())) result.push_back(localPath(url));
    }
    return result;
}
bool ArchiveController::archiveFilename(const QString& file) {
    const auto name = file.toLower();
    const QStringList ext = {".zip", ".zipx", ".7z", ".rar", ".rar5", ".tar", ".tar.gz", ".tgz", ".tar.xz",
        ".txz", ".tar.bz2", ".tbz2", ".tar.zst", ".zst", ".gz", ".xz", ".bz2", ".cpio",
        ".cab", ".xar", ".iso", ".lha", ".lzh", ".ar", ".warc", ".001"};
    return std::any_of(ext.begin(), ext.end(), [&name](const QString& e){ return name.endsWith(e); });
}
std::vector<std::string> ArchiveController::selection() const {
    std::vector<std::string> result;
    for (const auto& path : model_.selectedPaths()) result.push_back(utf8(path));
    return result;
}
void ArchiveController::start(const QString& name, const QString& passphrase, Work work, Success success) {
    if (shuttingDown_) return;
    if (busy_) {
        if (pending_.size() >= 32) { fail("Operation queue is full"); return; }
        pending_.push_back([this, name, passphrase, work = std::move(work), success = std::move(success)]() mutable {
            start(name, passphrase, std::move(work), std::move(success));
        });
        emit queueChanged();
        return;
    }
    busy_ = true;
    operation_ = name;
    progress_ = "Initializing…";
    success_ = std::move(success);
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    emit busyChanged(); emit operationChanged(); emit progressChanged();
    setStatus(name);
    auto cancelled = cancelled_;
    QPointer<ArchiveController> guard(this);
    auto outcome = std::make_shared<arky::Result>();
    thread_ = QThread::create([guard, cancelled, outcome, passphrase, work = std::move(work)]() mutable {
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        arky::Callbacks cb;
        cb.cancelled = [cancelled] { return cancelled->load(); };
        cb.passphrase = utf8(passphrase);
        std::uint64_t total = 0;
        const auto started = std::chrono::steady_clock::now();
        cb.totalBytes = [&total](std::uint64_t value) { total = value; };
        cb.progress = [guard, &last, &total, started](std::uint64_t amount, const std::string& item) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds(150)) return;
            last = now;
            if (!guard) return;
            QString message;
            if (total && amount <= total) {
                const auto elapsed = std::chrono::duration<double>(now-started).count();
                const auto remaining = amount ? static_cast<qint64>(elapsed * (total-amount) / amount) : 0;
                message = QString("%1 / %2 (%3%) — ETA %4 s").arg(prettyBytes(amount), prettyBytes(total))
                    .arg(100.0*amount/total, 0, 'f', 1).arg(remaining);
            } else message = QString("%1 processed — %2").arg(static_cast<qulonglong>(amount)).arg(QString::fromUtf8(item.c_str())).left(170);
            QMetaObject::invokeMethod(guard.data(), [guard, message] {
                if (!guard) return;
                guard->progress_ = message;
                emit guard->progressChanged();
            }, Qt::QueuedConnection);
        };
        arky::Result result;
        try { result = work(cb); }
        catch (const std::exception& e) { result.error = e.what(); }
        *outcome = std::move(result);
    });
    auto* current = thread_;
    connect(current, &QThread::finished, this, [this, current, outcome] {
        if (thread_ == current) thread_ = nullptr;
        done(std::move(*outcome));
    });
    connect(current, &QThread::finished, current, &QObject::deleteLater);
    current->start();
}
void ArchiveController::done(arky::Result result) {
    // Clear the finished operation before invoking callbacks, which are allowed
    // to queue up another operation (e.g. refresh after modifying an archive).
    auto onSuccess = std::move(success_);
    conflict_.reset();
    emit conflictFinished();
    success_ = {};
    busy_ = false;
    operation_.clear();
    progress_.clear();
    emit busyChanged(); emit operationChanged(); emit progressChanged();
    if (result.ok) {
        if (onSuccess) onSuccess(result);
        if (!result.warnings.empty()) {
            QString warning = QString::fromUtf8(result.warnings.front().c_str());
            if (result.warnings.size() > 1) warning += QString(" (+%1 more)").arg(result.warnings.size() - 1);
            emit completed("Operation finished with notes: " + warning);
        }
    } else {
        if (loadingArchive_) {
            loadingArchive_ = false;
            if (activeTab_ >= 0) closeTab(activeTab_);
        }
        if (stagingInProgress_) {
            ExportStore::discard(exportDirectory_);
            exportDirectory_.clear();
            stagingInProgress_ = false;
        }
        QString message = errorOf(result);
        for (const auto& warning : result.warnings) message += "\n" + QString::fromStdString(warning);
        fail(message);
    }
    if (!busy_) QTimer::singleShot(0, this, [this]{ runNext(); });
}
void ArchiveController::runNext() {
    if (busy_ || shuttingDown_ || pending_.empty()) return;
    auto action = std::move(pending_.front());
    pending_.pop_front(); emit queueChanged(); action();
    if (!busy_ && !pending_.empty()) QTimer::singleShot(0, this, [this]{ runNext(); });
}
void ArchiveController::cancel() { if (cancelled_) cancelled_->store(true); }
void ArchiveController::openUrl(const QUrl& url, const QString& passphrase) {
    if (!url.isLocalFile()) return;
    if (busy_) {
        if (pending_.size() >= 32) { fail("Operation queue is full"); return; }
        pending_.push_back([this, url, passphrase]{ openUrl(url, passphrase); }); emit queueChanged(); return;
    }
    const auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i)
        if (tabs_[i].path == file) { selectTab(i); return; }
    if (!QFileInfo(file).isFile()) { fail("Please choose an archive file"); return; }
    if (tabs_.size() >= 8) { fail("Close an archive tab before opening another (limit 8)"); return; }
    storeTab();
    tabs_.push_back({file, {}, std::exchange(nestedDirectory_, QString()), {}, false});
    activeTab_ = static_cast<int>(tabs_.size()) - 1;
    emit tabsChanged();
    loadArchive(QUrl::fromLocalFile(file), passphrase);
}
void ArchiveController::loadArchive(const QUrl& url, const QString& passphrase) {
    if (busy_ || !url.isLocalFile()) return;
    const auto file = url.toLocalFile();
    if (!QFileInfo(file).isFile()) { fail("Please choose an archive file"); return; }
    clearPrepared();
    model_.beginIncremental();
    archivePath_ = file;
    currentFolder_.clear();
    proxy_.setFolder(currentFolder_);
    loadingArchive_ = true;
    const QPointer<ArchiveController> guard(this);
    // Back-pressure: at most four batches queued for the GUI at any time.
    // This prevents a large archive from duplicating its entire index in
    // queued Qt events when the GUI thread cannot keep up.
    const auto batchPermits = std::make_shared<QSemaphore>(4);
    start("Reading " + QFileInfo(file).fileName(), passphrase,
        [path = localPath(url), guard, batchPermits](const arky::Callbacks& cb) {
            return arky::ArchiveEngine::inspectBatched(path,
                [guard, batchPermits, &cb](std::vector<arky::ArchiveEntryInfo> batch) {
                    while (!batchPermits->tryAcquire(1, 50)) {
                        if (cb.cancelled && cb.cancelled())
                            throw std::runtime_error("Operation cancelled");
                    }
                    if (!guard) { batchPermits->release(); return; }
                    const bool posted = QMetaObject::invokeMethod(guard.data(),
                        [guard, batchPermits, batch = std::move(batch)]() mutable {
                            if (guard && guard->loadingArchive_) {
                                guard->model_.appendBatch(std::move(batch));
                                emit guard->archiveChanged();
                            }
                            batchPermits->release();
                        }, Qt::QueuedConnection);
                    if (!posted) batchPermits->release();
                }, cb, 512);
        },
        [this, file](const arky::Result& result) {
            model_.finishIncremental();
            loadingArchive_ = false;
            archivePath_ = file;
            if (activeTab_ >= 0) { tabs_[activeTab_].editable = result.editable; tabs_[activeTab_].multiVolume = result.multiVolume; }
            emit archiveChanged();
            setStatus(QString("Opened %1 · %2 entries").arg(QFileInfo(file).fileName()).arg(model_.count()));
            if (result.filesProcessed != static_cast<std::uint64_t>(model_.count()))
                setStatus(QString("Opened %1 · %2 indexed rows (including folders)")
                          .arg(QFileInfo(file).fileName()).arg(model_.count()));
        });
    emit folderChanged();
    emit archiveChanged();
}
void ArchiveController::beginCreate(const QVariantList& urls) {
    pendingSources_ = parsePaths(urls);
    if (pendingSources_.empty()) { fail("Choose files or folders first"); return; }
    emit requestSaveArchive();
}
void ArchiveController::beginCreateFolder(const QUrl& folder) {
    beginCreate(QVariantList{QVariant::fromValue(folder)});
}
QVariantMap ArchiveController::outputCapabilities(const QString& extension) const {
    const auto suffix = extension.startsWith('.') ? extension : "." + extension;
    const auto caps = arky::ArchiveEngine::capabilities(utf8(suffix));
    return {{"writable", caps.writable},
            {"editable", caps.editable},
            {"multipleFiles", caps.multipleFiles},
            {"supportsCompressionLevel", caps.supportsCompressionLevel},
            {"supportsMultiVolume", caps.supportsMultiVolume}};
}
QStringList ArchiveController::compressionMethods(const QString& extension) const {
    const auto suffix = extension.startsWith('.') ? extension : "." + extension;
    const auto caps = arky::ArchiveEngine::capabilities(utf8(suffix));
    QStringList result;
    for (const auto& method : caps.methods) result << QString::fromStdString(method);
    return result;
}
void ArchiveController::saveNewArchive(const QUrl& dst, const QString& requestedExtension,
                                      int compressionLevel,
                                      const QString& compressionMethod) {
    if (!dst.isLocalFile() || pendingSources_.empty()) return;
    auto out = dst.toLocalFile();
    const auto lowered = out.toLower();
    bool knownExtension = false;
    for (const auto& ext : arky::ArchiveEngine::writableExtensions())
        if (lowered.endsWith(QString::fromStdString(ext))) knownExtension = true;
    if (!knownExtension) {
        QString suffix = requestedExtension;
        if (suffix.startsWith("*.")) suffix = suffix.mid(2);
        else if (suffix.startsWith('.')) suffix = suffix.mid(1);
        if (suffix.isEmpty()) suffix = "zip";
        out += "." + suffix;
    }
    // Do not let a dialog's selected format silently disagree with an explicit
    // filename extension, e.g. selecting ZIP but entering "backup.7z".
    QString expectedSuffix = requestedExtension;
    if (expectedSuffix.startsWith("*.")) expectedSuffix = expectedSuffix.mid(1);
    else if (!expectedSuffix.startsWith('.')) expectedSuffix.prepend('.');
    const auto actualCaps = arky::ArchiveEngine::capabilities(utf8(QFileInfo(out).fileName()));
    if (!expectedSuffix.isEmpty() && actualCaps.writable &&
        QString::fromStdString(actualCaps.extension).compare(expectedSuffix, Qt::CaseInsensitive) != 0) {
        fail("Filename extension and chosen archive format differ; please select a matching format");
        return;
    }
    if (QFileInfo::exists(out)) { fail("Archive already exists; overwriting is disabled"); return; }
    auto sources = std::move(pendingSources_);
    pendingSources_.clear();
    const auto output = nativePath(out);
    arky::CreateOptions options;
    options.compressionLevel = compressionLevel;
    options.compressionMethod = compressionMethod.isEmpty() ? "auto" : utf8(compressionMethod);
    options.threads = static_cast<unsigned>(compressionThreads());
    start("Creating " + QFileInfo(out).fileName(), {},
        [output, sources = std::move(sources), options](const arky::Callbacks& cb) {
            return arky::ArchiveEngine::create(output, sources, cb, options);
        },
        [this, out](const arky::Result& result) {
            setStatus(QString("Created %1 · %2 entries").arg(QFileInfo(out).fileName()).arg(static_cast<qulonglong>(result.filesProcessed)));
            emit completed("Archive created successfully");
            openUrl(QUrl::fromLocalFile(out));
        });
}
void ArchiveController::droppedUrls(const QVariantList& urls) {
    if (busy_) return;
    const auto paths = parsePaths(urls);
    if (paths.empty()) { fail("Drop local files or folders; virtual attachments require exporting first"); return; }
    if (editable()) { appendFiles(urls); return; }
    if (paths.size() == 1) {
        const auto path = urls.first().toUrl().toLocalFile();
        if (QFileInfo(path).isFile() && archiveFilename(path)) {
            openUrl(QUrl::fromLocalFile(path)); return;
        }
    }
    if (!archivePath_.isEmpty()) { fail("This archive is read-only. Open a ZIP or create a new archive to add files."); return; }
    pendingSources_ = paths;
    emit requestSaveArchive();
}
void ArchiveController::extractTo(const QUrl& folder, bool selectedOnly, const QString& passphrase, int conflicts) {
    if (loadingArchive_ || archivePath_.isEmpty() || !folder.isLocalFile()) return;
    const auto targets = selectedOnly ? selection() : std::vector<std::string>{};
    if (selectedOnly && targets.empty()) { fail("Select one or more entries first"); return; }
    const auto dest = folder.toLocalFile();
    arky::ExtractOptions opts;
    opts.existingFiles = conflicts == 1 ? arky::ExistingFilePolicy::Skip
                      : conflicts == 2 ? arky::ExistingFilePolicy::Rename
                      : conflicts == 3 ? arky::ExistingFilePolicy::Overwrite
                      : arky::ExistingFilePolicy::Fail;
    const auto original = archivePath_;
    const bool multiVolume = activeTab_ >= 0 && tabs_[activeTab_].multiVolume;
    const QFileInfo sourceInfo(original);
    QPointer<ArchiveController> guard(this);
    start("Extracting " + archiveName(), passphrase,
        [path = nativePath(archivePath_), target = localPath(folder), targets, opts, conflicts, guard](const arky::Callbacks& cb) mutable {
            if (conflicts == 0) opts.conflictResolver = [guard, &cb](const std::string& path) {
                auto answer = std::make_shared<ConflictAnswer>();
                if (!guard) return arky::ExistingFilePolicy::Fail;
                QMetaObject::invokeMethod(guard.data(), [guard, answer, path] {
                    if (!guard) return;
                    guard->conflict_ = answer;
                    emit guard->requestConflict(QString::fromStdString(path));
                }, Qt::QueuedConnection);
                while (!answer->semaphore.tryAcquire(1, 50)) {
                    if (!guard || (cb.cancelled && cb.cancelled())) return arky::ExistingFilePolicy::Fail;
                }
                switch (answer->choice.load()) {
                    case 1: return arky::ExistingFilePolicy::Skip;
                    case 2: return arky::ExistingFilePolicy::Rename;
                    case 3: return arky::ExistingFilePolicy::Overwrite;
                    default: return arky::ExistingFilePolicy::Fail;
                }
            };
            return arky::ArchiveEngine::extract(path, target, targets, cb, opts);
        },
        [this, dest, original, selectedOnly, multiVolume, originalSize = sourceInfo.size(), originalTime = sourceInfo.lastModified()](const arky::Result& result) {
            setStatus(QString("Extracted %1 entries (%2) to %3").arg(static_cast<qulonglong>(result.filesProcessed))
                .arg(prettyBytes(result.bytesProcessed), dest));
            emit completed("Extraction complete");
            // Only offer trash for complete, warning-free extractions.
            if (!selectedOnly && !multiVolume && result.warnings.empty() && deletePolicy() != 2 && pending_.empty() &&
                !original.endsWith(".001") && !original.contains(QRegularExpression("\\.part[0-9]+\\.rar$", QRegularExpression::CaseInsensitiveOption)) &&
                QFileInfo(original).isFile() && !original.startsWith(ExportStore::root()+"/")) {
                awaitingTrash_ = original;
                trashSize_ = originalSize; trashModified_ = originalTime;
                if (deletePolicy() == 1) confirmTrash(true, false);
                else emit requestTrash(original);
            }
        });
}
void ArchiveController::appendFiles(const QVariantList& urls, const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    auto files = parsePaths(urls);
    if (files.empty()) { fail("Choose one or more local files"); return; }
    arky::EditPlan plan;
    for (const auto& file : files) {
#ifdef _WIN32
        auto filename = QString::fromStdWString(file.filename().wstring());
#else
        auto filename = QString::fromUtf8(file.filename().string().c_str());
#endif
        const auto archiveTarget = currentFolder_.isEmpty() ? filename : currentFolder_ + "/" + filename;
        plan.additions.push_back({file, utf8(archiveTarget), false});
    }
    runEdit("Adding files · " + archiveName(), std::move(plan), passphrase);
}
void ArchiveController::removeSelected(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    auto paths = selection();
    if (paths.empty()) { fail("Select one or more entries first"); return; }
    arky::EditPlan plan;
    plan.removals = std::move(paths);
    runEdit("Removing entries · " + archiveName(), std::move(plan), passphrase);
}
void ArchiveController::runEdit(const QString& title, arky::EditPlan plan, const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    if (!editable()) { fail("Only unencrypted ZIP archives support modification"); return; }
    start(title, passphrase,
          [source = nativePath(archivePath_), plan = std::move(plan)](const arky::Callbacks& cb) {
              return arky::ArchiveEngine::edit(source, plan, cb);
          },
          [this](const arky::Result&) { refresh(); });
}
void ArchiveController::addFolder(const QUrl& source, const QString& passphrase) {
    if (!source.isLocalFile() || !QFileInfo(source.toLocalFile()).isDir()) {
        fail("Select a folder from your computer"); return;
    }
    appendFiles({QVariant::fromValue(source)}, passphrase);
}
void ArchiveController::newFolder(const QString& name, const QString& passphrase) {
    if (archivePath_.isEmpty() || busy_) return;
    const auto target = currentFolder_.isEmpty() ? name.trimmed() : currentFolder_ + "/" + name.trimmed();
    arky::EditPlan plan;
    plan.createDirectories.push_back(utf8(target));
    runEdit("Creating folder " + name, std::move(plan), passphrase);
}
void ArchiveController::renameSelected(const QString& name, const QString& passphrase) {
    const auto paths = model_.selectedPaths();
    if (paths.size() != 1) { fail("Select exactly one entry to rename"); return; }
    const auto original = paths.front();
    const auto parentPos = original.lastIndexOf('/');
    const auto newPath = (parentPos < 0 ? QString() : original.left(parentPos+1)) + name.trimmed();
    arky::EditPlan plan;
    plan.renames.push_back({utf8(original), utf8(newPath)});
    runEdit("Renaming " + original, std::move(plan), passphrase);
}
void ArchiveController::moveSelected(const QString& destinationFolder, const QString& passphrase) {
    const auto paths = model_.selectedPaths();
    if (paths.empty()) { fail("Select entries to move"); return; }
    auto folder = destinationFolder.trimmed();
    while (folder.endsWith('/')) folder.chop(1);
    arky::EditPlan plan;
    for (const auto& path : paths) {
        const auto name = path.section('/', -1);
        const auto target = folder.isEmpty() ? name : folder + "/" + name;
        plan.renames.push_back({utf8(path), utf8(target)});
    }
    runEdit("Moving " + QString::number(paths.size()) + " entries", std::move(plan), passphrase);
}
void ArchiveController::replaceSelected(const QUrl& source, const QString& passphrase) {
    const auto paths = model_.selectedPaths();
    if (paths.size() != 1 || !source.isLocalFile() || !QFileInfo(source.toLocalFile()).isFile()) {
        fail("Select exactly one archive file and one local replacement file"); return;
    }
    auto entries = model_.selectedEntries();
    if (entries.empty() || entries.front().type != arky::EntryType::Regular) {
        fail("Only regular files can be replaced"); return;
    }
    arky::EditPlan plan;
    plan.additions.push_back({localPath(source), utf8(paths.front()), true});
    runEdit("Replacing " + paths.front(), std::move(plan), passphrase);
}
void ArchiveController::testArchive(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    start("Checking archive integrity · " + archiveName(), passphrase,
        [source = nativePath(archivePath_)](const arky::Callbacks& cb) {
            return arky::ArchiveEngine::test(source, cb);
        },
        [this](const arky::Result& result) {
            const auto message = QString("All %1 entries decoded without detected errors (%2)")
                 .arg(static_cast<qulonglong>(result.filesProcessed))
                 .arg(prettyBytes(result.bytesProcessed));
            setStatus(message);
            emit completed(message);
        });
}

void ArchiveController::refresh() {
    // After the modifying operation finishes, start a separate non-blocking scan.
    const auto file = archivePath_;
    QMetaObject::invokeMethod(this, [this, file] { loadArchive(QUrl::fromLocalFile(file), {}); }, Qt::QueuedConnection);
    setStatus("Archive updated; refreshing…");
}
void ArchiveController::previewSelected(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    auto paths = model_.selectedPaths();
    if (paths.size() != 1) { fail("Select exactly one file to preview"); return; }
    const auto name = paths.first();
    auto text = std::make_shared<std::string>();
    start("Previewing " + name, passphrase,
        [path = nativePath(archivePath_), entry = utf8(name), text](const arky::Callbacks& cb){
            return arky::ArchiveEngine::preview(path, entry, *text, 128 * 1024, cb);
        },
        [this, text, name](const arky::Result& result) {
            const auto bytes = QByteArray(text->data(), static_cast<qsizetype>(text->size()));
            const bool binary = bytes.contains('\0');
            emit previewReady(name, binary ? "Binary file. Extract it to view with an external application."
                                          : QString::fromUtf8(bytes), result.bytesProcessed == 128 * 1024);
            setStatus("Preview ready · " + name);
        });
}
void ArchiveController::stageSelection(std::vector<std::string> paths,
                                       std::vector<arky::ArchiveEntryInfo> entries,
                                       const QString& passphrase, bool copyWhenReady, bool dragWhenReady) {
    if (busy_ || archivePath_.isEmpty() || paths.empty()) return;
    std::vector<arky::ExportItem> planned;
    try { planned = arky::planExports(std::move(entries)); }
    catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); return; }
    if (planned.empty()) return;
    paths.clear();
    for (const auto& item : planned) paths.push_back(item.entry.path);

    // Clipboard contents must outlive this controller and even the process.
    // Every export is kept in an independent, timestamped directory. Previously
    // copied file URLs remain valid when the user exports another selection.
    clearPrepared();
    exportDirectory_ = ExportStore::create();
    if (exportDirectory_.isEmpty()) { fail("Cannot create export directory"); return; }
    const auto stagingPath = exportDirectory_;
    const auto unpackedPath = QDir(stagingPath).filePath("source");
    const auto readyPath = QDir(stagingPath).filePath("ready");
    if (!QDir().mkpath(unpackedPath) || !QDir().mkpath(readyPath)) {
        ExportStore::discard(stagingPath);
        clearPrepared();
        fail("Cannot initialize export staging directories");
        return;
    }
    stagingInProgress_ = true;
    start("Preparing extracted files for desktop transfer", passphrase,
        [source = nativePath(archivePath_), destination = nativePath(unpackedPath), paths = std::move(paths)](const arky::Callbacks& cb) {
            return arky::ArchiveEngine::extract(source, destination, paths, cb);
        },
        [this, planned = std::move(planned), unpackedPath, readyPath, copyWhenReady, dragWhenReady](const arky::Result&) {
            stagingInProgress_ = false;
            for (const auto& item : planned) {
                const auto original = QDir(unpackedPath).filePath(QString::fromUtf8(item.entry.path.c_str()));
                if (!QFileInfo::exists(original)) continue; // Link/special entries are not extracted.
                const auto renamed = QDir(readyPath).filePath(QString::fromUtf8(item.exportedName.c_str()));
                if (!QDir().rename(original, renamed)) {
                    preparedEntries_.clear();
                    preparedUrls_.clear();
                    ExportStore::discard(exportDirectory_);
                    exportDirectory_.clear();
                    fail("Failed to prepare unique exported filenames");
                    return;
                }
                preparedEntries_.push_back(item.entry);
                preparedUrls_.push_back(QUrl::fromLocalFile(renamed));
                preparedPaths_.push_back(QString::fromUtf8(item.entry.path.c_str()));
            }
            if (preparedUrls_.isEmpty()) {
                ExportStore::discard(exportDirectory_);
                clearPrepared();
                fail("No regular files or directories were available to export");
                return;
            }
            emit preparedChanged();
            setStatus(preparedText());
            if (copyWhenReady) copyPreparedFiles();
            if (dragWhenReady) setStatus("Files ready; start a new drag gesture to transfer them");
        });
}
void ArchiveController::prepareDrag(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    if (selection().empty()) { fail("Select archive entries first"); return; }
    stageSelection(selection(), model_.selectedEntries(), passphrase, false, false);
}
void ArchiveController::dragPrepared() {
    if (busy_ || preparedUrls_.empty()) { fail("Use Prepare drag-out first"); return; }
    auto* source = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
    if (!source) { fail("No active desktop window for drag-and-drop"); return; }
    QDrag drag(source);
    drag.setMimeData(MimeBridge::make(preparedEntries_, archivePath_, preparedUrls_));
    drag.exec(Qt::CopyAction);
}
void ArchiveController::dragEntry(int visibleRow, const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    const auto proxyIndex = proxy_.index(visibleRow, 0);
    if (!proxyIndex.isValid()) return;
    const auto* entry = model_.entryAt(proxy_.mapToSource(proxyIndex).row());
    if (!entry) return;
    const auto currentPath = QString::fromUtf8(entry->path.c_str());
    auto entries = model_.selectedPaths().contains(currentPath)
                 ? model_.selectedEntries() : std::vector<arky::ArchiveEntryInfo>{*entry};
#ifdef _WIN32
    // Include directory descendants in FileGroupDescriptorW, without
    // decompressing them or creating temporary files. Unsafe names and
    // unsupported entry kinds fall through to the disk-backed path.
    try {
        auto plan = arky::planWindowsTransfer(entries, model_.allEntries());
        const bool namesFit = std::all_of(plan.begin(), plan.end(), [](const auto& item) {
            return QString::fromUtf8(item.shellPath.c_str()).size() < MAX_PATH;
        });
        // For very large trees, avoid reopening a sequential archive once per
        // member. Staging is often substantially faster in that case.
        if (namesFit && !plan.empty() && plan.size() <= 128) {
            auto* source = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
            if (!source) { fail("No active desktop window for drag-and-drop"); return; }
            QDrag drag(source);
            drag.setMimeData(new WindowsVirtualMimeData(std::move(plan), archivePath_, passphrase));
            drag.exec(Qt::CopyAction);
            return;
        }
    } catch (const std::invalid_argument&) {
        // Use the original disk-backed mechanism for incompatible entries.
    }
#endif
    if (entries.size() == preparedEntries_.size() && !preparedUrls_.isEmpty()) {
        std::vector<std::string> wanted, cached;
        for (const auto& e : entries) wanted.push_back(e.path);
        for (const auto& e : preparedEntries_) cached.push_back(e.path);
        std::sort(wanted.begin(), wanted.end());
        std::sort(cached.begin(), cached.end());
        if (wanted == cached) { dragPrepared(); return; }
    }
    std::vector<std::string> paths;
    for (const auto& e : entries) paths.push_back(e.path);
    stageSelection(std::move(paths), std::move(entries), passphrase, false, false);
    setStatus("Preparing native file URLs. Drag again when the export is ready.");
}
void ArchiveController::copyVirtualSelection(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty() || model_.selectedCount() == 0) return;
#ifdef _WIN32
    try {
        auto plan = arky::planWindowsTransfer(model_.selectedEntries(), model_.allEntries());
        const bool namesFit = std::all_of(plan.begin(), plan.end(), [](const auto& item) {
            return QString::fromUtf8(item.shellPath.c_str()).size() < MAX_PATH;
        });
        if (plan.empty() || !namesFit || plan.size() > 128) {
            fail("Selection is too large or has unsupported Windows filenames; use Copy files instead");
            return;
        }
        // Clipboard ownership is live: Explorer will request IStreams from us.
        // Use Copy files for persistent disk-backed URLs if Arky may close.
        QGuiApplication::clipboard()->setMimeData(
            new WindowsVirtualMimeData(std::move(plan), archivePath_, passphrase));
        setStatus("Copied virtual files. Keep Arky open until Explorer finishes pasting.");
    } catch (const std::exception& e) {
        fail(QString("Cannot copy virtual files: %1. Use Copy files instead.").arg(e.what()));
    }
#else
    copySelection(passphrase);
#endif
}
void ArchiveController::copyEntryInfo() {
    if (model_.selectedCount() == 0) return;
    QGuiApplication::clipboard()->setMimeData(MimeBridge::make(model_.selectedEntries(), archivePath_));
    setStatus("Copied entry metadata · JSON, text, and Arky MIME");
}
void ArchiveController::copySelection(const QString& passphrase) {
    if (busy_ || model_.selectedCount() == 0) return;
    stageSelection(selection(), model_.selectedEntries(), passphrase, true, false);
}
void ArchiveController::copyPreparedFiles() {
    if (preparedUrls_.empty()) { fail("Prepare file export first"); return; }
    QGuiApplication::clipboard()->setMimeData(MimeBridge::make(preparedEntries_, archivePath_, preparedUrls_));
    setStatus("Copied file URLs and entry metadata to the clipboard");
}
void ArchiveController::pasteFiles() {
    const auto paths = MimeBridge::localUrls(QGuiApplication::clipboard()->mimeData());
    if (paths.isEmpty()) { fail("Clipboard has no local file URLs. Virtual entry metadata alone cannot recreate files."); return; }
    QVariantList values;
    for (const auto& url : paths) values << QVariant::fromValue(url);
    droppedUrls(values);
}
void ArchiveController::showExportFolder() {
    if (!exportDirectory_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(exportDirectory_));
}

void ArchiveController::extractHere(const QString& passphrase, int conflicts) {
    if (archivePath_.isEmpty()) return;
    extractTo(QUrl::fromLocalFile(QFileInfo(archivePath_).absolutePath()), false, passphrase, conflicts);
}
void ArchiveController::confirmTrash(bool yes, bool remember) {
    const auto file = std::exchange(awaitingTrash_, QString());
    if (remember) setDeletePolicy(yes ? 1 : 2);
    if (!yes || file.isEmpty()) return;
    // Never delete permanently and never remove the source of a queued operation.
    if (busy_ || !pending_.empty()) { fail("Archive retained because operations are pending"); return; }
    const QFileInfo current(file);
    if (current.size() != trashSize_ || current.lastModified() != trashModified_) {
        fail("Archive changed since extraction; original retained"); return;
    }
    if (!QFile::moveToTrash(file)) { fail("Could not move original archive to Trash; original retained"); return; }
    for (int i=0; i<static_cast<int>(tabs_.size()); ++i)
        if (tabs_[i].path == file) { closeTab(i); break; }
    setStatus("Original moved to Trash");
}
void ArchiveController::resolveConflict(int choice) {
    if (!conflict_) return;
    conflict_->choice.store(choice);
    conflict_->semaphore.release();
    conflict_.reset();
}
void ArchiveController::openSelected(const QString& passphrase) {
    if (busy_ || archivePath_.isEmpty()) return;
    const auto entries = model_.selectedEntries();
    if (entries.size() != 1 || entries.front().type != arky::EntryType::Regular) { fail("Select one regular file"); return; }
    const auto entry = entries.front().path;
    const auto directory = ExportStore::create();
    if (directory.isEmpty()) { fail("Cannot create temporary export"); return; }
    const auto file = QDir(directory).filePath(QString::fromStdString(entry));
    const bool nested = archiveFilename(file);
    if (nested && tabs_.size() >= 8) { ExportStore::discard(directory); fail("Tab limit reached"); return; }
    start("Opening archived file", passphrase,
        [source = nativePath(archivePath_), dest = nativePath(directory), entry, directory](const arky::Callbacks& cb) {
            auto result = arky::ArchiveEngine::extract(source, dest, {entry}, cb);
            if (!result.ok) ExportStore::discard(directory);
            return result;
        }, [this, file, directory, nested](const arky::Result&) {
            if (nested) {
                nestedDirectory_ = directory;
                openUrl(QUrl::fromLocalFile(file));
            } else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file))) {
                ExportStore::discard(directory); fail("No default application could open this file");
            } else setStatus("Opened temporary file; kept for the external application");
        });
}
