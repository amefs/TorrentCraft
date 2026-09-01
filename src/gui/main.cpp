#include "GuiLogController.hpp"
#include "Logo.hpp"
#include "MainWindow.hpp"
#include "cli.hpp"

#include <QApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QtGlobal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

void initialize_torrentcraft_resources()
{
    Q_INIT_RESOURCE(TorrentCraft);
}

namespace {
#ifdef _WIN32
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
        return {};
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

void enable_virtual_terminal_output(const DWORD standard_handle)
{
    const auto handle = ::GetStdHandle(standard_handle);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
    {
        return;
    }
    DWORD mode{};
    if (::GetConsoleMode(handle, &mode) != 0)
    {
        static_cast<void>(::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
    }
}

void prepare_cli_console()
{
    if (::GetConsoleOutputCP() == 0)
    {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            ::AllocConsole();
        }
    }
    if (::GetConsoleOutputCP() != 0)
    {
        FILE* stream = nullptr;
        ::freopen_s(&stream, "CONOUT$", "w", stdout);
        ::freopen_s(&stream, "CONOUT$", "w", stderr);
        enable_virtual_terminal_output(STD_OUTPUT_HANDLE);
        enable_virtual_terminal_output(STD_ERROR_HANDLE);
        // Keep this path aligned with the pure CLI: it does not change stream
        // synchronization after startup. Reopening stdout/stderr and clearing
        // their state is sufficient after AttachConsole.
        std::cout.clear();
        std::cerr.clear();
    }
}

void finish_cli_console()
{
    std::cout.flush();
    std::cerr.flush();
    static_cast<void>(::_flushall());
}
#endif

GuiLogController* active_gui_logger{};
QtMessageHandler previous_qt_message_handler{};

void configure_static_linux_qt_platform_theme()
{
#if defined(TORRENTCRAFT_STATIC_LINUX_QT)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
    {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }
#endif
}

void qt_message_handler(const QtMsgType type, const QMessageLogContext& context,
                        const QString& message)
{
    static thread_local bool handling_message{};
    if (active_gui_logger == nullptr || handling_message)
    {
        return;
    }
    handling_message = true;
    const auto level = type == QtDebugMsg     ? torrentutils::core::LogLevel::Debug
                       : type == QtInfoMsg    ? torrentutils::core::LogLevel::Info
                       : type == QtWarningMsg ? torrentutils::core::LogLevel::Warning
                                              : torrentutils::core::LogLevel::Error;
    GuiLogController::Fields fields;
    if (context.category != nullptr && *context.category != '\0')
    {
        fields.emplace_back("category", context.category);
    }
    fields.emplace_back("message", message.toUtf8().toStdString());
    active_gui_logger->log_event(level, "qt", "message", "emit", fields);
    handling_message = false;
}

class QtLogHandlerGuard final
{
  public:
    explicit QtLogHandlerGuard(GuiLogController& logger) noexcept
    {
        active_gui_logger = &logger;
        previous_qt_message_handler = qInstallMessageHandler(qt_message_handler);
    }

    ~QtLogHandlerGuard()
    {
        qInstallMessageHandler(previous_qt_message_handler);
        previous_qt_message_handler = nullptr;
        active_gui_logger = nullptr;
    }
};

int run_gui()
{
    int qt_argc = 1;
    char app_name[] = "torrentcraft-gui";
    char* qt_argv[] = {app_name, nullptr};
    initialize_torrentcraft_resources();
    configure_static_linux_qt_platform_theme();
    QApplication application(qt_argc, qt_argv);
#ifndef Q_OS_WIN
    QApplication::setStyle(QStringLiteral("Fusion"));
#endif
    application.setOrganizationDomain("torrentcraft.org");
    application.setOrganizationName("TorrentCraft");
    application.setApplicationName("TorrentCraft");
    // Qt desktop plugins append applicationDisplayName to native window titles. The main window
    // owns the complete title so the display name must remain empty here.
    application.setApplicationDisplayName(QString());
    application.setWindowIcon(torrentcraft::gui::application_icon());
    GuiLogController logger;
    QtLogHandlerGuard log_handler(logger);
    int result{};
    {
        MainWindow window(&logger);
        window.show();
        if (qEnvironmentVariableIsSet("TORRENTCRAFT_GUI_SMOKE"))
        {
            QTimer::singleShot(100, &application, [&application] { application.quit(); });
        }
        result = application.exec();
    }
    logger.close();
    return result;
}
} // namespace

#ifdef _WIN32
int main(int, char*[])
try
{
    int wide_argc = 0;
    auto* wide_argv = ::CommandLineToArgvW(::GetCommandLineW(), &wide_argc);
    if (wide_argv == nullptr)
    {
        std::cerr << "error: could not read the Windows command line\n";
        return 10;
    }
    if (wide_argc <= 1)
    {
        ::LocalFree(wide_argv);
        return run_gui();
    }

    prepare_cli_console();
    torrentcraft::cli::initialize_console_output();

    std::vector<std::string> arguments;
    std::vector<const char*> argv;
    arguments.reserve(static_cast<std::size_t>(wide_argc));
    argv.reserve(static_cast<std::size_t>(wide_argc));
    for (int index = 0; index < wide_argc; ++index)
    {
        arguments.push_back(wide_to_utf8(wide_argv[index]));
        argv.push_back(arguments.back().c_str());
    }

    const auto result =
        torrentcraft::cli::run(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
    finish_cli_console();
    ::LocalFree(wide_argv);
    return result;
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

int main(int argc, char* argv[])
try
{
    if (argc > 1)
    {
        return torrentcraft::cli::run(argc, argv, std::cout, std::cerr);
    }
    return run_gui();
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
