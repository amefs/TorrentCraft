#include "../src/utf8.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using torrentutils::core::detail::is_valid_utf8;

TEST_CASE("given_well_formed_utf8_when_validated_then_it_is_accepted", "[unit][utf8]")
{
    CHECK(is_valid_utf8(""));
    CHECK(is_valid_utf8("tracker.example/announce"));
    CHECK(is_valid_utf8(std::string_view("a\0b", 3)));
    CHECK(is_valid_utf8("\xC2\x80"));         // U+0080, smallest two-byte
    CHECK(is_valid_utf8("\xE2\x82\xAC"));     // U+20AC euro sign
    CHECK(is_valid_utf8("\xED\x9F\xBF"));     // U+D7FF, just below surrogates
    CHECK(is_valid_utf8("\xEE\x80\x80"));     // U+E000, just above surrogates
    CHECK(is_valid_utf8("\xF0\x9F\x8E\xB5")); // U+1F3B5
    CHECK(is_valid_utf8("\xF4\x8F\xBF\xBF")); // U+10FFFF, the last code point
    CHECK(is_valid_utf8(u8"安達としまむら"));
}

TEST_CASE("given_malformed_utf8_when_validated_then_it_is_rejected", "[unit][utf8]")
{
    CHECK_FALSE(is_valid_utf8("\x80"));                 // lone continuation byte
    CHECK_FALSE(is_valid_utf8("\xC0\x80"));             // overlong NUL
    CHECK_FALSE(is_valid_utf8("\xC1\xBF"));             // overlong two-byte
    CHECK_FALSE(is_valid_utf8("\xE0\x80\x80"));         // overlong three-byte
    CHECK_FALSE(is_valid_utf8("\xF0\x80\x80\x80"));     // overlong four-byte
    CHECK_FALSE(is_valid_utf8("\xED\xA0\x80"));         // U+D800 surrogate
    CHECK_FALSE(is_valid_utf8("\xED\xBF\xBF"));         // U+DFFF surrogate
    CHECK_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));     // above U+10FFFF
    CHECK_FALSE(is_valid_utf8("\xF8\x88\x80\x80\x80")); // five-byte lead
    CHECK_FALSE(is_valid_utf8("\xFF"));
    CHECK_FALSE(is_valid_utf8("\xE2\x82"));       // truncated at the end
    CHECK_FALSE(is_valid_utf8("\xE2\x82x"));      // ASCII where a continuation belongs
    CHECK_FALSE(is_valid_utf8("ok\xF0\x9F\x8E")); // truncated after valid text
}
