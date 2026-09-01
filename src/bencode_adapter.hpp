#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <torrentutils/core/result.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace torrentutils::core::detail {

struct BencodeLimits
{
    std::size_t max_input_bytes{128U * 1024U * 1024U};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_depth{64U};
    std::size_t max_tokens{10'000'000U};
    std::size_t max_container_entries{1'000'000U};
    std::size_t max_integer_digits{20U};
};

struct BencodeSpan
{
    std::size_t offset{};
    std::size_t size{};
};

struct BencodeNode;
using BencodeList = std::vector<BencodeNode>;
using BencodeDictionary = std::vector<std::pair<BencodeSpan, BencodeNode>>;

struct BencodeNode
{
    enum class Kind
    {
        Integer,
        String,
        List,
        Dictionary
    };

    Kind kind{};
    BencodeSpan encoded_span{};
    BencodeSpan string_span{};
    std::int64_t integer{};
    std::variant<std::monostate, BencodeList, BencodeDictionary> children;
};

struct BencodeDocument
{
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    BencodeNode root;
};

class BencodeAdapter
{
  public:
    [[nodiscard]] static Result<BencodeDocument> decode(std::vector<std::uint8_t> bytes,
                                                        const BencodeLimits& limits = {});
};

} // namespace torrentutils::core::detail
