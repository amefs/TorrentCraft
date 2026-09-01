#include "FileTreeModel.hpp"

#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QStringList>
#include <algorithm>
#include <array>

namespace {

using torrentutils::core::FileVerificationFinding;
using torrentutils::core::has_finding;

constexpr std::array verification_findings{FileVerificationFinding::Missing,
                                           FileVerificationFinding::NotRegularFile,
                                           FileVerificationFinding::LengthMismatch,
                                           FileVerificationFinding::HashMismatch,
                                           FileVerificationFinding::SharedPieceMismatch,
                                           FileVerificationFinding::SymlinkMissing,
                                           FileVerificationFinding::SymlinkTargetMismatch};

int finding_count(const FileVerificationFinding findings)
{
    return static_cast<int>(
        std::count_if(verification_findings.begin(), verification_findings.end(),
                      [findings](const auto value) { return has_finding(findings, value); }));
}

QIcon icon_with_fallback(QIcon preferred, const QString& fallback_resource)
{
    constexpr auto probe_size = 16;
    if (!preferred.isNull() && !preferred.pixmap(probe_size, probe_size).isNull())
    {
        return preferred;
    }
    return QIcon(fallback_resource);
}

} // namespace

struct FileTreeModel::Node
{
    Node* parent{};
    int row{};
    QString prefix;
    QString name;
    std::string path_key;
    quint64 size{};
    bool directory{};
    bool padding{};
    bool populated{};
    std::vector<std::unique_ptr<Node>> children;
};

FileTreeModel::FileTreeModel(QObject* parent, const Mode mode)
    : QAbstractItemModel(parent), mode_(mode), root_(new Node)
{
    root_->directory = true;
}

FileTreeModel::~FileTreeModel() = default;

void FileTreeModel::add_status(StatusSummary& aggregate, const VerificationState& state,
                               const int direction)
{
    const auto adjust = [direction](std::uint64_t& target, const std::uint64_t value) {
        if (direction > 0)
        {
            target += value;
        }
        else
        {
            target -= value;
        }
    };
    adjust(aggregate.expected_bytes, state.expected_bytes);
    adjust(aggregate.hashed_bytes, state.hashed_bytes);
    adjust(aggregate.verified_bytes, state.verified_bytes);
    adjust(aggregate.mismatched_bytes, state.mismatched_bytes);
    adjust(aggregate.participating_files, 1);

    switch (state.phase)
    {
    case VerificationPhase::Checking:
        adjust(aggregate.checking_files, 1);
        break;
    case VerificationPhase::Checked:
        adjust(aggregate.checked_files, 1);
        break;
    case VerificationPhase::MismatchDetected:
        adjust(aggregate.mismatch_files, 1);
        break;
    case VerificationPhase::Final:
        adjust(aggregate.final_files, 1);
        if (state.findings != FileVerificationFinding::None)
        {
            adjust(aggregate.issue_files, 1);
        }
        break;
    case VerificationPhase::Cancelled:
        adjust(aggregate.cancelled_files, 1);
        break;
    case VerificationPhase::Interrupted:
        adjust(aggregate.interrupted_files, 1);
        break;
    case VerificationPhase::NotChecked:
        break;
    }

    for (std::size_t index = 0; index < verification_findings.size(); ++index)
    {
        if (has_finding(state.findings, verification_findings[index]))
        {
            adjust(aggregate.finding_counts[index], 1);
        }
    }
    aggregate.findings = FileVerificationFinding::None;
    for (std::size_t index = 0; index < verification_findings.size(); ++index)
    {
        if (aggregate.finding_counts[index] > 0)
        {
            aggregate.findings |= verification_findings[index];
        }
    }
}

FileTreeModel::StatusSummary FileTreeModel::finalized_status(const StatusSummary& aggregate)
{
    auto result = aggregate;
    if (result.participating_files == 0)
    {
        result.padding = true;
        return result;
    }

    result.padding = false;
    if (result.issue_files > 0 || result.final_files == result.participating_files)
    {
        result.phase = VerificationPhase::Final;
    }
    else if (result.mismatch_files > 0)
    {
        result.phase = VerificationPhase::MismatchDetected;
    }
    else if (result.interrupted_files > 0)
    {
        result.phase = VerificationPhase::Interrupted;
    }
    else if (result.cancelled_files > 0)
    {
        result.phase = VerificationPhase::Cancelled;
    }
    else if (result.checking_files > 0 || result.final_files > 0 ||
             (result.checked_files > 0 && result.checked_files < result.participating_files))
    {
        result.phase = VerificationPhase::Checking;
    }
    else if (result.checked_files == result.participating_files)
    {
        result.phase = VerificationPhase::Checked;
    }
    else
    {
        result.phase = VerificationPhase::NotChecked;
    }
    return result;
}

