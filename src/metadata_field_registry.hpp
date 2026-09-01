#pragma once

#include <array>
#include <string_view>
#include <torrentutils/core/metadata.hpp>

namespace torrentutils::core::detail {

/** Format-aware bencode dictionary location used for registry lookups. */
enum class FieldDictionary
{
    TopLevelV1,
    TopLevelV2,
    InfoV1,
    InfoV2,
    InfoHybrid,
    InfoV1File,
    InfoV2FileTreeLeaf
};

/** Edit policy for one field location; this does not change read classification. */
enum class InfoPatchability
{
    NotInfo,
    StandardIdentity,
    ExplicitExtensionIdentity,
    RetainedReadOnly,
    RebuildRequired,
};

/** One known metadata field in the TorrentCraft field registry. */
struct RegistryFieldSpec
{
    std::string_view key;
    FieldDictionary dictionary;
    std::string_view source;
    std::string_view type;
    bool modeled{};
};

/**
 * The standard metadata field database. Every entry names the BEP or convention
 * that defines the key, the human-readable bencode value type, and whether the
 * Domain fully processes the field (modeled). Entries with modeled == false are
 * classified as Extension; the `x-*` convention covers remaining extensions.
 */
inline constexpr std::array<RegistryFieldSpec, 74> kMetadataFieldRegistry{{
    // Top level (V1)
    {"announce", FieldDictionary::TopLevelV1, "BEP 3", "string", true},
    {"announce-list", FieldDictionary::TopLevelV1, "BEP 12", "list[list[string]]", true},
    {"collections", FieldDictionary::TopLevelV1, "BEP 38", "list[string]", true},
    {"comment", FieldDictionary::TopLevelV1, "BEP 3", "string", true},
    {"created by", FieldDictionary::TopLevelV1, "BEP 3", "string", true},
    {"creation date", FieldDictionary::TopLevelV1, "BEP 3", "integer", true},
    {"httpseeds", FieldDictionary::TopLevelV1, "BEP 17", "string|list[string]", true},
    {"info", FieldDictionary::TopLevelV1, "BEP 3", "dictionary", true},
    {"nodes", FieldDictionary::TopLevelV1, "BEP 5", "list[[host,port]]", true},
    {"similar", FieldDictionary::TopLevelV1, "BEP 38", "list[bytes(20)]", false},
    {"source", FieldDictionary::TopLevelV1, "convention", "string", true},
    {"url-list", FieldDictionary::TopLevelV1, "BEP 19", "string|list[string]", true},
    // Top level (V2 / hybrid)
    {"announce", FieldDictionary::TopLevelV2, "BEP 3", "string", true},
    {"announce-list", FieldDictionary::TopLevelV2, "BEP 12", "list[list[string]]", true},
    {"collections", FieldDictionary::TopLevelV2, "BEP 38", "list[string]", true},
    {"comment", FieldDictionary::TopLevelV2, "BEP 3", "string", true},
    {"created by", FieldDictionary::TopLevelV2, "BEP 3", "string", true},
    {"creation date", FieldDictionary::TopLevelV2, "BEP 3", "integer", true},
    {"httpseeds", FieldDictionary::TopLevelV2, "BEP 17", "string|list[string]", true},
    {"info", FieldDictionary::TopLevelV2, "BEP 3", "dictionary", true},
    {"nodes", FieldDictionary::TopLevelV2, "BEP 5", "list[[host,port]]", true},
    {"piece layers", FieldDictionary::TopLevelV2, "BEP 52", "dictionary", true},
    {"similar", FieldDictionary::TopLevelV2, "BEP 38", "list[bytes(20)]", false},
    {"source", FieldDictionary::TopLevelV2, "convention", "string", true},
    {"url-list", FieldDictionary::TopLevelV2, "BEP 19", "string|list[string]", true},
    // Info dictionary (V1)
    {"attr", FieldDictionary::InfoV1, "BEP 47", "string", true},
    {"collections", FieldDictionary::InfoV1, "BEP 38", "list[string]", false},
    {"files", FieldDictionary::InfoV1, "BEP 3", "list[dictionary]", true},
    {"length", FieldDictionary::InfoV1, "BEP 3", "integer", true},
    {"name", FieldDictionary::InfoV1, "BEP 3", "string", true},
    {"originator", FieldDictionary::InfoV1, "BEP 39", "bytes(20)", false},
    {"piece length", FieldDictionary::InfoV1, "BEP 3", "integer", true},
    {"pieces", FieldDictionary::InfoV1, "BEP 3", "bytes", true},
    {"private", FieldDictionary::InfoV1, "BEP 27", "integer", true},
    {"sha1", FieldDictionary::InfoV1, "BEP 47", "bytes(20)", true},
    {"similar", FieldDictionary::InfoV1, "BEP 38", "list[bytes(20)]", false},
    {"source", FieldDictionary::InfoV1, "convention", "string", true},
    {"symlink path", FieldDictionary::InfoV1, "BEP 47", "list[string]", true},
    {"update-url", FieldDictionary::InfoV1, "BEP 39", "string", false},
    // Info dictionary (V2)
    {"collections", FieldDictionary::InfoV2, "BEP 38", "list[string]", false},
    {"file tree", FieldDictionary::InfoV2, "BEP 52", "dictionary", true},
    {"meta version", FieldDictionary::InfoV2, "BEP 52", "integer", true},
    {"name", FieldDictionary::InfoV2, "BEP 3", "string", true},
    {"originator", FieldDictionary::InfoV2, "BEP 39", "bytes(20)", false},
    {"piece length", FieldDictionary::InfoV2, "BEP 3", "integer", true},
    {"private", FieldDictionary::InfoV2, "BEP 27", "integer", true},
    {"similar", FieldDictionary::InfoV2, "BEP 38", "list[bytes(20)]", false},
    {"source", FieldDictionary::InfoV2, "convention", "string", true},
    {"update-url", FieldDictionary::InfoV2, "BEP 39", "string", false},
    // Info dictionary (hybrid)
    {"attr", FieldDictionary::InfoHybrid, "BEP 47", "string", true},
    {"collections", FieldDictionary::InfoHybrid, "BEP 38", "list[string]", false},
    {"file tree", FieldDictionary::InfoHybrid, "BEP 52", "dictionary", true},
    {"files", FieldDictionary::InfoHybrid, "BEP 3", "list[dictionary]", true},
    {"length", FieldDictionary::InfoHybrid, "BEP 3", "integer", true},
    {"meta version", FieldDictionary::InfoHybrid, "BEP 52", "integer", true},
    {"name", FieldDictionary::InfoHybrid, "BEP 3", "string", true},
    {"originator", FieldDictionary::InfoHybrid, "BEP 39", "bytes(20)", false},
    {"piece length", FieldDictionary::InfoHybrid, "BEP 3", "integer", true},
    {"pieces", FieldDictionary::InfoHybrid, "BEP 3", "bytes", true},
    {"private", FieldDictionary::InfoHybrid, "BEP 27", "integer", true},
    {"similar", FieldDictionary::InfoHybrid, "BEP 38", "list[bytes(20)]", false},
    {"source", FieldDictionary::InfoHybrid, "convention", "string", true},
    {"symlink path", FieldDictionary::InfoHybrid, "BEP 47", "list[string]", true},
    {"update-url", FieldDictionary::InfoHybrid, "BEP 39", "string", false},
    // V1 files[] entries
    {"attr", FieldDictionary::InfoV1File, "BEP 47", "string", true},
    {"length", FieldDictionary::InfoV1File, "BEP 3", "integer", true},
    {"path", FieldDictionary::InfoV1File, "BEP 3", "list[string]", true},
    {"sha1", FieldDictionary::InfoV1File, "BEP 47", "bytes(20)", true},
    {"symlink path", FieldDictionary::InfoV1File, "BEP 47", "list[string]", true},
    // V2 file tree leaves
    {"attr", FieldDictionary::InfoV2FileTreeLeaf, "BEP 47", "string", true},
    {"length", FieldDictionary::InfoV2FileTreeLeaf, "BEP 52", "integer", true},
    {"pieces root", FieldDictionary::InfoV2FileTreeLeaf, "BEP 52", "bytes(32)", true},
    {"sha1", FieldDictionary::InfoV2FileTreeLeaf, "BEP 47", "bytes(20)", true},
    {"symlink path", FieldDictionary::InfoV2FileTreeLeaf, "BEP 47", "list[string]", true},
}};

/** Returns the registry entry for (key, dictionary) or nullptr when absent. */
[[nodiscard]] inline const RegistryFieldSpec*
find_field_spec(const std::string_view key, const FieldDictionary dictionary) noexcept
{
    for (const auto& spec : kMetadataFieldRegistry)
    {
        if (spec.dictionary == dictionary && spec.key == key)
        {
            return &spec;
        }
    }
    return nullptr;
}

/** True when the key uses the de-facto `x-*` extension prefix. */
[[nodiscard]] inline bool has_extension_prefix(const std::string_view key) noexcept
{
    return key.size() >= 2U && key[0] == 'x' && key[1] == '-';
}

/** Returns the surgical-edit policy for a raw key at its bencode dictionary location. */
[[nodiscard]] inline InfoPatchability info_patchability(const std::string_view key,
                                                        const FieldDictionary dictionary) noexcept
{
    switch (dictionary)
    {
    case FieldDictionary::InfoV1:
    case FieldDictionary::InfoV2:
    case FieldDictionary::InfoHybrid:
        if (key == "private" || key == "name")
        {
            return InfoPatchability::StandardIdentity;
        }
        if (key == "source")
        {
            return InfoPatchability::ExplicitExtensionIdentity;
        }
        if (key == "name.utf-8")
        {
            return InfoPatchability::RetainedReadOnly;
        }
        return InfoPatchability::RebuildRequired;
    case FieldDictionary::TopLevelV1:
    case FieldDictionary::TopLevelV2:
        return key == "source" ? InfoPatchability::RetainedReadOnly : InfoPatchability::NotInfo;
    case FieldDictionary::InfoV1File:
    case FieldDictionary::InfoV2FileTreeLeaf:
        return InfoPatchability::RebuildRequired;
    }
    return InfoPatchability::NotInfo;
}

/** Classification outcome for one raw key at one dictionary location. */
struct ClassifiedField
{
    MetadataFieldCategory category{MetadataFieldCategory::Unknown};
    std::string_view source;
    std::string_view type;
    bool modeled{};
};

/**
 * Classifies a raw key: Standard when the registry entry is modeled, Extension
 * when the registry entry is unmodeled or the key uses `x-*`, Unknown otherwise.
 */
[[nodiscard]] inline ClassifiedField classify_field(const std::string_view key,
                                                    const FieldDictionary dictionary) noexcept
{
    if (const auto* spec = find_field_spec(key, dictionary); spec != nullptr)
    {
        return {spec->modeled ? MetadataFieldCategory::Standard : MetadataFieldCategory::Extension,
                spec->source, spec->type, spec->modeled};
    }
    if (has_extension_prefix(key))
    {
        return {MetadataFieldCategory::Extension, "x-* convention", "any", false};
    }
    return {MetadataFieldCategory::Unknown, "-", "any", false};
}

/** Coarse public scope for a format-aware dictionary location. */
[[nodiscard]] inline MetadataFieldScope field_scope(const FieldDictionary dictionary) noexcept
{
    switch (dictionary)
    {
    case FieldDictionary::TopLevelV1:
    case FieldDictionary::TopLevelV2:
        return MetadataFieldScope::TopLevel;
    case FieldDictionary::InfoV1:
    case FieldDictionary::InfoV2:
    case FieldDictionary::InfoHybrid:
        return MetadataFieldScope::Info;
    case FieldDictionary::InfoV1File:
        return MetadataFieldScope::InfoV1File;
    case FieldDictionary::InfoV2FileTreeLeaf:
        return MetadataFieldScope::InfoV2FileTreeLeaf;
    }
    return MetadataFieldScope::TopLevel;
}

/** True when the dictionary location is covered by the info hash. */
[[nodiscard]] inline bool info_hash_bearing(const FieldDictionary dictionary) noexcept
{
    switch (dictionary)
    {
    case FieldDictionary::TopLevelV1:
    case FieldDictionary::TopLevelV2:
        return false;
    case FieldDictionary::InfoV1:
    case FieldDictionary::InfoV2:
    case FieldDictionary::InfoHybrid:
    case FieldDictionary::InfoV1File:
    case FieldDictionary::InfoV2FileTreeLeaf:
        return true;
    }
    return false;
}

} // namespace torrentutils::core::detail
