#pragma once

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <torrentutils/core/torrent_engine.hpp>
#include <torrentutils/core/torrent_info.hpp>
#include <unordered_map>
#include <vector>

/** Core file snapshots projected into an expandable, lazily materialized Qt tree. */
class FileTreeModel final : public QAbstractItemModel
{
    Q_OBJECT
  public:
    enum class Mode : std::uint8_t
    {
        Inspect,
        Verify
    };

    explicit FileTreeModel(QObject* parent = nullptr, Mode mode = Mode::Inspect);
    ~FileTreeModel() override;

    void set_files(const std::vector<torrentutils::core::FileEntry>& files,
                   bool show_padding_files = false);
    void clear();
    void reset_verification();
    void apply_verification_progress(const torrentutils::core::VerificationProgress& progress);
    struct VerificationTotals
    {
        std::uint64_t expected_bytes{};
        std::uint64_t hashed_bytes{};
    };
    [[nodiscard]] VerificationTotals verification_totals() const noexcept;
    void apply_verification_report(const torrentutils::core::VerificationReport& report);
    void mark_verification_cancelled();
    void mark_verification_interrupted();
    void refresh_translations();

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

  private:
    enum class VerificationPhase : std::uint8_t
    {
        NotChecked,
        Checking,
        Checked,
        MismatchDetected,
        Final,
        Cancelled,
        Interrupted
    };

    struct VerificationState
    {
        std::uint64_t expected_bytes{};
        std::uint64_t hashed_bytes{};
        std::uint64_t verified_bytes{};
        std::uint64_t mismatched_bytes{};
        torrentutils::core::FileVerificationFinding findings{
            torrentutils::core::FileVerificationFinding::None};
        VerificationPhase phase{VerificationPhase::NotChecked};
    };

    struct StatusSummary
    {
        VerificationPhase phase{VerificationPhase::NotChecked};
        torrentutils::core::FileVerificationFinding findings{
            torrentutils::core::FileVerificationFinding::None};
        std::uint64_t expected_bytes{};
        std::uint64_t hashed_bytes{};
        std::uint64_t verified_bytes{};
        std::uint64_t mismatched_bytes{};
        std::uint64_t participating_files{};
        std::uint64_t issue_files{};
        std::uint64_t checking_files{};
        std::uint64_t checked_files{};
        std::uint64_t final_files{};
        std::uint64_t mismatch_files{};
        std::uint64_t cancelled_files{};
        std::uint64_t interrupted_files{};
        std::array<std::uint64_t, 7> finding_counts{};
        bool padding{};
    };

    struct IndexedChild
    {
        QString name;
        QString prefix;
        std::string key;
        std::uint64_t size{};
        bool directory{};
        bool padding{};
    };

    struct DirectoryIndex
    {
        std::vector<IndexedChild> children;
        std::unordered_map<std::string, std::size_t> child_rows;
        StatusSummary status;
    };

    struct Node;

    Mode mode_;
    QFileIconProvider icon_provider_;
    std::unique_ptr<Node> root_;
    std::unordered_map<std::string, VerificationState> verification_;
    std::unordered_map<std::string, std::unique_ptr<DirectoryIndex>> directory_index_;
    std::optional<std::uint64_t> last_progress_sequence_;

    static Node* node(const QModelIndex& index) noexcept;
    static QString display_name(const Node& node);
    [[nodiscard]] static QString human_size(std::uint64_t bytes);
    [[nodiscard]] QString exact_size(std::uint64_t bytes) const;
    static void add_status(StatusSummary& aggregate, const VerificationState& state, int direction);
    static StatusSummary finalized_status(const StatusSummary& aggregate);
    void add_status_to_aggregates(const std::string& path, const VerificationState& state);
    void update_status_aggregates(const std::string& path, const VerificationState& previous,
                                  const VerificationState& current);
    [[nodiscard]] StatusSummary status_summary(const Node& item) const;
    [[nodiscard]] QString status_text(const StatusSummary& summary) const;
    [[nodiscard]] QString status_tooltip(const StatusSummary& summary) const;
    [[nodiscard]] QVariant status_icon(const StatusSummary& summary) const;
    void set_interrupted(VerificationPhase phase);
    void emit_status_changed();
    void emit_status_changed(Node& parent, const QModelIndex& parent_index);
};
