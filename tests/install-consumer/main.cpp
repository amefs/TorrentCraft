#include <string>
#include <string_view>
#include <torrentutils/core/core.hpp>
#include <type_traits>
#include <utility>

int main()
try
{
    using namespace std::literals;

    torrentutils::core::CancellationSource source;
    const auto token = source.token();
    auto path = torrentutils::core::LogicalPath::from_segments({"payload.bin"});
    const bool foundation_is_transitive = !token.is_cancelled();
    const bool domain_is_transitive = path.has_value();
    auto endpoint = torrentutils::core::TrackerUrl::parse("https://tracker.example/announce");
    auto tier = endpoint ? torrentutils::core::TrackerTier::create({std::move(endpoint).value()})
                         : torrentutils::core::Result<torrentutils::core::TrackerTier>::failure(
                               endpoint.error());
    auto trackers =
        tier ? torrentutils::core::TrackerList::create({std::move(tier).value()})
             : torrentutils::core::Result<torrentutils::core::TrackerList>::failure(tier.error());
    const bool tracker_engine_is_available =
        trackers &&
        torrentutils::core::TrackerEngine::export_json(trackers.value()).find("tracker-list/v1") !=
            std::string::npos;
    const torrentutils::core::TorrentEngine torrent_engine;
    torrentutils::core::CreateOptionsInput create_options_input;
    create_options_input.file_order_policy = torrentutils::core::FileOrderPolicy::Natural;
    const auto create_options = torrentutils::core::CreateOptions::create(create_options_input);
    torrentutils::core::VerificationResourceBudgetInput verification_budget_input;
    verification_budget_input.hashing_workers = 1;
    verification_budget_input.checking_memory_bytes = 16ULL * 1024ULL;
    verification_budget_input.max_logical_files = 1;
    verification_budget_input.max_pieces = 1;
    const auto verification_budget =
        torrentutils::core::VerificationResourceBudget::create(verification_budget_input);

    static_assert(std::is_empty_v<torrentutils::core::TorrentEngine>);
    static_assert(std::is_copy_constructible_v<torrentutils::core::VerificationProgressCallback>);
    const torrentutils::core::PieceRange piece_range{
        0, 1, torrentutils::core::PieceVerificationState::Verified};
    const bool torrent_engine_contract_is_available =
        torrentutils::core::is_valid(piece_range) && create_options &&
        create_options.value().file_order_policy() ==
            torrentutils::core::FileOrderPolicy::Natural &&
        verification_budget && verification_budget.value().max_pieces() == 1 &&
        verification_budget.value().checking_memory_bytes() == 16ULL * 1024ULL &&
        torrentutils::core::VerificationCapability::Supported !=
            torrentutils::core::VerificationCapability::Unsupported;
    static_cast<void>(torrent_engine);
    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    static_cast<void>(service);
    const bool application_service_is_available =
        std::is_constructible_v<torrentutils::core::TorrentService,
                                torrentutils::core::TorrentRepository&, torrentutils::core::Clock&>;
    const bool version_is_current = torrentutils::core::version() == "1.0.0"sv;
    return foundation_is_transitive && domain_is_transitive && tracker_engine_is_available &&
                   torrent_engine_contract_is_available && application_service_is_available &&
                   version_is_current
               ? 0
               : 1;
}
catch (...)
{
    return 1;
}
