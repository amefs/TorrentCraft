#pragma once

#include <string_view>

namespace torrentutils::core::detail {

/**
 * Returns whether value is well-formed UTF-8: no overlong encodings, surrogates (U+D800 to
 * U+DFFF), code points above U+10FFFF or truncated sequences.
 */
[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept;

} // namespace torrentutils::core::detail
