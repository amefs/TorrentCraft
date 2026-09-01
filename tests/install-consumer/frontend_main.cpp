#include <filesystem>
#include <optional>
#include <string_view>
#include <torrentutils/frontend/frontend.hpp>

int main()
try
{
    using namespace std::literals;

    const auto config = torrentutils::frontend::parse_config_json(
        R"({"schema":"torrentcraft.config/v1","defaults":{"piece_size":4096}})"sv);
    if (!config)
    {
        return 1;
    }
    const auto resolved = torrentutils::frontend::resolve_settings(config.value().defaults);
    if (!resolved)
    {
        return 1;
    }
    const auto missing_config = std::filesystem::temp_directory_path() /
                                "torrentutils-frontend-install-consumer-missing.json";
    const auto discovery = torrentutils::frontend::discover_config(
        {{missing_config}, missing_config.parent_path(), std::nullopt});
    return resolved.value().options.fixed_piece_length() == 4096U * 1024U && !discovery &&
                   discovery.error().code == torrentutils::core::ErrorCode::FileNotFound
               ? 0
               : 1;
}
catch (...)
{
    return 1;
}
