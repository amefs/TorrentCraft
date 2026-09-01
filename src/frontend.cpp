#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>
#include <torrentutils/frontend/settings.hpp>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
// clang-format on
#endif

namespace torrentutils::frontend {
namespace {

constexpr std::size_t kMaximumInputBytes = std::size_t{1} * 1024U * 1024U;
constexpr std::size_t kMaximumJsonDepth = 64U;
constexpr std::uint32_t kMinimumPieceSizeKib = 16U;
constexpr std::uint32_t kMaximumPieceSizeKib = 16384U;

using Json = nlohmann::json;

[[nodiscard]] core::Error validation_error(std::string field, std::string message)
{
    return {core::ErrorCode::ValidationFailed,
            "frontend settings validation failed",
            {{std::move(field), std::move(message)}}};
}

[[nodiscard]] core::Error validation_errors(std::vector<core::FieldIssue> issues)
{
    return {core::ErrorCode::ValidationFailed, "frontend settings validation failed",
            std::move(issues)};
}

struct JsonParseOutcome
{
    Json document;
    std::vector<core::FieldIssue> issues;
};

[[nodiscard]] std::optional<std::uint64_t> parse_json_memory_size(const Json& value)
{
    if (value.is_number_unsigned())
    {
        const auto number = value.get<std::uint64_t>();
        return number == 0U ? std::nullopt : std::optional<std::uint64_t>(number);
    }
    if (value.is_number_integer())
    {
        const auto integer = value.get<std::int64_t>();
        return integer > 0 ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(integer))
                           : std::nullopt;
    }
    if (value.is_string())
    {
        return parse_memory_size(value.get<std::string>());
    }
    return std::nullopt;
}

[[nodiscard]] core::Result<JsonParseOutcome> parse_json(std::string_view input,
                                                        std::string_view field)
{
    if (input.size() > kMaximumInputBytes)
    {
        return core::Result<JsonParseOutcome>::failure(
            validation_error(std::string(field), "input exceeds the 1 MiB limit"));
    }

    bool exceeded_depth = false;
    std::vector<std::unordered_set<std::string>> object_fields;
    std::vector<core::FieldIssue> issues;
    auto document = Json::parse(
        input.begin(), input.end(),
        [&exceeded_depth, &object_fields, &issues,
         field](const int depth, const Json::parse_event_t event, Json& parsed) {
            if (depth > static_cast<int>(kMaximumJsonDepth))
            {
                exceeded_depth = true;
                return false;
            }
            if (event == Json::parse_event_t::object_start)
            {
                object_fields.emplace_back();
            }
            else if (event == Json::parse_event_t::key && !object_fields.empty())
            {
                const auto& key = parsed.get_ref<const Json::string_t&>();
                if (!object_fields.back().insert(key).second)
                {
                    issues.push_back(
                        {std::string(field) + "." + key, "must not appear more than once"});
                }
            }
            else if (event == Json::parse_event_t::object_end && !object_fields.empty())
            {
                object_fields.pop_back();
            }
            return true;
        },
        false);

    if (exceeded_depth)
    {
        return core::Result<JsonParseOutcome>::failure(
            validation_error(std::string(field), "JSON nesting exceeds the limit of 64"));
    }
    if (document.is_discarded())
    {
        return core::Result<JsonParseOutcome>::failure(
            validation_error(std::string(field), "invalid JSON syntax"));
    }
    if (!document.is_object())
    {
        return core::Result<JsonParseOutcome>::failure(
            validation_error(std::string(field), "root must be an object"));
    }
    return core::Result<JsonParseOutcome>::success({std::move(document), std::move(issues)});
}

void parse_format(const Json& object, CreationSettingsPatch& output,
                  std::vector<core::FieldIssue>& issues, const std::string& prefix)
{
    const auto value = object.find("format");
    if (value == object.end())
    {
        return;
    }
    if (!value->is_string())
    {
        issues.push_back({prefix + ".format", "must be a string"});
        return;
    }
    const auto format = value->get<std::string>();
    if (format == "v1")
    {
        output.format = core::TorrentFormat::V1;
    }
    else if (format == "v2")
    {
        output.format = core::TorrentFormat::V2;
    }
    else if (format == "hybrid")
    {
        output.format = core::TorrentFormat::Hybrid;
    }
    else
    {
        issues.push_back({prefix + ".format", "must be v1, v2, or hybrid"});
    }
}

void parse_file_order(const Json& object, CreationSettingsPatch& output,
                      std::vector<core::FieldIssue>& issues, const std::string& prefix)
{
    const auto value = object.find("file_order");
    if (value == object.end())
    {
        return;
    }
    if (!value->is_string())
    {
        issues.push_back({prefix + ".file_order", "must be a string"});
        return;
    }
    const auto order = value->get<std::string>();
    if (order == "lexicographical")
    {
        output.file_order = core::FileOrderPolicy::Lexicographical;
    }
    else if (order == "canonical_alignment")
    {
        output.file_order = core::FileOrderPolicy::CanonicalAlignment;
    }
    else if (order == "natural")
    {
        output.file_order = core::FileOrderPolicy::Natural;
    }
    else if (order == "breadth_first")
    {
        output.file_order = core::FileOrderPolicy::BreadthFirst;
    }
    else
    {
        issues.push_back(
            {prefix + ".file_order",
             "must be lexicographical, canonical_alignment, natural, or breadth_first"});
    }
}

void parse_piece_size(const Json& object, CreationSettingsPatch& output,
                      std::vector<core::FieldIssue>& issues, const std::string& prefix)
{
    const auto value = object.find("piece_size");
    if (value == object.end())
    {
        return;
    }
    if (value->is_string())
    {
        if (value->get<std::string>() == "auto")
        {
            output.piece_size = PieceSizeSetting{};
            return;
        }
        issues.push_back({prefix + ".piece_size", "must be auto or an integer in KiB"});
        return;
    }
    if (!value->is_number_integer() && !value->is_number_unsigned())
    {
        issues.push_back({prefix + ".piece_size", "must be auto or an integer in KiB"});
        return;
    }
    std::int64_t kib{};
    if (value->is_number_unsigned())
    {
        const auto unsigned_kib = value->get<std::uint64_t>();
        if (unsigned_kib > kMaximumPieceSizeKib)
        {
            issues.push_back(
                {prefix + ".piece_size", "must be a power of two between 16 and 16384 KiB"});
            return;
        }
        kib = static_cast<std::int64_t>(unsigned_kib);
    }
    else
    {
        kib = value->get<std::int64_t>();
    }
    if (kib == 0)
    {
        output.piece_size = PieceSizeSetting{};
        return;
    }
    if (kib < static_cast<std::int64_t>(kMinimumPieceSizeKib) ||
        kib > static_cast<std::int64_t>(kMaximumPieceSizeKib) || (kib & (kib - 1)) != 0)
    {
        issues.push_back(
            {prefix + ".piece_size", "must be a power of two between 16 and 16384 KiB"});
        return;
    }
    output.piece_size = PieceSizeSetting{static_cast<std::uint32_t>(kib)};
}

void parse_private(const Json& object, CreationSettingsPatch& output,
                   std::vector<core::FieldIssue>& issues, const std::string& prefix)
{
    const auto value = object.find("private");
    if (value == object.end())
    {
        return;
    }
    if (value->is_boolean())
    {
        output.is_private = value->get<bool>();
        return;
    }
    if (value->is_number_integer())
    {
        const auto integer = value->get<std::int64_t>();
        if (integer == 0 || integer == 1)
        {
            output.is_private = integer == 1;
            return;
        }
    }
    issues.push_back({prefix + ".private", "must be a boolean or 0 or 1"});
}

[[nodiscard]] std::optional<std::vector<std::string>>
parse_string_array(const Json& object, const char* key, std::vector<core::FieldIssue>& issues,
                   const std::string& prefix)
{
    const auto value = object.find(key);
    if (value == object.end())
    {
        return std::nullopt;
    }
    if (!value->is_array())
    {
        issues.push_back({prefix + "." + key, "must be an array"});
        return std::nullopt;
    }
    std::vector<std::string> values;
    values.reserve(value->size());
    for (std::size_t index = 0; index < value->size(); ++index)
    {
        const auto& item = (*value)[index];
        if (!item.is_string())
        {
            issues.push_back(
                {prefix + "." + key + "[" + std::to_string(index) + "]", "must be a string"});
            continue;
        }
        values.push_back(item.get<std::string>());
    }
    return values;
}

void parse_trackers(const Json& object, CreationSettingsPatch& output,
                    std::vector<core::FieldIssue>& issues, const std::string& prefix,
                    const bool canonical)
{
    auto tracker_list = parse_string_array(object, "tracker_list", issues, prefix);
    if (tracker_list)
    {
        output.tracker_tiers = tracker_list->empty()
                                   ? std::vector<std::vector<std::string>>{}
                                   : std::vector<std::vector<std::string>>{*tracker_list};
    }
    if (!canonical)
    {
        return;
    }

    const auto tiers = object.find("tracker_tiers");
    if (tiers == object.end())
    {
        return;
    }
    if (!tiers->is_array())
    {
        issues.push_back({prefix + ".tracker_tiers", "must be an array"});
        return;
    }
    std::vector<std::vector<std::string>> parsed_tiers;
    parsed_tiers.reserve(tiers->size());
    for (std::size_t tier_index = 0; tier_index < tiers->size(); ++tier_index)
    {
        const auto& tier = (*tiers)[tier_index];
        if (!tier.is_array())
        {
            issues.push_back({prefix + ".tracker_tiers[" + std::to_string(tier_index) + "]",
                              "must be an array"});
            continue;
        }
        std::vector<std::string> parsed_tier;
        for (std::size_t tracker_index = 0; tracker_index < tier.size(); ++tracker_index)
        {
            if (!tier[tracker_index].is_string())
            {
                issues.push_back({prefix + ".tracker_tiers[" + std::to_string(tier_index) + "][" +
                                      std::to_string(tracker_index) + "]",
                                  "must be a string"});
                continue;
            }
            parsed_tier.push_back(tier[tracker_index].get<std::string>());
        }
        parsed_tiers.push_back(std::move(parsed_tier));
    }
    output.tracker_tiers = std::move(parsed_tiers);
}

void parse_optional_text(const Json& object, const char* key, std::optional<std::string>& output,
                         std::vector<core::FieldIssue>& issues, const std::string& prefix)
{
    const auto value = object.find(key);
    if (value == object.end())
    {
        return;
    }
    if (!value->is_string())
    {
        issues.push_back({prefix + "." + key, "must be a string"});
        return;
    }
    output = value->get<std::string>();
}

void parse_verify_object(const Json& object, const std::string& prefix,
                         VerifyResourceSettings& output, std::vector<core::FieldIssue>& issues)
{
    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator)
    {
        const auto& key = iterator.key();
        const auto& value = iterator.value();
        if (key == "workers")
        {
            if (!value.is_number_integer() && !value.is_number_unsigned())
            {
                issues.push_back({prefix + ".workers", "must be a positive integer"});
                continue;
            }
            std::uint64_t workers{};
            if (value.is_number_unsigned())
            {
                workers = value.get<std::uint64_t>();
            }
            else
            {
                const auto integer = value.get<std::int64_t>();
                if (integer <= 0)
                {
                    issues.push_back({prefix + ".workers", "must be a positive integer"});
                    continue;
                }
                workers = static_cast<std::uint64_t>(integer);
            }
            if (workers == 0U || workers > (std::numeric_limits<std::uint32_t>::max)())
            {
                issues.push_back({prefix + ".workers", "must be a positive integer"});
                continue;
            }
            output.hashing_workers = static_cast<std::uint32_t>(workers);
        }
        else if (key == "memory")
        {
            const auto memory = parse_json_memory_size(value);
            if (!memory)
            {
                issues.push_back(
                    {prefix + ".memory",
                     "must be a positive byte count or a size with KiB/MiB/GiB suffix"});
                continue;
            }
            output.checking_memory_bytes = memory;
        }
        else
        {
            std::string field = prefix;
            field.push_back('.');
            field.append(key);
            issues.push_back({std::move(field), "unknown member"});
        }
    }
}

