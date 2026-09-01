#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <torrentutils/core/torrent_engine.hpp>
#include <vector>

namespace torrentutils::core::detail {

struct VerificationFileOverlap
{
    std::size_t file_index;
    std::uint64_t bytes;
};

struct VerificationPieceLayout
{
    bool hashable{true};
    std::vector<VerificationFileOverlap> overlaps;
};

using VerificationPieceLayouts = std::vector<VerificationPieceLayout>;

class VerificationProgressPublisher
{
  public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    VerificationProgressPublisher(std::vector<FileVerificationProgress> files,
                                  const VerificationPieceLayouts& layouts,
                                  VerificationProgressCallback callback,
                                  CancellationToken cancellation = {});
    VerificationProgressPublisher(std::vector<FileVerificationProgress> files,
                                  const VerificationPieceLayouts& layouts,
                                  VerificationProgressCallback callback,
                                  CancellationToken cancellation, Clock clock);

    void record(std::uint64_t piece, PieceVerificationState state);
    void flush();
    void complete(const std::vector<PieceVerificationState>& states);

  private:
    void insert_range(std::uint64_t piece, PieceVerificationState state);
    void deliver(bool force);

    static constexpr std::size_t max_ranges_per_snapshot = 256U;
    static constexpr std::size_t max_files_per_snapshot = 256U;
    static constexpr auto normal_cadence = std::chrono::milliseconds{50};

    const VerificationPieceLayouts& layouts_;
    VerificationProgressCallback callback_;
    CancellationToken cancellation_;
    Clock clock_;
    std::chrono::steady_clock::time_point last_delivery_;
    std::vector<bool> emitted_;
    std::vector<FileVerificationProgress> files_;
    std::vector<bool> changed_files_;
    std::size_t pending_file_count_{};
    std::map<std::uint64_t, PieceRange> ranges_;
    std::uint64_t sequence_{1};
};

} // namespace torrentutils::core::detail
