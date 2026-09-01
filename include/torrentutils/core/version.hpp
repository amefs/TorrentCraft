#pragma once

#include <cstdint>
#include <string_view>

namespace torrentutils::core {

/** Compile-time major component of the Core SDK version. */
inline constexpr std::uint32_t kVersionMajor = 1;

/** Compile-time minor component of the Core SDK version. */
inline constexpr std::uint32_t kVersionMinor = 0;

/** Compile-time patch component of the Core SDK version. */
inline constexpr std::uint32_t kVersionPatch = 0;

/**
 * Returns the Core SDK version in dotted decimal form.
 *
 * @return A process-lifetime string view containing the SDK version.
 */
[[nodiscard]] std::string_view version() noexcept;

} // namespace torrentutils::core
