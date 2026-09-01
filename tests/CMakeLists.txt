find_package(Catch2 3 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(
    torrentutils_core_tests
    domain_test.cpp
    foundation_test.cpp
    version_test.cpp
    metadata_engine_test.cpp
    torrent_engine_test.cpp
    tracker_engine_test.cpp
    application_service_test.cpp
    cli_progress_test.cpp
    config_store_test.cpp
    cli_test.cpp
    frontend_test.cpp
    verification_admission_controller_test_probe.cpp
)
target_link_libraries(
    torrentutils_core_tests
    PRIVATE
        TorrentUtils::Core
        TorrentUtils::Frontend
        torrentcraft_cli
        indicators::indicators
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
        Threads::Threads
)
target_compile_definitions(
    torrentutils_core_tests
    PRIVATE
        TORRENTUTILS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
    target_include_directories(torrentutils_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/cli")
torrentutils_set_project_warnings(torrentutils_core_tests)

add_executable(
    torrentutils_generate_file_order_policy_matrix
    fixtures/torrent-engine/generate_file_order_policy_matrix.cpp
)
target_link_libraries(torrentutils_generate_file_order_policy_matrix PRIVATE TorrentUtils::Core)
torrentutils_set_project_warnings(torrentutils_generate_file_order_policy_matrix)

include(Catch)
catch_discover_tests(
    torrentutils_core_tests
    TEST_PREFIX "TorrentUtils.Core."
    PROPERTIES LABELS "unit"
)

set(test_dependency_prefix_path "${CMAKE_PREFIX_PATH}")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND test_dependency_prefix_path
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
    )
endif()

set(test_dependency_runtime_path)
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND test_dependency_runtime_path
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
    )
endif()

set(test_dependency_prefix_arguments)
set(test_dependency_prefix_count 0)
foreach(test_dependency_prefix IN LISTS test_dependency_prefix_path)
    list(APPEND test_dependency_prefix_arguments
        "-DTEST_DEPENDENCY_PREFIX_PATH_${test_dependency_prefix_count}=${test_dependency_prefix}")
    math(EXPR test_dependency_prefix_count "${test_dependency_prefix_count} + 1")
endforeach()
list(APPEND test_dependency_prefix_arguments
    "-DTEST_DEPENDENCY_PREFIX_PATH_COUNT=${test_dependency_prefix_count}")

set(test_dependency_runtime_arguments)
set(test_dependency_runtime_count 0)
foreach(test_dependency_runtime IN LISTS test_dependency_runtime_path)
    string(CONCAT test_dependency_runtime_argument
        "-DTEST_DEPENDENCY_RUNTIME_PATH_${test_dependency_runtime_count}="
        "${test_dependency_runtime}")
    list(APPEND test_dependency_runtime_arguments
        "${test_dependency_runtime_argument}")
    math(EXPR test_dependency_runtime_count "${test_dependency_runtime_count} + 1")
endforeach()
list(APPEND test_dependency_runtime_arguments
    "-DTEST_DEPENDENCY_RUNTIME_PATH_COUNT=${test_dependency_runtime_count}")

set(test_msvc_runtime_arguments)
if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "")
    list(APPEND test_msvc_runtime_arguments
        "-DTEST_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

add_test(
    NAME given_installed_core_when_configuring_consumer_then_targets_link_and_run
    COMMAND
        "${CMAKE_COMMAND}"
        "-DCORE_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DCORE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DTEST_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
        "-DTEST_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        ${test_msvc_runtime_arguments}
        "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
        "-DTEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DTEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DTEST_CONFIG=$<CONFIG>"
        "-DTEST_ROOT=${PROJECT_BINARY_DIR}/install-consumer-test"
        ${test_dependency_prefix_arguments}
        ${test_dependency_runtime_arguments}
        -P "${PROJECT_SOURCE_DIR}/cmake/RunInstallConsumerTest.cmake"
)
set_tests_properties(
    given_installed_core_when_configuring_consumer_then_targets_link_and_run
    PROPERTIES LABELS "install-consumer"
)

add_test(
    NAME given_torrent_engine_public_header_when_scanned_then_private_dependencies_do_not_escape
    COMMAND
        "${CMAKE_COMMAND}"
        "-DHEADER=${PROJECT_SOURCE_DIR}/include/torrentutils/core/torrent_engine.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckTorrentEnginePublicHeader.cmake"
)
set_tests_properties(
    given_torrent_engine_public_header_when_scanned_then_private_dependencies_do_not_escape
    PROPERTIES LABELS "public-contract"
)

add_test(
    NAME given_application_public_header_when_scanned_then_private_dependencies_do_not_escape
    COMMAND
        "${CMAKE_COMMAND}"
        "-DHEADER=${PROJECT_SOURCE_DIR}/include/torrentutils/core/application.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckTorrentEnginePublicHeader.cmake"
)
set_tests_properties(
    given_application_public_header_when_scanned_then_private_dependencies_do_not_escape
    PROPERTIES LABELS "public-contract"
)

add_test(
    NAME given_frontend_public_header_when_scanned_then_private_dependencies_do_not_escape
    COMMAND
        "${CMAKE_COMMAND}"
        "-DHEADER=${PROJECT_SOURCE_DIR}/include/torrentutils/frontend/frontend.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckTorrentEnginePublicHeader.cmake"
)
set_tests_properties(
    given_frontend_public_header_when_scanned_then_private_dependencies_do_not_escape
    PROPERTIES LABELS "public-contract"
)
