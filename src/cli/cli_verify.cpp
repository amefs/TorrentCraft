#include "cli_common.hpp"
#include "interrupt.hpp"
#include "progress.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

constexpr std::string_view kVerifyHelp = R"(Usage:
  torrentcraft verify <torrent> <content> [options]

Verify content against a torrent file and report per-file results.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --verify-workers N        Limit hashing worker threads (default 1)
  --verify-memory SIZE      Limit checking memory in bytes or KiB/MiB/GiB
                            (default 32 MiB)
  --memory-working-set-limit SIZE
                            Limit the Windows process working set (default 512 MiB)
  --progress json|plain|tty Emit progress (json/plain/tty)
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

// Human verify output switches to a summary when the logical file count
// exceeds this value, so large torrents do not flood the terminal.
constexpr std::size_t kVerifyFileSummaryThreshold = 50U;

} // namespace

[[nodiscard]] std::string
capability_code_name(const torrentutils::core::VerificationCapabilityDiagnosticCode code)
{
    switch (code)
    {
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedTorrentFormat:
        return "unsupported_torrent_format";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme:
        return "unsupported_piece_hash_scheme";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedFileLayout:
        return "unsupported_file_layout";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedFileAttribute:
        return "unsupported_file_attribute";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedSymlinkSemantics:
        return "unsupported_symlink_semantics";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::BackendFeatureUnavailable:
        return "backend_feature_unavailable";
    }
    return "backend_feature_unavailable";
}

namespace {

[[nodiscard]] std::string finding_name(const torrentutils::core::FileVerificationFinding finding)
{
    switch (finding)
    {
    case torrentutils::core::FileVerificationFinding::Missing:
        return "missing";
    case torrentutils::core::FileVerificationFinding::NotRegularFile:
        return "not_regular_file";
    case torrentutils::core::FileVerificationFinding::LengthMismatch:
        return "length_mismatch";
    case torrentutils::core::FileVerificationFinding::HashMismatch:
        return "hash_mismatch";
    case torrentutils::core::FileVerificationFinding::SharedPieceMismatch:
        return "shared_piece_mismatch";
    case torrentutils::core::FileVerificationFinding::SymlinkMissing:
        return "symlink_missing";
    case torrentutils::core::FileVerificationFinding::SymlinkTargetMismatch:
        return "symlink_target_mismatch";
    case torrentutils::core::FileVerificationFinding::None:
        return "none";
    }
    return "none";
}

} // namespace