[[nodiscard]] CreationSettingsPatch parse_settings_object(const Json& object, const bool canonical,
                                                          const std::string& prefix,
                                                          std::vector<core::FieldIssue>& issues)
{
    CreationSettingsPatch output;
    if (canonical)
    {
        parse_format(object, output, issues, prefix);
        parse_file_order(object, output, issues, prefix);
    }
    parse_piece_size(object, output, issues, prefix);
    parse_private(object, output, issues, prefix);
    parse_trackers(object, output, issues, prefix, canonical);
    if (canonical)
    {
        output.web_seeds = parse_string_array(object, "web_seeds", issues, prefix);
        parse_optional_text(object, "source", output.info_source, issues, prefix);
    }
    parse_optional_text(object, "comment", output.comment, issues, prefix);
    parse_optional_text(object, "created_by", output.created_by, issues, prefix);
    return output;
}

void append_legacy_encoding_diagnostic(const Json& object,
                                       std::vector<SettingsDiagnostic>& diagnostics)
{
    if (object.find("encoding") != object.end())
    {
        diagnostics.push_back({SettingsDiagnosticCode::LegacyEncodingIgnored, "encoding"});
    }
}

[[nodiscard]] std::string core_issue_message(const core::Error& error)
{
    return error.issues.empty() ? error.message : error.issues.front().message;
}

} // namespace