void FileTreeModel::add_status_to_aggregates(const std::string& path,
                                             const VerificationState& state)
{
    const auto add_to = [this, &state](const std::string& prefix) {
        const auto iterator = directory_index_.find(prefix);
        if (iterator != directory_index_.end())
        {
            add_status(iterator->second->status, state, 1);
        }
    };
    add_to({});
    for (auto separator = path.find('/'); separator != std::string::npos;
         separator = path.find('/', separator + 1))
    {
        add_to(path.substr(0, separator));
    }
}

void FileTreeModel::update_status_aggregates(const std::string& path,
                                             const VerificationState& previous,
                                             const VerificationState& current)
{
    const auto update = [this, &previous, &current](const std::string& prefix) {
        const auto iterator = directory_index_.find(prefix);
        if (iterator != directory_index_.end())
        {
            add_status(iterator->second->status, previous, -1);
            add_status(iterator->second->status, current, 1);
        }
    };
    update({});
    for (auto separator = path.find('/'); separator != std::string::npos;
         separator = path.find('/', separator + 1))
    {
        update(path.substr(0, separator));
    }
}

void FileTreeModel::set_files(const std::vector<torrentutils::core::FileEntry>& files,
                              const bool show_padding_files)
{
    beginResetModel();
    verification_.clear();
    directory_index_.clear();
    last_progress_sequence_.reset();
    verification_.reserve(files.size());
    directory_index_.reserve(files.size() + 1);
    directory_index_.emplace(std::string{}, std::make_unique<DirectoryIndex>());
    for (const auto& file : files)
    {
        if (!show_padding_files && file.attributes().padding)
        {
            continue;
        }
        const auto path = file.path().to_string();
        if (!file.attributes().padding)
        {
            const auto inserted =
                verification_.try_emplace(path, VerificationState{file.length(), 0, 0, 0}).second;
            if (!inserted)
            {
                continue;
            }
        }

        std::string parent_prefix;
        std::size_t component_start{};
        while (component_start < path.size())
        {
            const auto separator = path.find('/', component_start);
            const auto component_length = separator == std::string::npos
                                              ? path.size() - component_start
                                              : separator - component_start;
            const auto component = path.substr(component_start, component_length);
            if (component.empty())
            {
                component_start = separator == std::string::npos ? path.size() : separator + 1;
                continue;
            }

            const auto child_key =
                parent_prefix.empty() ? component : parent_prefix + '/' + component;
            auto directory_iterator = directory_index_.find(parent_prefix);
            if (directory_iterator == directory_index_.end())
            {
                directory_iterator =
                    directory_index_.emplace(parent_prefix, std::make_unique<DirectoryIndex>())
                        .first;
            }
            auto& directory = *directory_iterator->second;
            const auto [child_iterator, inserted] =
                directory.child_rows.try_emplace(component, directory.children.size());
            if (inserted)
            {
                directory.children.push_back(
                    {QString::fromStdString(component), QString::fromStdString(child_key),
                     child_key, file.length(), separator != std::string::npos,
                     separator == std::string::npos && file.attributes().padding});
            }
            else
            {
                directory.children[child_iterator->second].size += file.length();
            }

            if (separator == std::string::npos)
            {
                directory.children[child_iterator->second].padding = file.attributes().padding;
                break;
            }
            parent_prefix = child_key;
            component_start = separator + 1;
        }

        if (!file.attributes().padding)
        {
            add_status_to_aggregates(path, verification_.at(path));
        }
    }
    for (auto& entry : directory_index_)
    {
        auto& directory = entry.second;
        std::sort(directory->children.begin(), directory->children.end(),
                  [](const auto& left, const auto& right) {
                      if (left.directory != right.directory)
                      {
                          return left.directory > right.directory;
                      }
                      return left.name < right.name;
                  });
        directory->child_rows.clear();
        directory->child_rows.rehash(0);
    }
    root_ = std::make_unique<Node>();
    root_->directory = true;
    endResetModel();
}

