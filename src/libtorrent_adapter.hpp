#pragma once

#include <torrentutils/core/torrent_engine.hpp>

namespace torrentutils::core::detail {

/** Private translation boundary around the pinned libtorrent backend. */
class LibtorrentAdapter
{
  public:
    [[nodiscard]] static Result<InspectionReport> inspect(const TorrentDocument& document);
    [[nodiscard]] static Result<CreateResult> create(const CreateRequest& request,
                                                     const TaskContext& context);
    [[nodiscard]] static Result<CreatePlan> plan_create(const CreatePlanRequest& request,
                                                        const TaskContext& context);
    [[nodiscard]] static Result<VerificationReport> verify(const VerifyRequest& request,
                                                           const TaskContext& context);
};

} // namespace torrentutils::core::detail
