#include "archive_model.hpp"

#include <QDateTime>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <QLocale>

ArchiveModel::ArchiveModel(QObject* parent) : QAbstractListModel(parent) {}
int ArchiveModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}
int ArchiveModel::count() const { return rowCount(); }
int ArchiveModel::selectedCount() const { return selected_.size(); }
const arky::ArchiveEntryInfo* ArchiveModel::entryAt(int index) const {
    if (index < 0 || index >= count()) return nullptr;
    return &entries_[static_cast<std::size_t>(index)];
}
QVariant ArchiveModel::data(const QModelIndex& index, int role) const {
    const auto* e = entryAt(index.row());
    if (!index.isValid() || !e) return {};
    const QString path = QString::fromUtf8(e->path.c_str());
    const QString fileName = path.section('/', -1);
    const bool directory = e->type == arky::EntryType::Directory;
    // Avoid MIME database lookups on every role request while scrolling/filtering.
    const auto mime = [&] { return directory ? QStringLiteral("inode/directory")
        : mime_.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension).name(); };
    const QString kind = directory ? QStringLiteral("Folder")
        : e->type == arky::EntryType::Link ? QStringLiteral("Link")
        : e->type == arky::EntryType::Regular ? QStringLiteral("File") : QStringLiteral("Special");
    switch (role) {
        case PathRole: return path;
        case NameRole: return fileName.isEmpty() ? path : fileName;
        case KindRole: return kind;
        case SizeRole: return QVariant::fromValue(static_cast<qulonglong>(e->size));
        case SizeTextRole: return e->sizeKnown && !directory
             ? QLocale().formattedDataSize(static_cast<qint64>(e->size)) : QStringLiteral("—");
        case MimeRole: return mime();
        case ModifiedRole: return e->modified > 0
            ? QDateTime::fromSecsSinceEpoch(e->modified).toString("yyyy-MM-dd  hh:mm") : QStringLiteral("—");
        case SelectedRole: return selected_.contains(index.row());
        case IconRole: return directory ? QStringLiteral("folder") : QStringLiteral("file");
        case SearchRole: return path + " " + kind + " " + mime();
        case CompressedRole: return e->compressedSizeKnown
            ? QLocale().formattedDataSize(static_cast<qint64>(e->compressedSize)) : QStringLiteral("Unknown");
        case PermissionsRole: return e->permissionsKnown
            ? QString::number(e->permissions, 8).rightJustified(3, '0') : QStringLiteral("Unknown");
        case MethodRole: return e->compressionMethod.empty() ? QStringLiteral("Unknown") : QString::fromStdString(e->compressionMethod);
    }
    return {};
}
QHash<int, QByteArray> ArchiveModel::roleNames() const {
    return {{PathRole, "path"}, {NameRole, "name"}, {KindRole, "kind"},
            {SizeRole, "size"}, {SizeTextRole, "sizeText"}, {MimeRole, "mime"},
            {ModifiedRole, "modified"}, {SelectedRole, "selected"},
            {IconRole, "iconName"}, {SearchRole, "search"}, {CompressedRole, "compressed"},
            {PermissionsRole, "permissions"}, {MethodRole, "method"}};
}
void ArchiveModel::resetEntries(std::vector<arky::ArchiveEntryInfo> entries) {
    // Build directory rows for archive formats that omit explicit folder headers.
    // No file payloads are loaded; this is an O(N paths) metadata index.
    std::unordered_set<std::string> seen;
    for (auto& entry : entries) {
        while (!entry.path.empty() && entry.path.back() == '/') entry.path.pop_back();
        if (!entry.path.empty()) seen.insert(entry.path);
    }
    std::vector<arky::ArchiveEntryInfo> implicit;
    for (const auto& entry : entries) {
        auto pos = entry.path.find('/');
        while (pos != std::string::npos) {
            const auto parent = entry.path.substr(0, pos);
            if (seen.insert(parent).second) {
                arky::ArchiveEntryInfo folder;
                folder.path = parent;
                folder.type = arky::EntryType::Directory;
                implicit.push_back(std::move(folder));
            }
            pos = entry.path.find('/', pos+1);
        }
    }
    entries.insert(entries.end(), implicit.begin(), implicit.end());
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.type == arky::EntryType::Directory && b.type != arky::EntryType::Directory) return true;
        if (a.type != arky::EntryType::Directory && b.type == arky::EntryType::Directory) return false;
        return a.path < b.path;
    });
    beginResetModel();
    entries_ = std::move(entries);
    selected_.clear();
    endResetModel();
    emit countChanged();
    emit selectionChanged();
}
void ArchiveModel::beginIncremental() {
    beginResetModel();
    entries_.clear();
    loadingIndex_.clear();
    selected_.clear();
    endResetModel();
    emit countChanged();
    emit selectionChanged();
}
void ArchiveModel::appendBatch(std::vector<arky::ArchiveEntryInfo> batch) {
    std::vector<arky::ArchiveEntryInfo> additions;
    std::vector<int> changedRows;
    auto addOne = [&](arky::ArchiveEntryInfo item, bool synthetic) {
        while (!item.path.empty() && item.path.back() == '/') item.path.pop_back();
        if (item.path.empty()) return;
        const QString key = QString::fromUtf8(item.path.c_str());
        auto found = loadingIndex_.constFind(key);
        if (found != loadingIndex_.cend()) {
            const int existingRow = found.value();
            // Replace an earlier synthetic folder with its real archive metadata.
            if (!synthetic && item.type == arky::EntryType::Directory) {
                if (static_cast<std::size_t>(existingRow) < entries_.size() &&
                    entries_[existingRow].type == arky::EntryType::Directory) {
                    entries_[existingRow] = std::move(item);
                    changedRows.push_back(existingRow);
                    return;
                }
                const auto pending = static_cast<std::size_t>(existingRow) - entries_.size();
                if (existingRow >= static_cast<int>(entries_.size()) && pending < additions.size() &&
                    additions[pending].type == arky::EntryType::Directory) {
                    additions[pending] = std::move(item);
                    return;
                }
            }
            if (synthetic) return;
            // Keep duplicate file records visible, even though editing them is
            // deliberately prohibited by ArchiveEngine's collision guard.
        } else loadingIndex_.insert(key, static_cast<int>(entries_.size() + additions.size()));
        additions.push_back(std::move(item));
    };
    for (auto& item : batch) {
        auto slash = item.path.find('/');
        while (slash != std::string::npos) {
            arky::ArchiveEntryInfo parent;
            parent.path = item.path.substr(0, slash);
            parent.type = arky::EntryType::Directory;
            addOne(std::move(parent), true);
            slash = item.path.find('/', slash + 1);
        }
        addOne(std::move(item), false);
    }
    if (!additions.empty()) {
        const int first = static_cast<int>(entries_.size());
        const int last = first + static_cast<int>(additions.size()) - 1;
        beginInsertRows({}, first, last);
        entries_.insert(entries_.end(), std::make_move_iterator(additions.begin()),
                        std::make_move_iterator(additions.end()));
        endInsertRows();
        emit countChanged();
    }
    for (int row : changedRows) emit dataChanged(index(row), index(row));
}
void ArchiveModel::finishIncremental() {
    loadingIndex_.clear();
    // The proxy handles sorting. Preserve selections and avoid a second
    // full index copy and GUI-thread model reset on completion.
}
std::vector<arky::ArchiveEntryInfo> ArchiveModel::takeEntries() {
    beginResetModel();
    auto entries = std::move(entries_);
    entries_.clear();
    selected_.clear();
    loadingIndex_.clear();
    endResetModel();
    emit countChanged(); emit selectionChanged();
    return entries;
}
void ArchiveModel::restoreEntries(std::vector<arky::ArchiveEntryInfo> entries) {
    beginResetModel();
    entries_ = std::move(entries);
    selected_.clear();
    endResetModel();
    emit countChanged(); emit selectionChanged();
}
void ArchiveModel::setSelectedRows(const QList<int>& rows) {
    selected_.clear();
    for (int row : rows) if (entryAt(row)) selected_.insert(row);
    if (count()) emit dataChanged(index(0), index(count()-1), {SelectedRole});
    emit selectionChanged();
}
void ArchiveModel::toggle(int index) {
    if (!entryAt(index)) return;
    if (selected_.contains(index)) selected_.remove(index);
    else selected_.insert(index);
    emit dataChanged(this->index(index), this->index(index), {SelectedRole});
    emit selectionChanged();
}
void ArchiveModel::selectAll() {
    selected_.clear();
    for (int i = 0; i < count(); ++i) selected_.insert(i);
    if (count()) emit dataChanged(index(0), index(count()-1), {SelectedRole});
    emit selectionChanged();
}
void ArchiveModel::clearSelection() {
    if (selected_.empty()) return;
    selected_.clear();
    if (count()) emit dataChanged(index(0), index(count()-1), {SelectedRole});
    emit selectionChanged();
}
QStringList ArchiveModel::selectedPaths() const {
    QStringList result;
    auto rows = selected_.values();
    std::sort(rows.begin(), rows.end());
    for (int i : rows) if (auto* entry = entryAt(i)) result << QString::fromUtf8(entry->path.c_str());
    return result;
}
std::vector<arky::ArchiveEntryInfo> ArchiveModel::selectedEntries() const {
    std::vector<arky::ArchiveEntryInfo> result;
    for (int index : selected_) if (auto* entry = entryAt(index)) result.push_back(*entry);
    return result;
}
