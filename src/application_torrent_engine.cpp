#include <torrentutils/core/application.hpp>

namespace torrentutils::core {
Result<InspectionReport> TorrentService::inspect(const TorrentDocument& document,
                                                 const TaskContext& context) const
{
    return TorrentEngine{}.inspect(document, context);
}

Result<CreateResult> TorrentService::create(const CreateRequest& request,
                                            const TaskContext& context) const
{
    return TorrentEngine{}.create(request, context);
}

Result<VerificationReport> TorrentService::verify(const VerifyRequest& request,
                                                  const TaskContext& context) const
{
    return TorrentEngine{}.verify(request, context);
}
} // namespace torrentutils::core