std::optional<std::uint64_t> parse_memory_size(const std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    std::size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t'))
    {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
    {
        --end;
    }
    if (begin == end)
    {
        return std::nullopt;
    }
    std::size_t digits = begin;
    while (digits < end && value[digits] >= '0' && value[digits] <= '9')
    {
        ++digits;
    }
    if (digits == begin)
    {
        return std::nullopt;
    }
    std::uint64_t number{};
    const auto converted = std::from_chars(value.data() + begin, value.data() + digits, number);
    if (converted.ec != std::errc{} || number == 0U)
    {
        return std::nullopt;
    }
    std::size_t suffix_begin = digits;
    while (suffix_begin < end && (value[suffix_begin] == ' ' || value[suffix_begin] == '\t'))
    {
        ++suffix_begin;
    }
    std::uint64_t multiplier = 1U;
    if (suffix_begin < end)
    {
        std::string suffix;
        suffix.reserve(end - suffix_begin);
        for (std::size_t index = suffix_begin; index < end; ++index)
        {
            suffix.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(value[index]))));
        }
        if (suffix == "kib")
        {
            multiplier = 1024ULL;
        }
        else if (suffix == "mib")
        {
            multiplier = 1024ULL * 1024ULL;
        }
        else if (suffix == "gib")
        {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (number > (std::numeric_limits<std::uint64_t>::max)() / multiplier)
    {
        return std::nullopt;
    }
    return number * multiplier;
}

