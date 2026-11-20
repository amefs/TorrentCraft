#pragma once

/// @file
/// @brief Torrent template extraction: adopt reusable creation settings from an existing torrent.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/task.hpp>
#include <torrentutils/core/torrent_document.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <vector>

namespace torrentutils::frontend {

/** One creation setting that may be adopted from a torrent template. */
enum class TemplateField
{
    Trackers,
    PieceSize,
    Private,
    Source,
    WebSeeds,
    Format,
    Comment,
    CreatedBy,
};

/** Why a template field cannot be adopted from a specific torrent. */
enum class TemplateFieldUnavailability
{
    None,
    NotPresent,
    /** Present but unusable: invalid UTF-8 text, or a list whose entries are all invalid. */
    Unreadable,
    UnsupportedPieceSize,
};

/** Adoption state of one template field for a specific torrent template. */
struct TemplateFieldCandidate
{
    TemplateField field{TemplateField::Trackers};
    bool default_selected{};
    TemplateFieldUnavailability unavailability{TemplateFieldUnavailability::None};
    /** Torrent piece length in bytes when unavailability is UnsupportedPieceSize. */
    std::optional<std::uint64_t> rejected_piece_length;

    [[nodiscard]] bool available() const noexcept
    {
        return unavailability == TemplateFieldUnavailability::None;
    }
};

/** A non-fatal problem found while reading a torrent template. */
struct TemplateWarning
{
    enum class Kind
    {
        /** Unknown or extension keys at top level or in the info dictionary were ignored. */
        NonStandardFields,
        /** Some invalid entries of a list template field were dropped; the rest is adoptable. */
        InvalidEntriesDropped,
    };

    Kind kind{Kind::NonStandardFields};
    /** The affected template field for InvalidEntriesDropped. */
    std::optional<TemplateField> field;
    /** Raw keys for NonStandardFields, in document order. */
    std::vector<std::string> keys;
    /** Number of dropped entries for InvalidEntriesDropped. */
    std::size_t count{};
};

/**
 * Reusable creation settings read from a torrent.
 *
 * Torrent-specific metadata (name, files, piece hashes, info-hashes, creation date, DHT nodes,
 * collections, and the top-level source compatibility field) is never represented here.
 */
struct TorrentTemplate
{
    /** One candidate per template field, in all_template_fields() order. */
    std::vector<TemplateFieldCandidate> fields;
    /** Values for every available template field; unavailable fields stay empty. */
    CreationSettingsPatch adoptable;
    /** Non-standard metadata that was ignored; torrent-specific data problems are omitted. */
    std::vector<TemplateWarning> warnings;

    [[nodiscard]] const TemplateFieldCandidate& candidate(TemplateField field) const;
};

struct TemplateSelection
{
    /** A preset patch containing only the selected, available fields. */
    CreationSettingsPatch settings;
    /** Selected fields that were skipped because the torrent cannot supply them. */
    std::vector<TemplateFieldCandidate> skipped;
};

[[nodiscard]] const std::vector<TemplateField>& all_template_fields();
[[nodiscard]] bool is_default_template_field(TemplateField field) noexcept;
[[nodiscard]] std::vector<TemplateField> default_template_fields();

/** Stable kebab-case field token shared by the CLI and GUI (for example `piece-size`). */
[[nodiscard]] std::string_view template_field_name(TemplateField field) noexcept;

/** Parses a comma-separated field token list or `all`; duplicates are collapsed. */
[[nodiscard]] core::Result<std::vector<TemplateField>>
parse_template_field_list(std::string_view input);

/** English reason for an unavailable candidate, for CLI diagnostics. */
[[nodiscard]] std::string template_field_unavailable_reason(const TemplateFieldCandidate& field);

/** English text for a template warning, for CLI diagnostics. */
[[nodiscard]] std::string template_warning_text(const TemplateWarning& warning);

[[nodiscard]] TorrentTemplate read_torrent_template(const core::TorrentDocument& document);

/**
 * Leniently loads a torrent file and reads its template fields.
 *
 * Non-standard metadata is ignored and reported as warnings; only torrents that cannot be loaded
 * at all (malformed bencode, invalid info dictionary or pieces, unsafe paths, limits) fail.
 */
[[nodiscard]] core::Result<TorrentTemplate>
load_torrent_template(const std::filesystem::path& path, const core::TaskContext& context = {});

/**
 * Builds a preset patch from the selected fields. Unavailable selected fields are skipped and
 * reported; selection fails only when no selected field is available.
 */
[[nodiscard]] core::Result<TemplateSelection>
select_template_fields(const TorrentTemplate& source, const std::vector<TemplateField>& fields);

} // namespace torrentutils::frontend
