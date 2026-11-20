#include "cli.hpp"

#include "cli_common.hpp"
#include "interrupt.hpp"
#include "progress.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/version.hpp>
#include <vector>

namespace torrentcraft::cli {
namespace detail {
namespace {

[[nodiscard]] std::optional<std::filesystem::path>
executable_directory_from_argv0(const char* const value)
{
    if (value == nullptr || *value == '\0')
    {
        return std::nullopt;
    }
    const auto path = path_from_utf8(value);
    if (!path.has_parent_path())
    {
        return std::nullopt;
    }
    std::error_code error;
    const auto absolute_path = std::filesystem::absolute(path, error);
    if (error)
    {
        return std::nullopt;
    }
    return absolute_path.parent_path();
}

constexpr std::string_view kUsage = R"(Usage:
  torrentcraft <command> [options]
  torrentcraft <path...>       Infer a command from the provided paths
  torrentcraft --help
  torrentcraft --version

Commands:
  create      Create a torrent from a file or directory
  inspect     Show a summary of a torrent file
  tree        Print the torrent's file tree
  verify      Verify content against a torrent file
  validate    Validate a torrent file
  tracker     Manage tracker tiers
  metadata    Show or edit top-level metadata
  config      Manage the canonical torrentcraft.json config
  preset      Manage named creation presets
  completion  Print shell completion scripts

Run 'torrentcraft <command> --help' for command-specific options.
)";

constexpr std::string_view kCompletionHelp = R"(Usage:
  torrentcraft completion bash|zsh|fish

Print shell completion scripts.

Options:
  -h, --help                Show this help
)";

[[nodiscard]] bool is_torrent_path(const std::string& path)
{
    const auto extension = path_from_utf8(path).extension().u8string();
    std::string lowered = extension;
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == ".torrent";
}

[[nodiscard]] std::filesystem::path default_create_target(const std::string& content)
{
    return path_from_utf8(content + ".torrent");
}

[[nodiscard]] std::filesystem::file_type path_type(const std::string& path)
{
    std::error_code error;
    const auto status = std::filesystem::status(path_from_utf8(path), error);
    if (error)
    {
        return std::filesystem::file_type::none;
    }
    return status.type();
}

[[nodiscard]] bool inferred_option_takes_value(const std::string_view argument) noexcept
{
    constexpr std::array<std::string_view, 22> options{
        "-o",
        "--output",
        "--memory-working-set-limit",
        "--progress",
        "--config",
        "--preset",
        "--preset-file",
        "--format",
        "--piece-size",
        "--file-order",
        "--filter-mode",
        "--exclude",
        "--tracker",
        "--tier",
        "--web-seed",
        "--comment",
        "--created-by",
        "--source",
        "--creation-date",
        "--verify-workers",
        "--verify-memory",
        "--depth",
    };
    return std::find(options.begin(), options.end(), argument) != options.end();
}

struct InferredArguments
{
    std::vector<std::string> paths;
    std::vector<std::string> options;
    bool output_specified{};
};

[[nodiscard]] InferredArguments collect_inferred_arguments(const int argc, const char* const argv[])
{
    InferredArguments result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        result.options.push_back(argument);
        if (argument == "-o" || argument == "--output")
        {
            result.output_specified = true;
        }
        if (inferred_option_takes_value(argument) && index + 1 < argc)
        {
            result.options.emplace_back(argv[++index]);
        }
        else if (argument.empty() || argument.front() == '-')
        {
            continue;
        }
        else
        {
            result.paths.push_back(argument);
            result.options.pop_back();
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string> inferred_command_arguments(
    const InferredArguments& inferred, const std::vector<std::string>& positional,
    const std::optional<std::filesystem::path>& default_target = std::nullopt)
{
    auto result = positional;
    if (default_target && !inferred.output_specified)
    {
        result.emplace_back("-o");
        result.emplace_back(default_target->u8string());
    }
    result.insert(result.end(), inferred.options.begin(), inferred.options.end());
    return result;
}

[[nodiscard]] int
run_inferred_command(const int argc, const char* const argv[], std::ostream& output,
                     std::ostream& diagnostics,
                     const std::optional<std::filesystem::path>& executable_directory)
{
    const auto inferred = collect_inferred_arguments(argc, argv);
    const auto& paths = inferred.paths;

    if (paths.empty())
    {
        if (help_requested(inferred.options))
        {
            output << kUsage;
            return 0;
        }
        for (const auto& argument : inferred.options)
        {
            if (!argument.empty() && argument.front() == '-')
            {
                diagnostics << "error: unknown option: " << argument << '\n';
                return 2;
            }
        }
    }

    if (paths.size() == 1U)
    {
        if (is_torrent_path(paths[0]))
        {
            const auto args = inferred_command_arguments(inferred, {paths[0]});
            return run_inspect_command(args, output, diagnostics);
        }
        const auto args =
            inferred_command_arguments(inferred, {paths[0]}, default_create_target(paths[0]));
        return run_create_command(args, output, diagnostics, executable_directory);
    }

    if (paths.size() == 2U)
    {
        const bool first_torrent = is_torrent_path(paths[0]);
        const bool second_torrent = is_torrent_path(paths[1]);
        if (first_torrent != second_torrent)
        {
            const std::string& torrent = first_torrent ? paths[0] : paths[1];
            const std::string& content = first_torrent ? paths[1] : paths[0];
            const auto args = inferred_command_arguments(inferred, {torrent, content});
            return run_verify_command(args, output, diagnostics, executable_directory);
        }
        if (!first_torrent && !second_torrent)
        {
            const auto first_type = path_type(paths[0]);
            const auto second_type = path_type(paths[1]);
            if (first_type == std::filesystem::file_type::directory &&
                second_type == std::filesystem::file_type::regular)
            {
                const auto target = path_from_utf8(paths[0]) /
                                    (path_from_utf8(paths[1]).stem().u8string() + ".torrent");
                const auto args = inferred_command_arguments(inferred, {paths[1]}, target);
                return run_create_command(args, output, diagnostics, executable_directory);
            }
            if (second_type == std::filesystem::file_type::directory &&
                first_type == std::filesystem::file_type::regular)
            {
                const auto target = path_from_utf8(paths[1]) /
                                    (path_from_utf8(paths[0]).stem().u8string() + ".torrent");
                const auto args = inferred_command_arguments(inferred, {paths[0]}, target);
                return run_create_command(args, output, diagnostics, executable_directory);
            }
        }
    }

    diagnostics << "error: could not infer a command from the provided paths\n";
    return 2;
}

[[nodiscard]] int run_completion_command(const std::vector<std::string>& args, std::ostream& output,
                                         std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kCompletionHelp;
        return 0;
    }
    if (args.size() != 1U)
    {
        diagnostics << "error: completion requires exactly one shell name\n";
        return 2;
    }
    const auto& shell = args[0];
    constexpr std::string_view commands =
        "create config preset inspect tree verify validate tracker metadata completion";
    constexpr std::string_view global_options = "--help --version --json --quiet";
    if (shell == "bash")
    {
        output << "complete -W '" << commands << " " << global_options << "' torrentcraft\n";
        return 0;
    }
    if (shell == "zsh")
    {
        output << "#compdef torrentcraft\n_arguments '1:command:(" << commands << ")'\n";
        return 0;
    }
    if (shell == "fish")
    {
        output << "complete -c torrentcraft -f -a '" << commands << "'\n";
        return 0;
    }
    diagnostics << "error: unknown shell: " << shell << '\n';
    return 2;
}

} // namespace
} // namespace detail

