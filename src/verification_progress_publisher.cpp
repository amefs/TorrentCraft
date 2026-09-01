#include "verification_progress_publisher.hpp"

#include <algorithm>
#include <utility>

namespace torrentutils::core::detail {

VerificationProgressPublisher::VerificationProgressPublisher(
    std::vector<FileVerificationProgress> files, const VerificationPieceLayouts& layouts,
    VerificationProgressCallback callback, CancellationToken cancellation)
    : VerificationProgressPublisher(std::move(files), layouts, std::move(callback),
                                    std::move(cancellation),
                                    []() { return std::chrono::steady_clock::now(); })
{
}

VerificationProgressPublisher::VerificationProgressPublisher(
    std::vector<FileVerificationProgress> files, const VerificationPieceLayouts& layouts,
    VerificationProgressCallback callback, CancellationToken cancellation, Clock clock)
    : layouts_(layouts), callback_(std::move(callback)), cancellation_(std::move(cancellation)),
      clock_(std::move(clock)), last_delivery_(clock_()), emitted_(layouts.size(), false),
      files_(std::move(files)), changed_files_(files_.size(), false)
{
}

void VerificationProgressPublisher::record(std::uint64_t piece, PieceVerificationState state)
{
    if (cancellation_.is_cancelled() || piece >= emitted_.size() || emitted_[piece])
    {
        return;
    }
    const auto& layout = layouts_[piece];
    if (!layout.hashable)
    {
        return;
    }
    emitted_[piece] = true;
    for (const auto& overlap : layout.overlaps)
    {
        auto& file = files_[overlap.file_index];
        file.hashed_bytes += overlap.bytes;
        if (state == PieceVerificationState::Verified)
        {
            file.verified_bytes += overlap.bytes;
        }
        else
        {
            file.mismatched_bytes += overlap.bytes;
        }
        if (!changed_files_[overlap.file_index])
        {
            changed_files_[overlap.file_index] = true;
            ++pending_file_count_;
        }
    }

    insert_range(piece, state);
    if (ranges_.size() >= max_ranges_per_snapshot || pending_file_count_ >= max_files_per_snapshot)
    {
        deliver(true);
    }
}

void VerificationProgressPublisher::flush()
{
    deliver(false);
}

void VerificationProgressPublisher::complete(const std::vector<PieceVerificationState>& states)
{
    deliver(true);
    for (std::uint64_t piece = 0; piece < emitted_.size() && !cancellation_.is_cancelled(); ++piece)
    {
        if (emitted_[piece])
        {
            continue;
        }
        const auto state =
            piece < states.size() ? states[piece] : PieceVerificationState::Mismatched;
        record(piece, state);
    }
    deliver(true);
}

void VerificationProgressPublisher::insert_range(std::uint64_t piece, PieceVerificationState state)
{
    auto next = ranges_.lower_bound(piece);
    auto previous = next;
    const bool has_previous = previous != ranges_.begin();
    if (has_previous)
    {
        --previous;
    }

    const bool merge_previous =
        has_previous && previous->second.end == piece && previous->second.state == state;
    const bool merge_next =
        next != ranges_.end() && next->second.begin == piece + 1U && next->second.state == state;
    if (merge_previous && merge_next)
    {
        previous->second.end = next->second.end;
        ranges_.erase(next);
        return;
    }
    if (merge_previous)
    {
        previous->second.end = piece + 1U;
        return;
    }
    if (merge_next)
    {
        const auto end = next->second.end;
        ranges_.erase(next);
        ranges_.emplace(piece, PieceRange{piece, end, state});
        return;
    }
    ranges_.emplace(piece, PieceRange{piece, piece + 1U, state});
}

void VerificationProgressPublisher::deliver(bool force)
{
    if (cancellation_.is_cancelled() || (ranges_.empty() && pending_file_count_ == 0U))
    {
        return;
    }
    const auto now = clock_();
    if (!force && now - last_delivery_ < normal_cadence)
    {
        return;
    }

    while ((!ranges_.empty() || pending_file_count_ != 0U) && !cancellation_.is_cancelled())
    {
        std::vector<FileVerificationProgress> changed;
        changed.reserve((std::min)(max_files_per_snapshot, pending_file_count_));
        for (std::size_t index = 0;
             index < files_.size() && changed.size() < max_files_per_snapshot; ++index)
        {
            if (changed_files_[index])
            {
                changed.push_back(files_[index]);
                changed_files_[index] = false;
                --pending_file_count_;
            }
        }

        std::vector<PieceRange> ranges;
        const auto range_count = (std::min)(max_ranges_per_snapshot, ranges_.size());
        ranges.reserve(range_count);
        auto range = ranges_.begin();
        for (std::size_t count = 0; count < range_count; ++count)
        {
            ranges.push_back(range->second);
            range = ranges_.erase(range);
        }
        callback_({sequence_++, std::move(changed), std::move(ranges)});
    }
    last_delivery_ = now;
}

} // namespace torrentutils::core::detail
