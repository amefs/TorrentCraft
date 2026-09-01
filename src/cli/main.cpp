#include "cli.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

[[nodiscard]] std::string wide_to_utf8(const wchar_t* value)
{
    if (value == nullptr)
    {
        return {};
    }
    const auto required =
        ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr) ==
        0)
    {
        return {};
    }
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

int wmain(int argc, wchar_t* wargv[])
try
{
    torrentcraft::cli::initialize_console_output();

    // Take the wide command line so characters outside the ANSI code page are
    // preserved, then feed the CLI UTF-8 arguments.
    std::vector<std::string> arguments;
    std::vector<const char*> argv;
    arguments.reserve(static_cast<std::size_t>(argc));
    argv.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
    {
        arguments.push_back(wide_to_utf8(wargv[index]));
        argv.push_back(arguments.back().c_str());
    }
    return torrentcraft::cli::run(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
}
catch (const std::exception& error)
{
    std::cerr << "error: internal exception: " << error.what() << '\n';
    return 10;
}
catch (...)
{
    std::cerr << "error: unknown internal exception\n";
    return 10;
}
#else
int main(int argc, const char* const argv[])
try
{
    return torrentcraft::cli::run(argc, argv, std::cout, std::cerr);
}
catch (const std::exception& error)
{
    std::cerr << "error: internal exception: " << error.what() << '\n';
    return 10;
}
catch (...)
{
    std::cerr << "error: unknown internal exception\n";
    return 10;
}
#endif