int run(const int argc, const char* const argv[], std::ostream& output, std::ostream& diagnostics)
{
    const auto executable_directory =
        detail::executable_directory_from_argv0(argc > 0 ? argv[0] : nullptr);
    if (argc == 1)
    {
        output << detail::kUsage;
        return 0;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || argv[1] == std::string_view{"-h"}))
    {
        output << detail::kUsage;
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        output << torrentutils::core::version() << '\n';
        return 0;
    }
    const std::string_view command = argv[1];
    if (command == "inspect")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_inspect_command(args, output, diagnostics);
    }
    if (command == "tree")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_tree_command(args, output, diagnostics);
    }
    if (command == "verify")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_verify_command(args, output, diagnostics, executable_directory);
    }
    if (command == "validate")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_validate_command(args, output, diagnostics);
    }
    if (command == "tracker")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_tracker_command(args, output, diagnostics);
    }
    if (command == "metadata")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_metadata_command(args, output, diagnostics);
    }
    if (command == "completion")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_completion_command(args, output, diagnostics);
    }
    if (command == "config")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_config_command(args, output, diagnostics, executable_directory);
    }
    if (command == "preset")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_preset_command(args, output, diagnostics, executable_directory);
    }
    if (command == "create")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return detail::run_create_command(args, output, diagnostics, executable_directory);
    }
    return detail::run_inferred_command(argc, argv, output, diagnostics, executable_directory);
}

void set_console_utf8_native(const bool value) noexcept
{
    console_utf8_native() = value;
}

void set_console_output_cp(const unsigned int value) noexcept
{
    console_output_cp() = value;
}

void set_console_shared_with_shell(const bool value) noexcept
{
    detail::set_console_shared_with_shell(value);
}

void initialize_console_output() noexcept
{
#ifdef _WIN32
    const auto native_console_cp = ::GetConsoleOutputCP();
    if (native_console_cp == 0U)
    {
        set_console_utf8_native(false);
        set_console_output_cp(CP_UTF8);
        return;
    }

    // Keep progress bar glyph selection based on the code page that the host
    // reported, but make the actual console byte stream UTF-8. This preserves
    // characters that cannot be represented by CP936/CP437, such as Japanese
    // punctuation and kana.
    set_console_utf8_native(native_console_cp == CP_UTF8);
    if (::SetConsoleOutputCP(CP_UTF8) != 0)
    {
        set_console_output_cp(CP_UTF8);
    }
    else
    {
        set_console_output_cp(native_console_cp);
    }
    ::SetConsoleCP(CP_UTF8);
#endif
}

} // namespace torrentcraft::cli