core::Result<ParsedConfig> parse_config_json(const std::string_view input)
{
    auto parsed = parse_json(input, "frontend.config");
    if (!parsed)
    {
        return core::Result<ParsedConfig>::failure(std::move(parsed).error());
    }
    auto outcome = std::move(parsed).value();
    auto issues = std::move(outcome.issues);
    ParsedConfig config;
    const auto schema = outcome.document.find("schema");
    if (schema == outcome.document.end())
    {
        config.legacy = true;
        config.defaults = parse_settings_object(outcome.document, false, "frontend.config", issues);
        append_legacy_encoding_diagnostic(outcome.document, config.diagnostics);
        if (outcome.document.find("verify") != outcome.document.end())
        {
            issues.push_back({"frontend.config.verify", "requires a canonical config"});
        }
        if (outcome.document.find("disk_io") != outcome.document.end())
        {
            issues.push_back({"frontend.config.disk_io", "requires a canonical config"});
        }
        if (outcome.document.find("memory_working_set_limit") != outcome.document.end())
        {
            issues.push_back(
                {"frontend.config.memory_working_set_limit", "requires a canonical config"});
        }
    }
    else
    {
        if (!schema->is_string())
        {
            issues.push_back({"frontend.config.schema", "must be a string"});
        }
        else if (schema->get<std::string>() != "torrentcraft.config/v1")
        {
            issues.push_back({"frontend.config.schema", "must equal torrentcraft.config/v1"});
        }

        const auto defaults = outcome.document.find("defaults");
        if (defaults != outcome.document.end())
        {
            if (!defaults->is_object())
            {
                issues.push_back({"frontend.config.defaults", "must be an object"});
            }
            else
            {
                config.defaults =
                    parse_settings_object(*defaults, true, "frontend.config.defaults", issues);
            }
        }

        const auto presets = outcome.document.find("presets");
        if (presets != outcome.document.end())
        {
            if (!presets->is_object())
            {
                issues.push_back({"frontend.config.presets", "must be an object"});
            }
            else
            {
                for (auto iterator = presets->cbegin(); iterator != presets->cend(); ++iterator)
                {
                    if (!iterator->is_object())
                    {
                        issues.push_back(
                            {"frontend.config.presets." + iterator.key(), "must be an object"});
                        continue;
                    }
                    config.presets.emplace(
                        iterator.key(),
                        parse_settings_object(*iterator, true,
                                              "frontend.config.presets." + iterator.key(), issues));
                }
            }
        }
        const auto gui = outcome.document.find("gui");
        if (gui != outcome.document.end() && !gui->is_object())
        {
            issues.push_back({"frontend.config.gui", "must be an object"});
        }

        const auto verify = outcome.document.find("verify");
        if (verify != outcome.document.end())
        {
            if (!verify->is_object())
            {
                issues.push_back({"frontend.config.verify", "must be an object"});
            }
            else
            {
                VerifyResourceSettings parsed_verify;
                parse_verify_object(*verify, "frontend.config.verify", parsed_verify, issues);
                config.verify = parsed_verify;
            }
        }

        const auto disk_io = outcome.document.find("disk_io");
        if (disk_io != outcome.document.end())
        {
            if (!disk_io->is_string())
            {
                issues.push_back({"frontend.config.disk_io", "must be a string"});
            }
            else
            {
                const auto mode = disk_io->get<std::string>();
                if (mode == "posix")
                {
                    config.disk_io = core::DiskIoMode::Posix;
                }
                else if (mode == "mmap")
                {
                    config.disk_io = core::DiskIoMode::Mmap;
                }
                else
                {
                    issues.push_back({"frontend.config.disk_io", "must be posix or mmap"});
                }
            }
        }

        const auto working_set_limit = outcome.document.find("memory_working_set_limit");
        if (working_set_limit != outcome.document.end())
        {
            const auto limit_bytes = parse_json_memory_size(*working_set_limit);
            if (!limit_bytes)
            {
                issues.push_back(
                    {"frontend.config.memory_working_set_limit",
                     "must be a positive byte count or a size with KiB/MiB/GiB suffix"});
            }
            else
            {
                config.memory_working_set_limit_bytes = limit_bytes;
            }
        }
    }

    if (!issues.empty())
    {
        return core::Result<ParsedConfig>::failure(validation_errors(std::move(issues)));
    }
    return core::Result<ParsedConfig>::success(std::move(config));
}

