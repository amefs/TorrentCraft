#include "bencode_adapter.hpp"

#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace torrentutils::core::detail {
namespace {
[[nodiscard]] Error invalid_bencode(std::string message)
{
    return {ErrorCode::InvalidBencode, std::move(message), {}};
}

class Parser
{
  public:
    Parser(std::shared_ptr<const std::vector<std::uint8_t>> input, BencodeLimits limits)
        : input_(std::move(input)), limits_(limits)
    {
    }

    [[nodiscard]] Result<BencodeNode> parse()
    {
        if (input_->size() > limits_.max_input_bytes)
        {
            return Result<BencodeNode>::failure(invalid_bencode("input byte limit exceeded"));
        }
        auto node = value(0);
        if (!node)
        {
            return node;
        }
        if (position_ != input_->size())
        {
            return Result<BencodeNode>::failure(invalid_bencode("trailing bencode data"));
        }
        return node;
    }

  private:
    [[nodiscard]] Result<void> consume_token()
    {
        ++tokens_;
        if (tokens_ > limits_.max_tokens)
        {
            return Result<void>::failure(invalid_bencode("decode token limit exceeded"));
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<BencodeNode> value(const std::size_t depth)
    {
        if (depth > limits_.max_depth)
        {
            return Result<BencodeNode>::failure(invalid_bencode("nesting depth limit exceeded"));
        }
        auto token = consume_token();
        if (!token)
        {
            return Result<BencodeNode>::failure(token.error());
        }
        if (position_ >= input_->size())
        {
            return Result<BencodeNode>::failure(invalid_bencode("unexpected end of input"));
        }

        const auto start = position_;
        const auto prefix = static_cast<char>((*input_)[position_]);
        if (prefix == 'i')
        {
            return integer(start);
        }
        if (prefix == 'l')
        {
            return list(start, depth);
        }
        if (prefix == 'd')
        {
            return dictionary(start, depth);
        }
        return string(start);
    }

    [[nodiscard]] Result<BencodeNode> integer(const std::size_t start)
    {
        ++position_;
        bool negative = false;
        if (position_ < input_->size() && (*input_)[position_] == '-')
        {
            negative = true;
            ++position_;
        }
        const auto first_digit = position_;
        while (position_ < input_->size() && (*input_)[position_] >= '0' &&
               (*input_)[position_] <= '9')
        {
            ++position_;
        }
        const auto digits = position_ - first_digit;
        if (digits == 0 || digits > limits_.max_integer_digits || position_ >= input_->size() ||
            (*input_)[position_] != 'e' ||
            (digits > 1 && (*input_)[first_digit] == static_cast<std::uint8_t>('0')) ||
            (negative && digits == 1 && (*input_)[first_digit] == static_cast<std::uint8_t>('0')))
        {
            return Result<BencodeNode>::failure(invalid_bencode("invalid integer"));
        }

        std::uint64_t magnitude = 0;
        for (auto index = first_digit; index < position_; ++index)
        {
            const auto digit = std::uint64_t((*input_)[index] - static_cast<std::uint8_t>('0'));
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
            {
                return Result<BencodeNode>::failure(invalid_bencode("integer overflow"));
            }
            magnitude = magnitude * 10U + digit;
        }
        ++position_;

        const auto maximum = std::uint64_t(std::numeric_limits<std::int64_t>::max());
        if ((!negative && magnitude > maximum) || (negative && magnitude > maximum + 1U))
        {
            return Result<BencodeNode>::failure(invalid_bencode("integer outside signed range"));
        }

        BencodeNode node;
        node.kind = BencodeNode::Kind::Integer;
        node.encoded_span = {start, position_ - start};
        node.integer = negative
                           ? (magnitude == maximum + 1U ? std::numeric_limits<std::int64_t>::min()
                                                        : -std::int64_t(magnitude))
                           : std::int64_t(magnitude);
        return Result<BencodeNode>::success(std::move(node));
    }

    [[nodiscard]] Result<BencodeNode> list(const std::size_t start, const std::size_t depth)
    {
        ++position_;
        BencodeList values;
        while (position_ < input_->size() && (*input_)[position_] != 'e')
        {
            if (values.size() >= limits_.max_container_entries)
            {
                return Result<BencodeNode>::failure(invalid_bencode("list entry limit exceeded"));
            }
            auto child = value(depth + 1U);
            if (!child)
            {
                return child;
            }
            values.push_back(std::move(child).value());
        }
        if (position_ >= input_->size())
        {
            return Result<BencodeNode>::failure(invalid_bencode("unterminated list"));
        }
        ++position_;

        BencodeNode node;
        node.kind = BencodeNode::Kind::List;
        node.encoded_span = {start, position_ - start};
        node.children = std::move(values);
        return Result<BencodeNode>::success(std::move(node));
    }

    [[nodiscard]] Result<BencodeNode> dictionary(const std::size_t start, const std::size_t depth)
    {
        ++position_;
        BencodeDictionary values;
        std::unordered_set<std::string> keys;
        while (position_ < input_->size() && (*input_)[position_] != 'e')
        {
            if (values.size() >= limits_.max_container_entries)
            {
                return Result<BencodeNode>::failure(
                    invalid_bencode("dictionary entry limit exceeded"));
            }
            auto token = consume_token();
            if (!token)
            {
                return Result<BencodeNode>::failure(token.error());
            }
            const auto key_start = position_;
            auto key = string(key_start);
            if (!key)
            {
                return key;
            }
            const auto key_span = key.value().string_span;
            const auto key_bytes = bytes(key_span);
            if (!keys.insert(key_bytes).second)
            {
                return Result<BencodeNode>::failure(invalid_bencode("duplicate dictionary key"));
            }
            auto child = value(depth + 1U);
            if (!child)
            {
                return child;
            }
            values.emplace_back(key_span, std::move(child).value());
        }
        if (position_ >= input_->size())
        {
            return Result<BencodeNode>::failure(invalid_bencode("unterminated dictionary"));
        }
        ++position_;

        BencodeNode node;
        node.kind = BencodeNode::Kind::Dictionary;
        node.encoded_span = {start, position_ - start};
        node.children = std::move(values);
        return Result<BencodeNode>::success(std::move(node));
    }

    [[nodiscard]] Result<BencodeNode> string(const std::size_t start)
    {
        std::uint64_t length = 0;
        std::size_t digits = 0;
        const auto first_digit = position_;
        while (position_ < input_->size() && (*input_)[position_] >= '0' &&
               (*input_)[position_] <= '9')
        {
            ++digits;
            if (digits > 20U)
            {
                return Result<BencodeNode>::failure(invalid_bencode("string length overflow"));
            }
            const auto digit = std::uint64_t((*input_)[position_] - static_cast<std::uint8_t>('0'));
            ++position_;
            if (length > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
            {
                return Result<BencodeNode>::failure(invalid_bencode("string length overflow"));
            }
            length = length * 10U + digit;
        }
        if (digits == 0 || (digits > 1U && (*input_)[first_digit] == '0') ||
            position_ >= input_->size() || (*input_)[position_] != ':' ||
            length > limits_.max_string_bytes)
        {
            return Result<BencodeNode>::failure(invalid_bencode("invalid string length"));
        }
        ++position_;
        if (length > input_->size() - position_)
        {
            return Result<BencodeNode>::failure(invalid_bencode("truncated string"));
        }

        const auto content_start = position_;
        position_ += std::size_t(length);
        BencodeNode node;
        node.kind = BencodeNode::Kind::String;
        node.encoded_span = {start, position_ - start};
        node.string_span = {content_start, std::size_t(length)};
        return Result<BencodeNode>::success(std::move(node));
    }

    [[nodiscard]] std::string bytes(const BencodeSpan span) const
    {
        return std::string(reinterpret_cast<const char*>(input_->data() + span.offset), span.size);
    }

    std::shared_ptr<const std::vector<std::uint8_t>> input_;
    BencodeLimits limits_;
    std::size_t position_{};
    std::size_t tokens_{};
};
} // namespace

Result<BencodeDocument> BencodeAdapter::decode(std::vector<std::uint8_t> bytes,
                                               const BencodeLimits& limits)
{
    auto input = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    Parser parser(input, limits);
    auto root = parser.parse();
    if (!root)
    {
        const auto& error = root.error();
        return Result<BencodeDocument>::failure(Error{error.code, error.message, error.issues});
    }
    return Result<BencodeDocument>::success({std::move(input), std::move(root).value()});
}

} // namespace torrentutils::core::detail
