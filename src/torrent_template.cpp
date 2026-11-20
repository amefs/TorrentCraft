#include <algorithm>
#include <string>
#include <torrentutils/core/application.hpp>
#include <torrentutils/frontend/torrent_template.hpp>
#include <utility>

namespace torrentutils::frontend {
namespace {

constexpr std::uint64_t kBytesPerKib = 1024U;

struct FieldToken
{
    std::string_view name;
    TemplateField field;
    bool default_selected;
};

constexpr FieldToken kFieldTokens[] = {
    {"trackers", TemplateField::Trackers, true},  {"piece-size", TemplateField::PieceSize, true},
    {"private", TemplateField::Private, true},    {"source", TemplateField::Source, true},
    {"web-seeds", TemplateField::WebSeeds, true}, {"format", TemplateField::Format, true},
    {"comment", TemplateField::Comment, false},   {"created-by", TemplateField::CreatedBy, false},
};

[[nodiscard]] const FieldToken& token_for(const TemplateField field) noexcept
{
    for (const auto& token : kFieldTokens)
    {
        if (token.field == field)
        {
            return token;
        }
    }
    return kFieldTokens[0];
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool has_field(const core::TorrentDocument& document, const std::string_view key,
                             const core::MetadataFieldScope scope)
{
    const auto& fields = document.metadata_fields();
    return std::any_of(fields.begin(), fields.end(),
                       [key, scope](const core::MetadataFieldInfo& field) {
                           return field.key == key && field.scope == scope;
                       });
}

[[nodiscard]] std::size_t count_document_warnings(const core::TorrentDocument& document,
                                                  const std::string_view field)
{
    const auto& warnings = document.warnings();
    return static_cast<std::size_t>(std::count_if(
        warnings.begin(), warnings.end(),
        [field](const core::DocumentWarning& warning) { return warning.field == field; }));
}

[[nodiscard]] std::vector<std::string> non_standard_keys(const core::TorrentDocument& document)
{
    std::vector<std::string> keys;
    for (const auto& field : document.metadata_fields())
    {
        const bool template_scope = field.scope == core::MetadataFieldScope::TopLevel ||
                                    field.scope == core::MetadataFieldScope::Info;
        if (template_scope && field.category != core::MetadataFieldCategory::Standard)
        {
            keys.push_back(field.scope == core::MetadataFieldScope::Info ? "info." + field.key
                                                                         : field.key);
        }
    }
    return keys;
}

void record_dropped_entries(TorrentTemplate& result, const TemplateField field,
                            const std::size_t count)
{
    if (count > 0U)
    {
        TemplateWarning warning;
        warning.kind = TemplateWarning::Kind::InvalidEntriesDropped;
        warning.field = field;
        warning.count = count;
        result.warnings.push_back(std::move(warning));
    }
}

/** Marks a text field as present-but-unreadable or absent. */
[[nodiscard]] TemplateFieldUnavailability missing_text(const bool key_present) noexcept
{
    return key_present ? TemplateFieldUnavailability::Unreadable
                       : TemplateFieldUnavailability::NotPresent;
}

void clear_field(CreationSettingsPatch& patch, const TemplateField field)
{
    switch (field)
    {
    case TemplateField::Trackers:
        patch.tracker_tiers.reset();
        break;
    case TemplateField::PieceSize:
        patch.piece_size.reset();
        break;
    case TemplateField::Private:
        patch.is_private.reset();
        break;
    case TemplateField::Source:
        patch.info_source.reset();
        break;
    case TemplateField::WebSeeds:
        patch.web_seeds.reset();
        break;
    case TemplateField::Format:
        patch.format.reset();
        break;
    case TemplateField::Comment:
        patch.comment.reset();
        break;
    case TemplateField::CreatedBy:
        patch.created_by.reset();
        break;
    }
}

} // namespace

const TemplateFieldCandidate& TorrentTemplate::candidate(const TemplateField field) const
{
    const auto found =
        std::find_if(fields.begin(), fields.end(),
                     [field](const TemplateFieldCandidate& entry) { return entry.field == field; });
    return found == fields.end() ? fields.front() : *found;
}

const std::vector<TemplateField>& all_template_fields()
{
    static const std::vector<TemplateField> fields = [] {
        std::vector<TemplateField> result;
        for (const auto& token : kFieldTokens)
        {
            result.push_back(token.field);
        }
        return result;
    }();
    return fields;
}

bool is_default_template_field(const TemplateField field) noexcept
{
    return token_for(field).default_selected;
}

std::vector<TemplateField> default_template_fields()
{
    std::vector<TemplateField> result;
    for (const auto& token : kFieldTokens)
    {
        if (token.default_selected)
        {
            result.push_back(token.field);
        }
    }
    return result;
}

std::string_view template_field_name(const TemplateField field) noexcept
{
    return token_for(field).name;
}

core::Result<std::vector<TemplateField>> parse_template_field_list(const std::string_view input)
{
    using ResultType = core::Result<std::vector<TemplateField>>;
    const auto whole = trim(input);
    if (whole == "all")
    {
        return ResultType::success(all_template_fields());
    }
    std::vector<TemplateField> result;
    std::vector<core::FieldIssue> issues;
    std::string_view rest = input;
    while (true)
    {
        const auto comma = rest.find(',');
        const auto item = trim(rest.substr(0, comma));
        const auto found =
            std::find_if(std::begin(kFieldTokens), std::end(kFieldTokens),
                         [item](const FieldToken& token) { return token.name == item; });
        if (found == std::end(kFieldTokens))
        {
            issues.push_back({"fields", item.empty()
                                            ? std::string("field name must not be empty")
                                            : "unknown template field: " + std::string(item)});
        }
        else if (std::find(result.begin(), result.end(), found->field) == result.end())
        {
            result.push_back(found->field);
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        rest.remove_prefix(comma + 1U);
    }
    if (!issues.empty())
    {
        return ResultType::failure(
            {core::ErrorCode::ValidationFailed, "invalid template field list", std::move(issues)});
    }
    return ResultType::success(std::move(result));
}

std::string template_field_unavailable_reason(const TemplateFieldCandidate& field)
{
    switch (field.unavailability)
    {
    case TemplateFieldUnavailability::None:
        return {};
    case TemplateFieldUnavailability::NotPresent:
        return "not present in the torrent";
    case TemplateFieldUnavailability::Unreadable:
        return "present but unreadable in the torrent";
    case TemplateFieldUnavailability::UnsupportedPieceSize:
        return "piece size of " + std::to_string(field.rejected_piece_length.value_or(0U)) +
               " bytes is not supported; fixed piece sizes must be a power of two from 16 KiB to "
               "16 MiB";
    }
    return {};
}

std::string template_warning_text(const TemplateWarning& warning)
{
    switch (warning.kind)
    {
    case TemplateWarning::Kind::NonStandardFields: {
        std::string text = "ignored non-standard metadata: ";
        for (std::size_t index = 0; index < warning.keys.size(); ++index)
        {
            if (index > 0U)
            {
                text += ", ";
            }
            text += warning.keys[index];
        }
        return text;
    }
    case TemplateWarning::Kind::InvalidEntriesDropped:
        return std::string(template_field_name(warning.field.value_or(TemplateField::Trackers))) +
               ": ignored " + std::to_string(warning.count) + " invalid " +
               (warning.count == 1U ? "entry" : "entries");
    }
    return {};
}

TorrentTemplate read_torrent_template(const core::TorrentDocument& document)
{
    TorrentTemplate result;
    if (auto keys = non_standard_keys(document); !keys.empty())
    {
        TemplateWarning warning;
        warning.kind = TemplateWarning::Kind::NonStandardFields;
        warning.keys = std::move(keys);
        result.warnings.push_back(std::move(warning));
    }
    auto& patch = result.adoptable;
    const auto& info = document.info();
    const auto& metadata = document.metadata();

    for (const auto field : all_template_fields())
    {
        TemplateFieldCandidate candidate;
        candidate.field = field;
        candidate.default_selected = is_default_template_field(field);
        switch (field)
        {
        case TemplateField::Trackers: {
            std::vector<std::vector<std::string>> tiers;
            for (const auto& tier : document.trackers().tiers())
            {
                std::vector<std::string> urls;
                for (const auto& tracker : tier.trackers())
                {
                    urls.push_back(tracker.value());
                }
                if (!urls.empty())
                {
                    tiers.push_back(std::move(urls));
                }
            }
            const auto dropped = count_document_warnings(document, "tracker");
            if (tiers.empty())
            {
                candidate.unavailability = dropped > 0U ? TemplateFieldUnavailability::Unreadable
                                                        : TemplateFieldUnavailability::NotPresent;
            }
            else
            {
                patch.tracker_tiers = std::move(tiers);
                record_dropped_entries(result, field, dropped);
            }
            break;
        }
        case TemplateField::PieceSize: {
            const auto bytes = info.pieces().piece_length();
            if (bytes % kBytesPerKib != 0U ||
                !is_supported_fixed_piece_size_kib(bytes / kBytesPerKib))
            {
                candidate.unavailability = TemplateFieldUnavailability::UnsupportedPieceSize;
                candidate.rejected_piece_length = bytes;
            }
            else
            {
                patch.piece_size =
                    PieceSizeSetting{static_cast<std::uint32_t>(bytes / kBytesPerKib)};
            }
            break;
        }
        case TemplateField::Private:
            // An absent private key means public, so this field is always adoptable.
            patch.is_private = info.is_private();
            break;
        case TemplateField::Source: {
            // Only info["source"] is adopted; the top-level compatibility field never is.
            const bool present = has_field(document, "source", core::MetadataFieldScope::Info);
            if (present && metadata.source().has_value())
            {
                patch.info_source = *metadata.source();
            }
            else
            {
                candidate.unavailability = missing_text(present);
            }
            break;
        }
        case TemplateField::WebSeeds: {
            std::vector<std::string> seeds;
            for (const auto& seed : metadata.web_seeds())
            {
                seeds.push_back(seed.value());
            }
            const auto dropped = count_document_warnings(document, "metadata.web_seed");
            if (seeds.empty())
            {
                candidate.unavailability = dropped > 0U ? TemplateFieldUnavailability::Unreadable
                                                        : TemplateFieldUnavailability::NotPresent;
            }
            else
            {
                patch.web_seeds = std::move(seeds);
                record_dropped_entries(result, field, dropped);
            }
            break;
        }
        case TemplateField::Format:
            patch.format = info.format();
            break;
        case TemplateField::Comment:
            if (metadata.comment().has_value())
            {
                patch.comment = *metadata.comment();
            }
            else
            {
                candidate.unavailability = missing_text(
                    has_field(document, "comment", core::MetadataFieldScope::TopLevel));
            }
            break;
        case TemplateField::CreatedBy:
            if (metadata.creator().has_value())
            {
                patch.created_by = *metadata.creator();
            }
            else
            {
                candidate.unavailability = missing_text(
                    has_field(document, "created by", core::MetadataFieldScope::TopLevel));
            }
            break;
        }
        result.fields.push_back(candidate);
    }
    return result;
}

core::Result<TorrentTemplate> load_torrent_template(const std::filesystem::path& path,
                                                    const core::TaskContext& context)
{
    core::FileTorrentRepository repository;
    core::SystemClock clock;
    const core::TorrentService service(repository, clock);
    auto loaded = service.load(path, core::LoadOptions{core::LoadMode::Lenient}, context);
    if (!loaded)
    {
        return core::Result<TorrentTemplate>::failure(std::move(loaded).error());
    }
    return core::Result<TorrentTemplate>::success(read_torrent_template(loaded.value().document()));
}

core::Result<TemplateSelection> select_template_fields(const TorrentTemplate& source,
                                                       const std::vector<TemplateField>& fields)
{
    TemplateSelection selection;
    selection.settings = source.adoptable;
    bool adopted_any = false;
    for (const auto field : all_template_fields())
    {
        const bool selected = std::find(fields.begin(), fields.end(), field) != fields.end();
        const auto& candidate = source.candidate(field);
        if (selected && candidate.available())
        {
            adopted_any = true;
            continue;
        }
        if (selected)
        {
            selection.skipped.push_back(candidate);
        }
        clear_field(selection.settings, field);
    }
    if (!adopted_any)
    {
        return core::Result<TemplateSelection>::failure(
            {core::ErrorCode::ValidationFailed,
             "no selected template field is available in the torrent",
             {{"fields", fields.empty() ? std::string("no template field was selected")
                                        : std::string("every selected field is unavailable")}}});
    }
    return core::Result<TemplateSelection>::success(std::move(selection));
}

} // namespace torrentutils::frontend