core::Result<ParsedPreset> parse_preset_json(const std::string_view input)
{
    auto parsed = parse_json(input, "frontend.preset");
    if (!parsed)
    {
        return core::Result<ParsedPreset>::failure(std::move(parsed).error());
    }
    auto outcome = std::move(parsed).value();
    auto issues = std::move(outcome.issues);
    if (outcome.document.find("schema") != outcome.document.end())
    {
        issues.push_back({"frontend.preset.schema", "is not allowed in a legacy preset file"});
    }
    ParsedPreset preset;
    preset.settings = parse_settings_object(outcome.document, false, "frontend.preset", issues);
    append_legacy_encoding_diagnostic(outcome.document, preset.diagnostics);
    if (!issues.empty())
    {
        return core::Result<ParsedPreset>::failure(validation_errors(std::move(issues)));
    }
    return core::Result<ParsedPreset>::success(std::move(preset));
}

core::Result<VerifyResourceSettings> parse_verify_settings_json(const std::string_view input)
{
    auto parsed = parse_json(input, "frontend.verify");
    if (!parsed)
    {
        return core::Result<VerifyResourceSettings>::failure(std::move(parsed).error());
    }
    auto outcome = std::move(parsed).value();
    auto issues = std::move(outcome.issues);
    VerifyResourceSettings settings;
    parse_verify_object(outcome.document, "frontend.verify", settings, issues);
    if (!issues.empty())
    {
        return core::Result<VerifyResourceSettings>::failure(validation_errors(std::move(issues)));
    }
    return core::Result<VerifyResourceSettings>::success(settings);
}

