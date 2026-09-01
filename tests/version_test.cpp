#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <torrentutils/core/core.hpp>

TEST_CASE("given_core_when_reading_version_then_reports_1_0_0", "[unit][version]")
{
    using namespace std::literals;

    STATIC_REQUIRE(torrentutils::core::kVersionMajor == 1);
    STATIC_REQUIRE(torrentutils::core::kVersionMinor == 0);
    STATIC_REQUIRE(torrentutils::core::kVersionPatch == 0);

    REQUIRE(torrentutils::core::version() == "1.0.0"sv);
}