void FileTreeModel::clear()
{
    set_files({});
}

void FileTreeModel::reset_verification()
{
    if (mode_ != Mode::Verify)
    {
        return;
    }
    last_progress_sequence_.reset();
    for (auto& entry : verification_)
    {
        const auto expected = entry.second.expected_bytes;
        entry.second = VerificationState{expected, 0, 0, 0};
    }
    for (auto& entry : directory_index_)
    {
        auto& directory = entry.second;
        directory->status = {};
    }
    for (const auto& entry : verification_)
    {
        add_status_to_aggregates(entry.first, entry.second);
    }
    emit_status_changed();
}

void FileTreeModel::apply_verification_progress(
    const torrentutils::core::VerificationProgress& progress)
{
    if (mode_ != Mode::Verify ||
        (last_progress_sequence_ && progress.sequence <= *last_progress_sequence_))
    {
        return;
    }
    last_progress_sequence_ = progress.sequence;
    for (const auto& file : progress.files)
    {
        const auto iterator = verification_.find(file.path.to_string());
        if (iterator == verification_.end())
        {
            continue;
        }
        const auto previous = iterator->second;
        auto& state = iterator->second;
        state.expected_bytes = file.expected_bytes;
        state.hashed_bytes = file.hashed_bytes;
        state.verified_bytes = file.verified_bytes;
        state.mismatched_bytes = file.mismatched_bytes;
        if (file.mismatched_bytes > 0)
        {
            state.phase = VerificationPhase::MismatchDetected;
        }
        else if (file.expected_bytes > 0 && file.hashed_bytes >= file.expected_bytes)
        {
            state.phase = VerificationPhase::Checked;
        }
        else if (file.hashed_bytes > 0)
        {
            state.phase = VerificationPhase::Checking;
        }
        update_status_aggregates(file.path.to_string(), previous, state);
    }
    emit_status_changed();
}

FileTreeModel::VerificationTotals FileTreeModel::verification_totals() const noexcept
{
    const auto root_iterator = directory_index_.find(std::string{});
    if (root_iterator != directory_index_.end())
    {
        return {root_iterator->second->status.expected_bytes,
                root_iterator->second->status.hashed_bytes};
    }

    VerificationTotals totals;
    for (const auto& entry : verification_)
    {
        totals.expected_bytes += entry.second.expected_bytes;
        totals.hashed_bytes += entry.second.hashed_bytes;
    }
    return totals;
}

void FileTreeModel::apply_verification_report(const torrentutils::core::VerificationReport& report)
{
    if (mode_ != Mode::Verify)
    {
        return;
    }
    for (const auto& file : report.files)
    {
        const auto iterator = verification_.find(file.path.to_string());
        if (iterator == verification_.end())
        {
            continue;
        }
        const auto previous = iterator->second;
        auto& state = iterator->second;
        state.expected_bytes = file.expected_bytes;
        state.hashed_bytes = file.hashed_bytes;
        state.verified_bytes = file.verified_bytes;
        state.mismatched_bytes = file.mismatched_bytes;
        state.findings = file.findings;
        state.phase = VerificationPhase::Final;
        update_status_aggregates(file.path.to_string(), previous, state);
    }
    emit_status_changed();
}

void FileTreeModel::mark_verification_cancelled()
{
    set_interrupted(VerificationPhase::Cancelled);
}

void FileTreeModel::mark_verification_interrupted()
{
    set_interrupted(VerificationPhase::Interrupted);
}

void FileTreeModel::set_interrupted(const VerificationPhase phase)
{
    if (mode_ != Mode::Verify)
    {
        return;
    }
    for (auto& entry : verification_)
    {
        if (entry.second.phase != VerificationPhase::Final)
        {
            const auto previous = entry.second;
            entry.second.phase = phase;
            update_status_aggregates(entry.first, previous, entry.second);
        }
    }
    emit_status_changed();
}

void FileTreeModel::refresh_translations()
{
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    emit_status_changed();
}

