#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "Core/FileOperationsFactory.h"
#include "Core/LocalFileOperations.h"
#include "Core/ElevatedOperations.h"
#include "Core/DockerFileOperations.h"
#include "sidebar/DockerContainerModel.h"
#include "tabs/TabsViewModel.h"

#define CHECK(condition) do { if (!(condition)) qFatal("Test failure: %s at line %d", #condition, __LINE__); } while (false)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    LocalFileOperations files;
    QString error;
    const QString base = temp.path();
    const QString folder = FileOperationsFactory::child(base, QStringLiteral("folder"));
    const QString file = FileOperationsFactory::child(folder, QStringLiteral("hello.txt"));
    CHECK(files.createDirectory(folder, &error));
    CHECK(files.createFile(file, &error));
    CHECK(files.writeText(file, QStringLiteral("Hello, Файлове!"), &error));
    QString contents;
    CHECK(files.readText(file, &contents, &error));
    CHECK(contents == QStringLiteral("Hello, Файлове!"));
    QVector<FileEntry> entries;
    CHECK(files.list(folder, &entries, &error));
    CHECK(entries.size() == 1);
    CHECK(entries.first().name == QStringLiteral("hello.txt"));

    const QString renamed = FileOperationsFactory::child(folder, QStringLiteral("renamed.txt"));
    CHECK(files.rename(file, renamed, &error));
    bool directory = false;
    CHECK(files.exists(renamed, &directory, &error));
    CHECK(!directory);
    const QString duplicate = FileOperationsFactory::child(folder, QStringLiteral("copy.txt"));
    CHECK(FileOperationsFactory::transfer(renamed, duplicate, false, &error));
    CHECK(files.exists(renamed, nullptr, &error));
    CHECK(files.exists(duplicate, nullptr, &error));
    CHECK(!FileOperationsFactory::transfer(folder, FileOperationsFactory::child(folder, QStringLiteral("child")), false, &error));
    CHECK(!files.copy(renamed,duplicate,&error));
    CHECK(files.readText(duplicate,&contents,&error)&&contents==QStringLiteral("Hello, Файлове!"));
    // Exercise the one-shot dispatcher without raising privileges on CI.
    const auto canonicalFolder=QFileInfo(folder).canonicalFilePath();
    const auto elevatedFile=QDir(canonicalFolder).filePath("administrator-test.txt");
    CHECK(!ElevatedOperations::validate({"copy","relative",elevatedFile},&error));
    CHECK(!ElevatedOperations::validate({"remove",QDir::rootPath()},&error));
    CHECK(!ElevatedOperations::validate({"arbitrary-command",elevatedFile},&error));
    CHECK(ElevatedOperations::execute({"create-file",elevatedFile},&error));
    CHECK(!ElevatedOperations::execute({"create-file",elevatedFile},&error));
    CHECK(!ElevatedOperations::execute({"remove",canonicalFolder},&error));
    CHECK(ElevatedOperations::execute({"remove",elevatedFile},&error));
    auto elevatedCopy=QDir(QFileInfo(base).canonicalFilePath()).filePath("admin-tree-test");
    CHECK(ElevatedOperations::execute({"copy",canonicalFolder,elevatedCopy},&error));
    CHECK(QFileInfo::exists(QDir(elevatedCopy).filePath("renamed.txt")));
    CHECK(!ElevatedOperations::execute({"copy",canonicalFolder,elevatedCopy},&error));
    CHECK(ElevatedOperations::execute({"remove-tree",elevatedCopy},&error));
    CHECK(!QFileInfo::exists(elevatedCopy)&&QFileInfo::exists(canonicalFolder));
#ifndef Q_OS_WIN
    const auto alias=QDir(base).filePath("alias");
    CHECK(QFile::link(folder,alias));
    CHECK(!files.copy(folder,QDir(alias).filePath("recursive"),&error));
    CHECK(!QFileInfo::exists(QDir(folder).filePath("recursive")));
    const auto linkCopy=QDir(base).filePath("alias-copy");
    CHECK(files.copy(alias,linkCopy,&error)&&QFileInfo(linkCopy).isSymLink());
    CHECK(files.remove(linkCopy,&error)&&QFileInfo::exists(folder));
    CHECK(files.remove(alias,&error));
