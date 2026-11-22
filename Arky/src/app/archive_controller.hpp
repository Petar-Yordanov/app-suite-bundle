#pragma once

#include "archive_engine.hpp"
#include "archive_model.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QThread>
#include <QUrl>
#include <QVariantMap>
#include <QStringList>
#include <QVariant>
#include <QSettings>
#include <QSemaphore>
#include <QDateTime>

#include <atomic>
#include <functional>
#include <memory>
#include <deque>

class QQuickWindow;

// Filesystem-style folder scope, without filtering away synthetic directory rows.
class ArchiveFolderProxy final : public QSortFilterProxyModel {
public:
    explicit ArchiveFolderProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setFolder(const QString& folder);
    void setSearch(const QString& search);
    void setTree(bool tree) { tree_ = tree; invalidate(); }
    void setOrdering(int role, bool descending) { setSortRole(role); sort(0, descending ? Qt::DescendingOrder : Qt::AscendingOrder); }
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
    QString folder_;
    QString search_;
    bool tree_ = false;
};

class ArchiveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* rows READ rows CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(QString progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString archiveName READ archiveName NOTIFY archiveChanged)
    Q_PROPERTY(QString archivePath READ archivePath NOTIFY archiveChanged)
    Q_PROPERTY(int count READ count NOTIFY archiveChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool prepared READ prepared NOTIFY preparedChanged)
    Q_PROPERTY(QString preparedText READ preparedText NOTIFY preparedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY folderChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(bool editable READ editable NOTIFY archiveChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY archiveChanged)
    Q_PROPERTY(QStringList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged)
    Q_PROPERTY(QString properties READ properties NOTIFY archiveChanged)
    Q_PROPERTY(int deletePolicy READ deletePolicy WRITE setDeletePolicy NOTIFY settingsChanged)
    Q_PROPERTY(int compressionThreads READ compressionThreads WRITE setCompressionThreads NOTIFY settingsChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(QString updateUrl READ updateUrl WRITE setUpdateUrl NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY settingsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY queueChanged)
public:
    explicit ArchiveController(QObject* parent = nullptr);
    ~ArchiveController() override;
    QAbstractItemModel* rows() { return &proxy_; }
    bool busy() const { return busy_; }
    QString operation() const { return operation_; }
    QString progress() const { return progress_; }
    QString archiveName() const;
    QString archivePath() const { return archivePath_; }
    int count() const { return model_.count(); }
    int visibleCount() const { return proxy_.rowCount(); }
    int selectedCount() const { return model_.selectedCount(); }
    bool prepared() const { return !preparedUrls_.isEmpty(); }
    QString preparedText() const { return QString("%1 file(s) ready to drag into Explorer/Finder") .arg(preparedUrls_.size()); }
    QString status() const { return status_; }
    QString currentFolder() const { return currentFolder_; }
    bool editable() const;
    bool loading() const { return loadingArchive_; }
    QStringList tabs() const;
    int activeTab() const { return activeTab_; }
    QString properties() const;
    int deletePolicy() const { return settings_.value("deleteOriginal", 0).toInt(); }
    int compressionThreads() const { return settings_.value("compressionThreads", 1).toInt(); }
    QString theme() const { return settings_.value("theme", "System").toString(); }
    QString updateUrl() const { return settings_.value("updateUrl").toString(); }
    void setUpdateUrl(const QString& value) { if (value != updateUrl()) { settings_.setValue("updateUrl", value); emit settingsChanged(); } }
    QVariantMap shortcuts() const;
    int queuedCount() const { return static_cast<int>(pending_.size()); }
    void setDeletePolicy(int);
    void setCompressionThreads(int);
    void setTheme(const QString&);
    Q_INVOKABLE void setShortcut(const QString& action, const QString& value);
    Q_INVOKABLE void selectTab(int);
    Q_INVOKABLE void closeTab(int);
    Q_INVOKABLE void setTree(bool value) { proxy_.setTree(value); }
    Q_INVOKABLE void setSort(int role, bool descending);
    Q_INVOKABLE void openSelected(const QString& passphrase = {});
    Q_INVOKABLE void confirmTrash(bool yes, bool remember);
    Q_INVOKABLE void resolveConflict(int choice);
    Q_INVOKABLE void extractHere(const QString& passphrase = {}, int conflicts = 0);
    QString selectedName() const {
        auto paths = model_.selectedPaths();
        return paths.size() == 1 ? paths.front().section('/', -1) : QString();
    }

    Q_INVOKABLE void setFilter(const QString& filter);
    Q_INVOKABLE void toggleVisible(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void enterVisibleFolder(int row);
    Q_INVOKABLE void upFolder();
    Q_INVOKABLE void rootFolder();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void openUrl(const QUrl& archive, const QString& passphrase = {});
    Q_INVOKABLE void beginCreate(const QVariantList& sourceUrls);
    Q_INVOKABLE void beginCreateFolder(const QUrl& folder);
    Q_INVOKABLE QVariantMap outputCapabilities(const QString& extension) const;
    Q_INVOKABLE QStringList compressionMethods(const QString& extension) const;
    Q_INVOKABLE void saveNewArchive(const QUrl& destination, const QString& requestedExtension = {},
                                    int compressionLevel = -1,
                                    const QString& compressionMethod = {});
    Q_INVOKABLE void droppedUrls(const QVariantList& urls);
    Q_INVOKABLE void extractTo(const QUrl& folder, bool selectedOnly, const QString& passphrase = {}, int conflicts = 0);
    Q_INVOKABLE void appendFiles(const QVariantList& urls, const QString& passphrase = {});
    Q_INVOKABLE void removeSelected(const QString& passphrase = {});
    Q_INVOKABLE void addFolder(const QUrl& source, const QString& passphrase = {});
    Q_INVOKABLE void newFolder(const QString& name, const QString& passphrase = {});
    Q_INVOKABLE void renameSelected(const QString& name, const QString& passphrase = {});
    Q_INVOKABLE void moveSelected(const QString& destinationFolder, const QString& passphrase = {});
    Q_INVOKABLE void replaceSelected(const QUrl& source, const QString& passphrase = {});
    Q_INVOKABLE void testArchive(const QString& passphrase = {});
    Q_INVOKABLE void previewSelected(const QString& passphrase = {});
    Q_INVOKABLE void prepareDrag(const QString& passphrase = {});
    Q_INVOKABLE void dragPrepared();
    Q_INVOKABLE void dragEntry(int visibleRow, const QString& passphrase = {});
    Q_INVOKABLE void copySelection(const QString& passphrase = {});
    Q_INVOKABLE void copyVirtualSelection(const QString& passphrase = {});
    Q_INVOKABLE void copyEntryInfo();
    Q_INVOKABLE void copyPreparedFiles();
    Q_INVOKABLE void pasteFiles();
    Q_INVOKABLE void showExportFolder();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void operationChanged();
    void progressChanged();
    void archiveChanged();
    void visibleCountChanged();
    void selectionChanged();
    void preparedChanged();
    void statusChanged();
    void folderChanged();
    void failed(const QString& reason);
    void completed(const QString& message);
    void requestSaveArchive();
    void previewReady(const QString& name, const QString& text, bool truncated);
    void tabsChanged();
    void settingsChanged();
    void queueChanged();
    void requestTrash(const QString& archive);
    void requestConflict(const QString& path);
    void conflictFinished();
private:
    using Work = std::function<arky::Result(const arky::Callbacks&)>;
    using Success = std::function<void(const arky::Result&)>;
    void start(const QString& name, const QString& passphrase, Work work, Success success);
    void done(arky::Result result);
    void fail(const QString& message);
    void setStatus(const QString& status);
    void refresh();
    void clearPrepared();
    void stageSelection(std::vector<std::string> paths,
                        std::vector<arky::ArchiveEntryInfo> entries,
                        const QString& passphrase, bool copyWhenReady, bool dragWhenReady);
    void runEdit(const QString& title, arky::EditPlan plan, const QString& passphrase);
    void loadArchive(const QUrl& url, const QString& passphrase);
    void storeTab();
    void runNext();
    static std::filesystem::path localPath(const QUrl& url);
    static std::vector<std::filesystem::path> parsePaths(const QVariantList& urls);
    static bool archiveFilename(const QString& file);
    std::vector<std::string> selection() const;

    ArchiveModel model_;
    ArchiveFolderProxy proxy_;
    QString currentFolder_;
    QString archivePath_;
    QString operation_;
    QString progress_;
    QString status_ = "Ready — open or drop an archive to begin";
    bool busy_ = false;
    bool loadingArchive_ = false;
    QThread* thread_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancelled_;
    Success success_;
    std::vector<std::filesystem::path> pendingSources_;
    QString exportDirectory_;
    QList<QUrl> preparedUrls_;
    QStringList preparedPaths_;
    bool stagingInProgress_ = false;
    std::vector<arky::ArchiveEntryInfo> preparedEntries_;
    struct Tab {
        QString path, folder, temporaryDirectory;
        std::vector<arky::ArchiveEntryInfo> entries;
        bool editable = false;
        bool multiVolume = false;
    };
    std::vector<Tab> tabs_;
    int activeTab_ = -1;
    QString nestedDirectory_;
    QString awaitingTrash_;
    qint64 trashSize_ = -1;
    QDateTime trashModified_;
    struct ConflictAnswer { QSemaphore semaphore; std::atomic_int choice{0}; };
    std::shared_ptr<ConflictAnswer> conflict_;
    QSettings settings_;
    std::deque<std::function<void()>> pending_;
    bool shuttingDown_ = false;
};
