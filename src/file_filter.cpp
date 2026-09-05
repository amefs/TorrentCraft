#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <torrentutils/core/create_options.hpp>
#include <utility>
#include <vector>

namespace torrentutils::core {
namespace {

[[nodiscard]] Error validation_error(std::string field, std::string message)
{
    return {ErrorCode::ValidationFailed,
            "domain validation failed",
            {{std::move(field), std::move(message)}}};
}

[[nodiscard]] std::string normalize_pattern(std::string pattern)
{
    std::replace(pattern.begin(), pattern.end(), '\\', '/');
    return pattern;
}

[[nodiscard]] std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return value;
}

[[nodiscard]] bool glob_match(std::string pattern, std::string value, const bool case_sensitive)
{
    if (!case_sensitive)
    {
        pattern = lower_ascii(std::move(pattern));
        value = lower_ascii(std::move(value));
    }

    const auto width = value.size() + 1U;
    std::vector<std::int8_t> memo((pattern.size() + 1U) * width, -1);
    const auto match = [&](const auto& self, const std::size_t pattern_index,
                           const std::size_t value_index) -> bool {
        auto& cached = memo[pattern_index * width + value_index];
        if (cached != -1)
        {
            return cached != 0;
        }
        bool result = false;
        if (pattern_index == pattern.size())
        {
            result = value_index == value.size();
        }
        else if (pattern[pattern_index] == '*')
        {
            std::size_t next = pattern_index;
            while (next < pattern.size() && pattern[next] == '*')
            {
                ++next;
            }
            const bool recursive = next - pattern_index >= 2U;
            if (recursive)
            {
                result =
                    self(self, next, value_index) ||
                    (value_index < value.size() && self(self, pattern_index, value_index + 1U));
            }
            else
            {
                result = self(self, next, value_index) ||
                         (value_index < value.size() && value[value_index] != '/' &&
                          self(self, pattern_index, value_index + 1U));
            }
        }
        else if (value_index < value.size() &&
                 (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index]))
        {
            result = self(self, pattern_index + 1U, value_index + 1U);
        }
        cached = result ? 1 : 0;
        return result;
    };
    return match(match, 0, 0);
}

[[nodiscard]] std::vector<std::string> common_patterns()
{
    return {".DS_Store",        "._*",         "Icon\r",        "__MACOSX/",
            ".Spotlight-V100/", ".Trashes/",   ".fseventsd/",   "Thumbs.db",
            "ehthumbs.db",      "desktop.ini", "$RECYCLE.BIN/", "System Volume Information/"};
}

[[nodiscard]] bool known_mode(const FileFilterMode mode) noexcept
{
    return mode == FileFilterMode::Disabled || mode == FileFilterMode::CommonArtifacts ||
           mode == FileFilterMode::CustomRules;
}

[[nodiscard]] std::string relative_match_value(std::string_view relative_path,
                                               const std::string& pattern)
{
    std::string path(relative_path);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0)
    {
        path.erase(0, 2);
    }
    const auto normalized_pattern = normalize_pattern(pattern);
    if (normalized_pattern.find('/') == std::string::npos)
    {
        const auto separator = path.find_last_of('/');
        return separator == std::string::npos ? path : path.substr(separator + 1);
    }
    return path;
}

[[nodiscard]] std::string normalized_pattern_for_match(std::string pattern)
{
    pattern = normalize_pattern(std::move(pattern));
    while (!pattern.empty() && pattern.back() == '/')
    {
        pattern.pop_back();
    }
    while (!pattern.empty() && pattern.front() == '/')
    {
        pattern.erase(pattern.begin());
    }
    return pattern;
}

} // namespace

FileFilter::FileFilter(FileFilterMode mode, const bool case_sensitive,
                       std::vector<std::string> patterns)
    : mode_(mode), case_sensitive_(case_sensitive), patterns_(std::move(patterns))
{
}

Result<FileFilter> FileFilter::create(FileFilterInput input)
{
    if (!known_mode(input.mode))
    {
        return Result<FileFilter>::failure(validation_error(
            "create.file_filter.mode", "must be disabled, common-artifacts, or custom-rules"));
    }

    std::vector<std::string> patterns = input.mode == FileFilterMode::CommonArtifacts
                                            ? common_patterns()
                                            : std::move(input.patterns);
    for (std::size_t index = 0; index < patterns.size(); ++index)
    {
        patterns[index] = normalize_pattern(std::move(patterns[index]));
        if (patterns[index].empty() ||
            std::all_of(patterns[index].begin(), patterns[index].end(), [](const char value) {
                return value == ' ' || value == '\t' || value == '\r' || value == '\n';
            }))
        {
            return Result<FileFilter>::failure(validation_error(
                "create.file_filter.patterns[" + std::to_string(index) + "]", "must not be empty"));
        }
        if (patterns[index].find('\0') != std::string::npos)
        {
            return Result<FileFilter>::failure(
                validation_error("create.file_filter.patterns[" + std::to_string(index) + "]",
                                 "must not contain a NUL byte"));
        }
    }
    return Result<FileFilter>::success(
        FileFilter(input.mode, input.case_sensitive, std::move(patterns)));
}

FileFilterMode FileFilter::mode() const noexcept
{
    return mode_;
}

bool FileFilter::case_sensitive() const noexcept
{
    return case_sensitive_;
}

bool FileFilter::enabled() const noexcept
{
    return mode_ != FileFilterMode::Disabled && !patterns_.empty();
}

bool FileFilter::matches(const std::string_view relative_path, const bool directory) const
{
    return !matched_rule(relative_path, directory).empty();
}

std::string FileFilter::matched_rule(const std::string_view relative_path,
                                     const bool directory) const
{
    if (!enabled())
    {
        return {};
    }
    for (const auto& original_pattern : patterns_)
    {
        const auto directory_only = !original_pattern.empty() && original_pattern.back() == '/';
        if (directory_only && !directory)
        {
            continue;
        }
        const auto pattern = normalized_pattern_for_match(original_pattern);
        if (glob_match(pattern, relative_match_value(relative_path, pattern), case_sensitive_))
        {
            return original_pattern;
        }
    }
    return {};
}

} // namespace torrentutils::core