FileTreeModel::Node* FileTreeModel::node(const QModelIndex& index) noexcept
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : nullptr;
}

QString FileTreeModel::display_name(const Node& item)
{
    const auto marker = item.padding ? QStringLiteral("[Padding] ") : QString();
    return marker + (item.name.isEmpty() ? QStringLiteral("/") : item.name);
}

QString FileTreeModel::human_size(const std::uint64_t bytes)
{
    constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    auto value = static_cast<long double>(bytes);
    std::size_t unit{};
    while (value >= 1024.0L && unit + 1 < units.size())
    {
        value /= 1024.0L;
        ++unit;
    }

    const QLocale locale;
    if (unit == 0)
    {
        return locale.toString(static_cast<qulonglong>(bytes)) + QStringLiteral(" B");
    }
    auto number = locale.toString(static_cast<double>(value), 'f', 2);
    while (number.endsWith(QLatin1Char('0')))
    {
        number.chop(1);
    }
    const auto decimal = locale.decimalPoint();
    if (number.endsWith(decimal))
    {
        number.chop(decimal.size());
    }
    return number + QLatin1Char(' ') + QString::fromLatin1(units[unit]);
}

QString FileTreeModel::exact_size(const std::uint64_t bytes) const
{
    return tr("%1 bytes").arg(QLocale().toString(static_cast<qulonglong>(bytes)));
}

QModelIndex FileTreeModel::index(const int row, const int column, const QModelIndex& parent) const
{
    if ((parent.isValid() && parent.column() != 0) || column < 0 || column >= columnCount(parent) ||
        row < 0 || row >= rowCount(parent))
    {
        return {};
    }
    auto* parent_node = node(parent);
    if (parent_node == nullptr)
    {
        parent_node = root_.get();
    }
    return createIndex(row, column, parent_node->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const
{
    auto* child_node = node(child);
    if (child_node == nullptr || child_node->parent == nullptr || child_node->parent == root_.get())
    {
        return {};
    }
    auto* parent_node = child_node->parent;
    return createIndex(parent_node->row, 0, parent_node);
}

int FileTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() && parent.column() != 0)
    {
        return 0;
    }
    auto* parent_node = node(parent);
    if (parent_node == nullptr)
    {
        parent_node = root_.get();
    }
    return parent_node->directory && parent_node->populated
               ? static_cast<int>(parent_node->children.size())
               : 0;
}

int FileTreeModel::columnCount(const QModelIndex&) const
{
    return mode_ == Mode::Verify ? 3 : 2;
}

