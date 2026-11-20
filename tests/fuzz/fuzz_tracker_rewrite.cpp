// libFuzzer target: sed-style tracker rewrite rules (src/tracker_rewrite.cpp). Each input line is
// one rule; rules that parse are applied to a fixed tracker list.
#include <cstddef>
#include <cstdint>
#include <string>
#include <torrentutils/core/tracker.hpp>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils::core;

[[nodiscard]] TrackerList sample_trackers()
{
    std::vector<TrackerTier> tiers;
    for (const auto* urls :
         {"https://tracker.example/announce?passkey=abc", "udp://tracker.example:6969/announce"})
    {
        std::vector<TrackerUrl> trackers;
        trackers.push_back(TrackerUrl::parse(urls).value());
        tiers.push_back(TrackerTier::create(std::move(trackers)).value());
    }
    return TrackerList::create(std::move(tiers)).value();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    static const TrackerList trackers = sample_trackers();
    std::vector<std::string> expressions;
    std::string line;
    for (std::size_t index = 0; index < size; ++index)
    {
        const auto byte = static_cast<char>(data[index]);
        if (byte == '\n')
        {
            expressions.push_back(std::move(line));
            line.clear();
            continue;
        }
        line.push_back(byte);
    }
    expressions.push_back(std::move(line));

    auto rules = TrackerRewriteRules::parse(expressions);
    if (rules)
    {
        static_cast<void>(preview_tracker_rewrite(trackers, rules.value()));
    }
    return 0;
}