VerifyResourceSettings overlay_verify_settings(VerifyResourceSettings lower,
                                               const VerifyResourceSettings& higher)
{
    if (higher.hashing_workers)
    {
        lower.hashing_workers = higher.hashing_workers;
    }
    if (higher.checking_memory_bytes)
    {
        lower.checking_memory_bytes = higher.checking_memory_bytes;
    }
    return lower;
}

core::Result<std::optional<core::VerificationResourceBudget>>
resolve_verify_resource_budget(const VerifyResourceSettings& settings)
{
    core::VerificationResourceBudgetInput input;
    input.hashing_workers = settings.hashing_workers.value_or(default_verify_hashing_workers);
    input.checking_memory_bytes =
        settings.checking_memory_bytes.value_or(default_verify_checking_memory_bytes);
    input.max_logical_files = (std::numeric_limits<std::uint64_t>::max)();
    input.max_pieces = (std::numeric_limits<std::uint64_t>::max)();
    auto budget = core::VerificationResourceBudget::create(input);
    if (!budget)
    {
        return core::Result<std::optional<core::VerificationResourceBudget>>::failure(
            std::move(budget).error());
    }
    return core::Result<std::optional<core::VerificationResourceBudget>>::success(
        std::optional<core::VerificationResourceBudget>(std::move(budget).value()));
}

CreationSettingsPatch overlay_settings(CreationSettingsPatch lower,
                                       const CreationSettingsPatch& higher)
{
    const auto overlay = [](auto& target, const auto& source) {
        if (source)
        {
            target = source;
        }
    };
    overlay(lower.format, higher.format);
    overlay(lower.file_order, higher.file_order);
    overlay(lower.piece_size, higher.piece_size);
    overlay(lower.is_private, higher.is_private);
    overlay(lower.tracker_tiers, higher.tracker_tiers);
    overlay(lower.web_seeds, higher.web_seeds);
    overlay(lower.comment, higher.comment);
    overlay(lower.created_by, higher.created_by);
    overlay(lower.info_source, higher.info_source);
    return lower;
}