[[nodiscard]] int
run_verify_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory)
{
    if (help_requested(args))
    {
        output << kVerifyHelp;
        return 0;
    }
    bool json = false;
    bool quiet = false;
    std::optional<ProgressMode> progress_mode;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::uint32_t> verify_workers;
    std::optional<std::uint64_t> verify_memory;
    std::optional<std::uint64_t> memory_working_set_limit_bytes;
    std::vector<std::string> positional;
    const auto parse_value_option = [&](const std::string& option,
                                        const std::string& value) -> bool {
        if (option == "--config")
        {
            if (config_path)
            {
                diagnostics << "error: --config was specified more than once\n";
                return false;
            }
            config_path = path_from_utf8(value);
            return true;
        }
        if (option == "--verify-workers")
        {
            const auto parsed = parse_unsigned(value);
            if (!parsed || *parsed == 0U || *parsed > (std::numeric_limits<std::uint32_t>::max)())
            {
                diagnostics << "error: --verify-workers must be a positive integer\n";
                return false;
            }
            if (verify_workers)
            {
                diagnostics << "error: --verify-workers was specified more than once\n";
                return false;
            }
            verify_workers = static_cast<std::uint32_t>(*parsed);
            return true;
        }
        if (option == "--verify-memory")
        {
            const auto memory = torrentutils::frontend::parse_memory_size(value);
            if (!memory)
            {
                diagnostics << "error: --verify-memory must be a positive byte count or a "
                               "size with KiB/MiB/GiB suffix\n";
                return false;
            }
            if (verify_memory)
            {
                diagnostics << "error: --verify-memory was specified more than once\n";
                return false;
            }
            verify_memory = memory;
            return true;
        }
        if (option == "--memory-working-set-limit")
        {
            const auto parsed = torrentutils::frontend::parse_memory_size(value);
            if (!parsed)
            {
                diagnostics
                    << "error: --memory-working-set-limit must be a positive byte count or a "
                       "size with KiB/MiB/GiB suffix\n";
                return false;
            }
            if (memory_working_set_limit_bytes)
            {
                diagnostics << "error: --memory-working-set-limit was specified more than once\n";
                return false;
            }
            memory_working_set_limit_bytes = parsed;
            return true;
        }
        diagnostics << "error: unknown option: " << option << '\n';
        return false;
    };
    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--progress")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: --progress requires a value\n";
                return 2;
            }
            ProgressMode mode;
            std::string parse_error;
            if (!parse_progress_mode(args[++index], mode, parse_error))
            {
                diagnostics << "error: " << parse_error << '\n';
                return 2;
            }
            if (progress_mode)
            {
                diagnostics << "error: --progress was specified more than once\n";
                return 2;
            }
            progress_mode = mode;
        }
        else if (argument.rfind("--progress=", 0) == 0)
        {
            ProgressMode mode;
            std::string parse_error;
            if (!parse_progress_mode(argument.substr(11), mode, parse_error))
            {
                diagnostics << "error: " << parse_error << '\n';
                return 2;
            }
            if (progress_mode)
            {
                diagnostics << "error: --progress was specified more than once\n";
                return 2;
            }
            progress_mode = mode;
        }
        else if (argument == "--config" || argument == "--verify-workers" ||
                 argument == "--verify-memory" || argument == "--memory-working-set-limit")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: " << argument << " requires a value\n";
                return 2;
            }
            if (!parse_value_option(argument, args[++index]))
            {
                return 2;
            }
        }
        else if (argument.rfind("--config=", 0) == 0)
        {
            if (!parse_value_option("--config",
                                    argument.substr(std::string_view("--config=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--verify-workers=", 0) == 0)
        {
            if (!parse_value_option("--verify-workers",
                                    argument.substr(std::string_view("--verify-workers=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--verify-memory=", 0) == 0)
        {
            if (!parse_value_option("--verify-memory",
                                    argument.substr(std::string_view("--verify-memory=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--memory-working-set-limit=", 0) == 0)
        {
            if (!parse_value_option(
                    "--memory-working-set-limit",
                    argument.substr(std::string_view("--memory-working-set-limit=").size())))
            {
                return 2;
            }
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 2U)
    {
        diagnostics << "error: verify requires a torrent path and a content path\n";
        return 2;
    }

    std::optional<ConfigFile> config;
    {
        std::error_code error;
        const auto working_directory = std::filesystem::current_path(error);
        if (error)
        {
            print_error({ErrorCode::IoFailure, "could not determine current directory", {}}, json,
                        diagnostics);
            return 8;
        }
        const auto paths =
            torrentutils::frontend::default_config_search_paths(config_path, working_directory);
        auto discovered = torrentutils::frontend::discover_config(paths, executable_directory);
        if (!discovered)
        {
            print_error(discovered.error(), json, diagnostics);
            return error_exit_code(discovered.error().code);
        }
        if (discovered.value())
        {
            auto loaded = ConfigFile::load(*discovered.value());
            if (!loaded)
            {
                print_error(loaded.error(), json, diagnostics);
                return error_exit_code(loaded.error().code);
            }
            config.emplace(std::move(loaded).value());
        }
    }

    const auto configured_working_set_limit =
        config && config->parsed().memory_working_set_limit_bytes
            ? *config->parsed().memory_working_set_limit_bytes
            : torrentutils::frontend::default_memory_working_set_limit_bytes;
    const auto effective_working_set_limit =
        memory_working_set_limit_bytes.value_or(configured_working_set_limit);
    auto working_set_applied =
        torrentutils::frontend::apply_memory_working_set_limit_bytes(effective_working_set_limit);
    if (!working_set_applied)
    {
        diagnostics << "warning: ";
        write_human_text(diagnostics, working_set_applied.error().message);
        diagnostics << "\n";
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }

    torrentutils::core::VerifyRequest request(loaded.value().document(),
                                              path_from_utf8(positional[1]));
    if (config)
    {
        request.disk_io_mode = config->parsed().disk_io;
    }
    VerifyResourceSettings verify_settings;
    if (config && config->parsed().verify)
    {
        verify_settings = *config->parsed().verify;
    }
    verify_settings = torrentutils::frontend::overlay_verify_settings(
        verify_settings, VerifyResourceSettings{verify_workers, verify_memory});
    auto budget = torrentutils::frontend::resolve_verify_resource_budget(verify_settings);
    if (!budget)
    {
        print_error(budget.error(), json, diagnostics);
        return error_exit_code(budget.error().code);
    }
    request.resource_budget = std::move(budget).value();
    const auto stderr_is_tty = diagnostics_is_tty(diagnostics);
    const auto effective_progress =
        progress_mode.value_or(stderr_is_tty ? ProgressMode::Tty : ProgressMode::None);
    if (!json && !quiet)
    {
        print_cancel_hint(diagnostics);
    }
    const InterruptScope interrupt;
    ProgressWriter writer(effective_progress, stderr_is_tty, quiet, diagnostics);
    const auto& info = loaded.value().document().info();
    std::uint64_t total_bytes = 0;
    for (const auto& file : info.files())
    {
        total_bytes += file.length();
    }
    writer.verify_start(static_cast<std::uint32_t>(info.pieces().piece_length()), total_bytes,
                        info.files().size());
    request.on_progress = [&writer](const torrentutils::core::VerificationProgress& progress) {
        writer.verify_event(progress);
    };

    torrentutils::core::TaskContext context;
    context.cancellation = interrupt.token();
    auto report = service.verify(request, context);
    if (!report)
    {
        writer.abandon();
        print_error(report.error(), json, diagnostics);
        return error_exit_code(report.error().code);
    }
    writer.finish();

    Json files = Json::array();
    for (const auto& file : report.value().files)
    {
        Json findings = Json::array();
        constexpr std::array<torrentutils::core::FileVerificationFinding, 7U> all_findings = {
            torrentutils::core::FileVerificationFinding::Missing,
            torrentutils::core::FileVerificationFinding::NotRegularFile,
            torrentutils::core::FileVerificationFinding::LengthMismatch,
            torrentutils::core::FileVerificationFinding::HashMismatch,
            torrentutils::core::FileVerificationFinding::SharedPieceMismatch,
            torrentutils::core::FileVerificationFinding::SymlinkMissing,
            torrentutils::core::FileVerificationFinding::SymlinkTargetMismatch};
        for (const auto finding : all_findings)
        {
            if (torrentutils::core::has_finding(file.findings, finding))
            {
                findings.push_back(finding_name(finding));
            }
        }
        files.push_back({{"path", file.path.to_string()},
                         {"expected_bytes", file.expected_bytes},
                         {"hashed_bytes", file.hashed_bytes},
                         {"verified_bytes", file.verified_bytes},
                         {"mismatched_bytes", file.mismatched_bytes},
                         {"findings", std::move(findings)}});
    }

    const auto& outcome = report.value().outcome;
    std::string outcome_name = "verified";
    int exit_code = 0;
    if (outcome == torrentutils::core::VerificationOutcome::Mismatched)
    {
        outcome_name = "mismatched";
        exit_code = 6;
    }
    else if (outcome == torrentutils::core::VerificationOutcome::Incomplete)
    {
        outcome_name = "incomplete";
        exit_code = 9;
    }

    if (json)
    {
        output << dump_json(Json{{"ok", true},
                                 {"data",
                                  {{"outcome", outcome_name},
                                   {"expected_bytes", report.value().expected_bytes},
                                   {"hashed_bytes", report.value().hashed_bytes},
                                   {"verified_bytes", report.value().verified_bytes},
                                   {"mismatched_bytes", report.value().mismatched_bytes},
                                   {"files", std::move(files)}}}})
               << '\n';
    }
    else if (!quiet)
    {
        output << "verify: " << outcome_name << '\n';
        output << "expected_bytes: " << report.value().expected_bytes << '\n';
        output << "hashed_bytes: " << report.value().hashed_bytes << '\n';
        output << "verified_bytes: " << report.value().verified_bytes << '\n';
        output << "mismatched_bytes: " << report.value().mismatched_bytes << '\n';
        const auto& file_results = report.value().files;
        if (file_results.size() > kVerifyFileSummaryThreshold)
        {
            std::size_t failed_count = 0;
            for (const auto& file : file_results)
            {
                if (file.findings != torrentutils::core::FileVerificationFinding::None)
                {
                    ++failed_count;
                }
            }
            output << "files: " << file_results.size() << " total, " << failed_count << " failed\n";
            for (const auto& file : file_results)
            {
                if (file.findings == torrentutils::core::FileVerificationFinding::None)
                {
                    continue;
                }
                output << "file: ";
                write_human_text(output, file.path.to_string());
                output << " mismatched\n";
            }
        }
        else
        {
            for (const auto& file : file_results)
            {
                output << "file: ";
                write_human_text(output, file.path.to_string());
                if (file.findings == torrentutils::core::FileVerificationFinding::None)
                {
                    output << " ok\n";
                }
                else
                {
                    output << " mismatched\n";
                }
            }
        }
    }
    return exit_code;
}

} // namespace torrentcraft::cli::detail