QVariant FileTreeModel::data(const QModelIndex& index, const int role) const
{
    const auto* item = node(index);
    if (item == nullptr || !index.isValid())
    {
        return {};
    }
    if (role == Qt::DisplayRole)
    {
        if (index.column() == 0)
        {
            return display_name(*item);
        }
        if (index.column() == 1)
        {
            return human_size(item->size);
        }
        return status_text(status_summary(*item));
    }
    if (role == Qt::ToolTipRole)
    {
        if (index.column() == 0)
        {
            return item->prefix;
        }
        if (index.column() == 1)
        {
            return exact_size(item->size);
        }
        return status_tooltip(status_summary(*item));
    }
    if (role == Qt::DecorationRole)
    {
        if (index.column() == 0)
        {
            if (item->directory)
            {
                return icon_with_fallback(icon_provider_.icon(QFileIconProvider::Folder),
                                          QStringLiteral(":/torrentcraft/icons/folder.svg"));
            }
            return icon_with_fallback(icon_provider_.icon(QFileInfo(item->name)),
                                      QStringLiteral(":/torrentcraft/icons/file.svg"));
        }
        if (index.column() == 2)
        {
            return status_icon(status_summary(*item));
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() == 1)
    {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::UserRole)
    {
        if (index.column() == 1)
        {
            return QVariant::fromValue<qulonglong>(item->size);
        }
        if (index.column() == 0)
        {
            return item->directory;
        }
    }
    return {};
}

QVariant FileTreeModel::headerData(const int section, const Qt::Orientation orientation,
                                   const int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    switch (section)
    {
    case 0:
        return tr("File");
    case 1:
        return tr("Size");
    case 2:
        return tr("Verification status");
    default:
        return {};
    }
}

bool FileTreeModel::hasChildren(const QModelIndex& parent) const
{
    auto* parent_node = node(parent);
    if (parent_node == nullptr)
    {
        parent_node = root_.get();
    }
    const auto iterator = directory_index_.find(parent_node->path_key);
    return parent_node->directory && iterator != directory_index_.end() &&
           !iterator->second->children.empty();
}

bool FileTreeModel::canFetchMore(const QModelIndex& parent) const
{
    auto* parent_node = node(parent);
    if (parent_node == nullptr)
    {
        parent_node = root_.get();
    }
    const auto iterator = directory_index_.find(parent_node->path_key);
    return parent_node->directory && !parent_node->populated &&
           iterator != directory_index_.end() && !iterator->second->children.empty();
}

void FileTreeModel::fetchMore(const QModelIndex& parent)
{
    auto* parent_node = node(parent);
    if (parent_node == nullptr)
    {
        parent_node = root_.get();
    }
    if (!parent_node->directory || parent_node->populated)
    {
        return;
    }

    const auto directory_iterator = directory_index_.find(parent_node->path_key);
    if (directory_iterator == directory_index_.end() ||
        directory_iterator->second->children.empty())
    {
        parent_node->populated = true;
        return;
    }
    const auto& children = directory_iterator->second->children;
    beginInsertRows(parent, 0, static_cast<int>(children.size()) - 1);
    for (std::size_t row = 0; row < children.size(); ++row)
    {
        const auto& child = children[row];
        auto item = std::make_unique<Node>();
        item->parent = parent_node;
        item->row = static_cast<int>(row);
        item->prefix = child.prefix;
        item->name = child.name;
        item->path_key = child.key;
        item->size = child.size;
        item->directory = child.directory;
        item->padding = child.padding;
        parent_node->children.push_back(std::move(item));
    }
    parent_node->populated = true;
    endInsertRows();
}

FileTreeModel::StatusSummary FileTreeModel::status_summary(const Node& item) const
{
    if (item.padding)
    {
        StatusSummary result;
        result.padding = true;
        return result;
    }

    if (!item.directory)
    {
        const auto iterator = verification_.find(item.path_key);
        if (iterator == verification_.end())
        {
            StatusSummary result;
            result.padding = true;
            return result;
        }
        StatusSummary result;
        add_status(result, iterator->second, 1);
        return finalized_status(result);
    }

    const auto iterator = directory_index_.find(item.path_key);
    if (iterator == directory_index_.end())
    {
        StatusSummary result;
        result.padding = true;
        return result;
    }
    return finalized_status(iterator->second->status);
}

QString FileTreeModel::status_text(const StatusSummary& summary) const
{
    if (summary.padding)
    {
        return tr("N/A (padding)");
    }
    switch (summary.phase)
    {
    case VerificationPhase::NotChecked:
        return tr("Not checked");
    case VerificationPhase::Checking: {
        const auto ratio = summary.expected_bytes == 0
                               ? 0.0
                               : static_cast<double>(summary.hashed_bytes) /
                                     static_cast<double>(summary.expected_bytes);
        return tr("Checking %1%").arg(std::clamp(static_cast<int>(ratio * 100.0), 0, 100));
    }
    case VerificationPhase::Checked:
        return tr("Checked; awaiting result");
    case VerificationPhase::MismatchDetected:
        return tr("Mismatch detected");
    case VerificationPhase::Cancelled:
        return tr("Cancelled");
    case VerificationPhase::Interrupted:
        return tr("Interrupted");
    case VerificationPhase::Final:
        break;
    }

    if (summary.issue_files == 0 && summary.findings == FileVerificationFinding::None)
    {
        return tr("Verified");
    }
    if (summary.participating_files > 1)
    {
        return tr("Issues in %1 files").arg(static_cast<qulonglong>(summary.issue_files));
    }
    if (finding_count(summary.findings) > 1)
    {
        return tr("Multiple issues");
    }
    if (has_finding(summary.findings, FileVerificationFinding::Missing))
        return tr("Missing");
    if (has_finding(summary.findings, FileVerificationFinding::NotRegularFile))
        return tr("Not a regular file");
    if (has_finding(summary.findings, FileVerificationFinding::LengthMismatch))
        return tr("Size mismatch");
    if (has_finding(summary.findings, FileVerificationFinding::HashMismatch))
        return tr("Hash mismatch");
    if (has_finding(summary.findings, FileVerificationFinding::SharedPieceMismatch))
        return tr("Shared-piece mismatch");
    if (has_finding(summary.findings, FileVerificationFinding::SymlinkMissing))
        return tr("Symlink missing");
    if (has_finding(summary.findings, FileVerificationFinding::SymlinkTargetMismatch))
        return tr("Symlink target mismatch");
    return tr("Verification issue");
}

QString FileTreeModel::status_tooltip(const StatusSummary& summary) const
{
    QStringList lines{status_text(summary)};
    if (summary.padding)
    {
        lines << tr("Padding files are not verified.");
        return lines.join(QLatin1Char('\n'));
    }
    if (summary.findings != FileVerificationFinding::None)
    {
        lines << tr("Findings:");
        if (has_finding(summary.findings, FileVerificationFinding::Missing))
            lines << tr("• Missing");
        if (has_finding(summary.findings, FileVerificationFinding::NotRegularFile))
            lines << tr("• Not a regular file");
        if (has_finding(summary.findings, FileVerificationFinding::LengthMismatch))
            lines << tr("• Size mismatch");
        if (has_finding(summary.findings, FileVerificationFinding::HashMismatch))
            lines << tr("• Hash mismatch");
        if (has_finding(summary.findings, FileVerificationFinding::SharedPieceMismatch))
            lines << tr("• Shared-piece mismatch");
        if (has_finding(summary.findings, FileVerificationFinding::SymlinkMissing))
            lines << tr("• Symlink missing");
        if (has_finding(summary.findings, FileVerificationFinding::SymlinkTargetMismatch))
            lines << tr("• Symlink target mismatch");
    }
    lines << tr("Expected: %1").arg(exact_size(summary.expected_bytes));
    lines << tr("Hashed: %1").arg(exact_size(summary.hashed_bytes));
    lines << tr("Verified: %1").arg(exact_size(summary.verified_bytes));
    lines << tr("Mismatched: %1").arg(exact_size(summary.mismatched_bytes));
    return lines.join(QLatin1Char('\n'));
}

QVariant FileTreeModel::status_icon(const StatusSummary& summary) const
{
    QString path;
    const auto shared_piece_only =
        finding_count(summary.findings) == 1 &&
        has_finding(summary.findings, FileVerificationFinding::SharedPieceMismatch);
    if (summary.padding || summary.phase == VerificationPhase::NotChecked)
        path = QStringLiteral(":/torrentcraft/icons/status-idle.svg");
    else if (summary.phase == VerificationPhase::Checking ||
             summary.phase == VerificationPhase::Checked)
        path = QStringLiteral(":/torrentcraft/icons/status-running.svg");
    else if (summary.phase == VerificationPhase::Cancelled)
        path = QStringLiteral(":/torrentcraft/icons/status-cancelled.svg");
    else if (summary.phase == VerificationPhase::Interrupted ||
             summary.phase == VerificationPhase::MismatchDetected ||
             (summary.findings != FileVerificationFinding::None && !shared_piece_only))
        path = QStringLiteral(":/torrentcraft/icons/status-error.svg");
    else if (shared_piece_only)
        path = QStringLiteral(":/torrentcraft/icons/status-warning.svg");
    else
        path = QStringLiteral(":/torrentcraft/icons/status-success.svg");
    return QIcon(path);
}

void FileTreeModel::emit_status_changed()
{
    if (mode_ == Mode::Verify)
    {
        emit_status_changed(*root_, {});
    }
}

void FileTreeModel::emit_status_changed(Node& parent, const QModelIndex& parent_index)
{
    if (!parent.populated || parent.children.empty())
    {
        return;
    }
    const auto first = index(0, 2, parent_index);
    const auto last = index(static_cast<int>(parent.children.size()) - 1, 2, parent_index);
    emit dataChanged(first, last, {Qt::DisplayRole, Qt::DecorationRole, Qt::ToolTipRole});
    for (std::size_t row = 0; row < parent.children.size(); ++row)
    {
        auto& child = *parent.children[row];
        if (child.directory && child.populated)
        {
            emit_status_changed(child, index(static_cast<int>(row), 0, parent_index));
        }
    }
}
