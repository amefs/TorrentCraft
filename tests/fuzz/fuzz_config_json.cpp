// libFuzzer target: TorrentCraft config JSON parsing and validation (src/frontend.cpp).
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <torrentutils/frontend/settings.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer hands raw bytes.
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    static_cast<void>(torrentutils::frontend::parse_config_json(text));
    return 0;
}
