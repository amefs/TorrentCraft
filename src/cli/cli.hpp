#pragma once

#include <iosfwd>

namespace torrentcraft::cli {

int run(int argc, const char* const argv[], std::ostream& output, std::ostream& diagnostics);

/** Windows startup hook: records whether the console was already UTF-8. */
void set_console_utf8_native(bool value) noexcept;

/** Windows startup hook: records the native console output code page. */
void set_console_output_cp(unsigned int value) noexcept;

/** Windows startup hook: configure the attached console for UTF-8 output. */
void initialize_console_output() noexcept;

/**
 * Windows GUI binary startup hook: the CLI writes to its parent shell's console, and that shell
 * does not wait for it, so long operations tell the user to cancel with Ctrl+Break.
 */
void set_console_shared_with_shell(bool value) noexcept;

} // namespace torrentcraft::cli
