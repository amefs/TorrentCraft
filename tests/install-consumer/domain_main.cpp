#include <string>
#include <torrentutils/core/domain.hpp>

int main()
try
{
    auto path = torrentutils::core::LogicalPath::from_segments({"payload.bin"});
    auto tracker = torrentutils::core::TrackerUrl::parse("https://tracker.example/announce");
    auto options = torrentutils::core::CreateOptions::create();
    return path.has_value() && path.value().to_string() == "payload.bin" && tracker.has_value() &&
                   options.has_value()
               ? 0
               : 1;
}
catch (...)
{
    return 1;
}
