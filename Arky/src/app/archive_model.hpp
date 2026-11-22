#pragma once

#include "archive_engine.hpp"

#include <QAbstractListModel>
#include <QMimeDatabase>
#include <QHash>
#include <QSet>
#include <QStringList>

class ArchiveModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
public:
    enum Role { PathRole = Qt::UserRole + 1, NameRole, KindRole, SizeRole,
                SizeTextRole, MimeRole, ModifiedRole, SelectedRole, IconRole, SearchRole,
                CompressedRole, PermissionsRole, MethodRole };
    explicit ArchiveModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int count() const;
    int selectedCount() const;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void resetEntries(std::vector<arky::ArchiveEntryInfo> entries);
    // GUI-thread only. Allows progressive archive indexing in bounded batches.
    void beginIncremental();
    void appendBatch(std::vector<arky::ArchiveEntryInfo> batch);
    void finishIncremental();
    std::vector<arky::ArchiveEntryInfo> takeEntries();
    void restoreEntries(std::vector<arky::ArchiveEntryInfo> entries);
    void setSelectedRows(const QList<int>& rows);
    Q_INVOKABLE void toggle(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    QStringList selectedPaths() const;
    std::vector<arky::ArchiveEntryInfo> selectedEntries() const;
    const std::vector<arky::ArchiveEntryInfo>& allEntries() const { return entries_; }
    const arky::ArchiveEntryInfo* entryAt(int index) const;
signals:
    void countChanged();
    void selectionChanged();
private:
    std::vector<arky::ArchiveEntryInfo> entries_;
    QSet<int> selected_;
    mutable QMimeDatabase mime_;
    QHash<QString, int> loadingIndex_;
};