#endif
    CHECK(files.remove(renamed, &error));
    CHECK(!files.exists(renamed, nullptr, &error));
    CHECK(files.remove(folder, &error));

    const QString dockerRoot = DockerFileOperations::uri(QStringLiteral("testcontainer"), QStringLiteral("/"));
    CHECK(FileOperationsFactory::isContainerPath(dockerRoot));
    CHECK(DockerFileOperations::innerPath(dockerRoot) == QStringLiteral("/"));
    CHECK(FileOperationsFactory::samePath(FileOperationsFactory::parent(dockerRoot), dockerRoot));
    const QString dockerChild = FileOperationsFactory::child(dockerRoot, QStringLiteral("hello world.txt"));
    CHECK(DockerFileOperations::innerPath(dockerChild) == QStringLiteral("/hello world.txt"));
    CHECK(FileOperationsFactory::samePath(FileOperationsFactory::parent(dockerChild), dockerRoot));
    CHECK(FileOperationsFactory::isAncestor(dockerRoot, dockerChild));
    CHECK(FileOperationsFactory::child(dockerRoot, QStringLiteral("../escape")).isEmpty());
    CHECK(FileOperationsFactory::normalize(QStringLiteral("http://example.com/x")).isEmpty());
    CHECK(FileOperationsFactory::normalize(QStringLiteral("container:")).isEmpty());
    CHECK(FileOperationsFactory::normalize(QStringLiteral("container:/invalid/path")).isEmpty());
    CHECK(FileOperationsFactory::normalize(QStringLiteral("container://")).isEmpty());
    // docker ps -a --no-trunc gives 64-digit IDs. A single 64-character URL
    // hostname is invalid to Qt; our encoding must retain the full ID.
    const QString fullDockerId(64, QLatin1Char('a'));
    const QString encodedRoot = DockerFileOperations::uri(fullDockerId, QStringLiteral("/"));
    CHECK(encodedRoot.startsWith(QStringLiteral("container://")));
    CHECK(!encodedRoot.isEmpty());
    CHECK(FileOperationsFactory::isContainerPath(encodedRoot));
    CHECK(DockerFileOperations::containerId(encodedRoot) == fullDockerId);
    CHECK(FileOperationsFactory::normalize(encodedRoot) == encodedRoot);
    CHECK(FileOperationsFactory::samePath(FileOperationsFactory::parent(encodedRoot), encodedRoot));
    const QString encodedChild = FileOperationsFactory::child(encodedRoot, QStringLiteral("config file.txt"));
    CHECK(DockerFileOperations::containerId(encodedChild) == fullDockerId);
    CHECK(DockerFileOperations::innerPath(encodedChild) == QStringLiteral("/config file.txt"));
    CHECK(FileOperationsFactory::samePath(FileOperationsFactory::parent(encodedChild), encodedRoot));
    CHECK(FileOperationsFactory::forPath(encodedRoot) != nullptr);
    CHECK(FileOperationsFactory::parent(QStringLiteral("container:")).isEmpty());
    CHECK(FileOperationsFactory::child(QStringLiteral("container:"), QStringLiteral("file.txt")).isEmpty());
    DockerContainerModel containers;
    containers.setContainers({ DockerContainerInfo{ fullDockerId, QStringLiteral("my-container"),
                                                     QStringLiteral("Up"), true } });
    CHECK(containers.rowCount() == 1);
    CHECK(containers.data(containers.index(0, 0), DockerContainerModel::PathRole).toString() == encodedRoot);
    // Hovering a destination tab must never activate it or disturb the source tab.
    TabsViewModel tabs;
    CHECK(tabs.tabsModel()->rowCount() >= 2);
    const QString initialTabPath = tabs.tabsModel()->tabs().first().path;
    const int initialIndex = tabs.currentIndex();
    tabs.activateTabForDrop(1);
    CHECK(tabs.currentIndex() == initialIndex);
    CHECK(tabs.tabsModel()->tabs().first().path == initialTabPath);
    tabs.cancelPendingDropTab();
    CHECK(tabs.currentIndex() == initialIndex);
    tabs.activateTabForDrop(1);
    tabs.activatePendingDropTab();
    CHECK(tabs.currentIndex() == 1);

    // Optional integration test: export FINDEX_TEST_DOCKER_ID=<running-linux-container>.
    const QString dockerId = qEnvironmentVariable("FINDEX_TEST_DOCKER_ID");
    if (!dockerId.isEmpty()) {
        DockerFileOperations docker;
        const QString testRoot = DockerFileOperations::uri(dockerId, QStringLiteral("/tmp/findex-test-%1").arg(QCoreApplication::applicationPid()));
        CHECK(docker.createDirectory(testRoot, &error));
        const QString remoteFile = FileOperationsFactory::child(testRoot, QStringLiteral("hello.txt"));
        CHECK(docker.createFile(remoteFile, &error));
        CHECK(docker.writeText(remoteFile, QStringLiteral("Container test ✓"), &error));
        QString remoteContents;
        CHECK(docker.readText(remoteFile, &remoteContents, &error));
        CHECK(remoteContents == QStringLiteral("Container test ✓"));
        QVector<FileEntry> remoteItems;
        CHECK(docker.list(testRoot, &remoteItems, &error));
        CHECK(remoteItems.size() == 1);
        const QString downloaded = QDir(temp.path()).filePath(QStringLiteral("downloaded.txt"));
        CHECK(docker.exportToLocal(remoteFile, downloaded, &error));
        CHECK(QFileInfo::exists(downloaded));
        CHECK(docker.remove(testRoot, &error));
    }
    qInfo("Core file operations and tab drop tests passed");
    return 0;
}