core::Result<ResolvedCreationSettings> resolve_settings(const CreationSettingsPatch& settings)
{
    core::CreateOptionsInput input;
    if (settings.format)
    {
        input.format = *settings.format;
    }
    if (settings.file_order)
    {
        input.file_order_policy = *settings.file_order;
    }
    if (settings.piece_size && settings.piece_size->fixed_kib)
    {
        input.piece_length_strategy = core::PieceLengthStrategy::Fixed;
        input.fixed_piece_length = *settings.piece_size->fixed_kib * 1024U;
    }
    if (settings.is_private)
    {
        input.is_private = *settings.is_private;
    }

    std::vector<core::FieldIssue> issues;
    if (settings.tracker_tiers)
    {
        for (std::size_t tier_index = 0; tier_index < settings.tracker_tiers->size(); ++tier_index)
        {
            std::vector<core::TrackerUrl> trackers;
            const auto& tier = (*settings.tracker_tiers)[tier_index];
            for (std::size_t tracker_index = 0; tracker_index < tier.size(); ++tracker_index)
            {
                auto tracker = core::TrackerUrl::parse(tier[tracker_index]);
                if (!tracker)
                {
                    issues.push_back({"frontend.settings.tracker_tiers[" +
                                          std::to_string(tier_index) + "][" +
                                          std::to_string(tracker_index) + "]",
                                      core_issue_message(tracker.error())});
                    continue;
                }
                trackers.push_back(std::move(tracker).value());
            }
            if (trackers.size() != tier.size())
            {
                continue;
            }
            auto parsed_tier = core::TrackerTier::create(std::move(trackers));
            if (!parsed_tier)
            {
                issues.push_back(
                    {"frontend.settings.tracker_tiers[" + std::to_string(tier_index) + "]",
                     core_issue_message(parsed_tier.error())});
                continue;
            }
            input.tracker_tiers.push_back(std::move(parsed_tier).value());
        }
    }
    if (settings.web_seeds)
    {
        for (std::size_t index = 0; index < settings.web_seeds->size(); ++index)
        {
            auto seed = core::WebSeedUrl::parse((*settings.web_seeds)[index]);
            if (!seed)
            {
                issues.push_back({"frontend.settings.web_seeds[" + std::to_string(index) + "]",
                                  core_issue_message(seed.error())});
                continue;
            }
            input.web_seeds.push_back(std::move(seed).value());
        }
    }
    if (!issues.empty())
    {
        return core::Result<ResolvedCreationSettings>::failure(
            validation_errors(std::move(issues)));
    }

    auto options = core::CreateOptions::create(std::move(input));
    if (!options)
    {
        return core::Result<ResolvedCreationSettings>::failure(std::move(options).error());
    }
    core::CreationMetadataInput metadata;
    metadata.comment = settings.comment;
    metadata.created_by = settings.created_by.value_or("TorrentCraft");
    core::CreateInfoInput info;
    info.source = settings.info_source;
    return core::Result<ResolvedCreationSettings>::success(
        {std::move(options).value(), std::move(metadata), std::move(info), std::nullopt});
}

core::Result<void> apply_memory_working_set_limit_bytes(const std::uint64_t limit_bytes)
{
    if (limit_bytes == 0U)
    {
        return core::Result<void>::failure(validation_error(
            "frontend.config.memory_working_set_limit", "must be a positive byte count"));
    }

#ifdef _WIN32
    if (limit_bytes > static_cast<std::uint64_t>((std::numeric_limits<SIZE_T>::max)()))
    {
        return core::Result<void>::failure(
            {core::ErrorCode::ResourceLimitExceeded,
             "physical memory working-set limit cannot be represented by Windows",
             {{"frontend.config.memory_working_set_limit", "value is too large"}}});
    }

    constexpr SIZE_T mib = SIZE_T{1024U} * 1024U;
    const auto maximum = static_cast<SIZE_T>(limit_bytes);
    const auto minimum = (std::min)(SIZE_T{64U} * mib, maximum / 2U);
    if (!::SetProcessWorkingSetSizeEx(::GetCurrentProcess(), minimum, maximum,
                                      QUOTA_LIMITS_HARDWS_MAX_ENABLE))
    {
        const auto system_error =
            std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return core::Result<void>::failure(
            {core::ErrorCode::IoFailure,
             "could not set physical memory working-set limit: " + system_error.message(),
             {{"frontend.config.memory_working_set_limit",
               std::to_string(limit_bytes) + " bytes"}}});
    }
#else
    static_cast<void>(limit_bytes);
#endif

    return core::Result<void>::success();
}

} // namespace torrentutils::frontend
