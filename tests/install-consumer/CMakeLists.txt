cmake_minimum_required(VERSION 3.25)
project(TorrentUtilsCoreInstallConsumer LANGUAGES CXX)

enable_testing()

find_package(TorrentUtilsCore 1.0.0 EXACT CONFIG REQUIRED)

add_executable(torrentutils_foundation_install_consumer foundation_main.cpp)
target_link_libraries(
    torrentutils_foundation_install_consumer
    PRIVATE TorrentUtils::Foundation
)

add_executable(torrentutils_domain_install_consumer domain_main.cpp)
target_link_libraries(
    torrentutils_domain_install_consumer
    PRIVATE TorrentUtils::Domain
)

add_executable(torrentutils_core_install_consumer main.cpp)
target_link_libraries(
    torrentutils_core_install_consumer
    PRIVATE TorrentUtils::Core
)

add_executable(torrentutils_frontend_install_consumer frontend_main.cpp)
target_link_libraries(
    torrentutils_frontend_install_consumer
    PRIVATE TorrentUtils::Frontend
)

add_test(
    NAME TorrentUtils.Foundation.InstallConsumer.Run
    COMMAND torrentutils_foundation_install_consumer
)
add_test(
    NAME TorrentUtils.Domain.InstallConsumer.Run
    COMMAND torrentutils_domain_install_consumer
)
add_test(
    NAME TorrentUtils.Core.InstallConsumer.Run
    COMMAND torrentutils_core_install_consumer
)
add_test(
    NAME TorrentUtils.Frontend.InstallConsumer.Run
    COMMAND torrentutils_frontend_install_consumer
)
