#include "utf8.hpp"

#include <cstddef>
#include <cstdint>

namespace torrentutils::core::detail {

bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }

        std::size_t count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            count = 2;
            code_point = first & 0x1FU;
            minimum = 0x80U;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            count = 3;
            code_point = first & 0x0FU;
            minimum = 0x800U;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            count = 4;
            code_point = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            return false;
        }

        if (count > value.size() - index)
        {
            return false;
        }
        for (std::size_t offset = 1; offset < count; ++offset)
        {
            const auto next = static_cast<std::uint8_t>(value[index + offset]);
            if ((next & 0xC0U) != 0x80U)
            {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU))
        {
            return false;
        }
        index += count;
    }
    return true;
}

} // namespace torrentutils::core::detail
