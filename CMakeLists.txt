cmake_minimum_required(VERSION 3.25)

project(TorrentUtilsCore VERSION 1.0.0 LANGUAGES CXX)

if(WIN32)
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/TorrentCraftVersion.rc.in"
        "${PROJECT_BINARY_DIR}/generated/TorrentCraftVersion.rc"
        @ONLY)
endif()

option(
    TORRENTUTILS_BUILD_GUI
    "Build the TorrentCraft Qt Widgets GUI"
    OFF
)

set(TORRENTCRAFT_GIT_COMMIT "" CACHE STRING "Git commit shown in the TorrentCraft About dialog")
set(TORRENTCRAFT_GIT_BRANCH "" CACHE STRING "Git branch shown in the TorrentCraft About dialog")
set(TORRENTCRAFT_VCPKG_COMMIT "" CACHE STRING "vcpkg commit shown in the TorrentCraft About dialog")
set(TORRENTCRAFT_BUILD_DATE "" CACHE STRING "UTC build date shown in the About dialog")
mark_as_advanced(TORRENTCRAFT_GIT_COMMIT TORRENTCRAFT_GIT_BRANCH TORRENTCRAFT_VCPKG_COMMIT TORRENTCRAFT_BUILD_DATE)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/ProjectWarnings.cmake)

function(torrentcraft_read_git_metadata repository_path out_commit out_branch)
    set(_git_dir "${repository_path}/.git")
    set(${out_commit} "" PARENT_SCOPE)
    set(${out_branch} "" PARENT_SCOPE)
    if(EXISTS "${_git_dir}" AND NOT IS_DIRECTORY "${_git_dir}")
        file(STRINGS "${_git_dir}" _git_marker LIMIT_COUNT 1)
        if(_git_marker MATCHES "^gitdir: *(.+)$")
            set(_git_dir "${CMAKE_MATCH_1}")
            if(NOT IS_ABSOLUTE "${_git_dir}")
                get_filename_component(_git_dir "${repository_path}/${_git_dir}" ABSOLUTE)
            endif()
        endif()
    endif()
    if(NOT EXISTS "${_git_dir}/HEAD")
        return()
    endif()
    file(STRINGS "${_git_dir}/HEAD" _head LIMIT_COUNT 1)
    if(NOT _head)
        return()
    endif()
    list(GET _head 0 _head_line)
    set(_commit "")
    set(_branch "")
    if(_head_line MATCHES "^ref: (.+)$")
        set(_ref "${CMAKE_MATCH_1}")
        if(_ref MATCHES "^refs/heads/(.+)$")
            set(_branch "${CMAKE_MATCH_1}")
        endif()
        if(EXISTS "${_git_dir}/${_ref}")
            file(STRINGS "${_git_dir}/${_ref}" _ref_value LIMIT_COUNT 1)
            if(_ref_value)
                list(GET _ref_value 0 _commit)
            endif()
        endif()
        if(NOT _commit AND EXISTS "${_git_dir}/packed-refs")
            file(STRINGS "${_git_dir}/packed-refs" _packed_refs)
            foreach(_packed_ref IN LISTS _packed_refs)
                if(_packed_ref MATCHES "^([0-9A-Fa-f]+)[ ]+${_ref}$")
                    set(_commit "${CMAKE_MATCH_1}")
                    break()
                endif()
            endforeach()
        endif()
    elseif(_head_line MATCHES "^[0-9A-Fa-f]+$")
        set(_commit "${_head_line}")
    endif()
    if(_commit MATCHES "^[0-9A-Fa-f]+$")
        string(SUBSTRING "${_commit}" 0 7 _short_commit)
        set(${out_commit} "${_short_commit}" PARENT_SCOPE)
    endif()
    if(_branch)
        set(${out_branch} "${_branch}" PARENT_SCOPE)
    endif()
endfunction()

option(
    TORRENTUTILS_WARNINGS_AS_ERRORS
    "Treat TorrentUtils Core compiler warnings as errors"
    OFF
)

add_library(torrentutils_foundation STATIC src/cancellation.cpp)
add_library(TorrentUtils::Foundation ALIAS torrentutils_foundation)

set_target_properties(
    torrentutils_foundation
    PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME Foundation
        OUTPUT_NAME TorrentUtilsFoundation
)

target_compile_features(torrentutils_foundation PUBLIC cxx_std_17)
target_include_directories(
    torrentutils_foundation
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
torrentutils_set_project_warnings(torrentutils_foundation)

add_library(torrentutils_domain STATIC src/domain.cpp)
add_library(TorrentUtils::Domain ALIAS torrentutils_domain)

set_target_properties(
    torrentutils_domain
    PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME Domain
        OUTPUT_NAME TorrentUtilsDomain
)

target_compile_features(torrentutils_domain PUBLIC cxx_std_17)
target_include_directories(
    torrentutils_domain
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(torrentutils_domain PUBLIC TorrentUtils::Foundation)
torrentutils_set_project_warnings(torrentutils_domain)

add_library(
    torrentutils_core
    STATIC
        src/application.cpp
        src/application_torrent_engine.cpp
        src/bencode_adapter.cpp
        src/libtorrent_adapter.cpp
        src/metadata_engine.cpp
        src/torrent_engine.cpp
        src/torrent_engine_fault_injection.cpp
        src/tracker_engine.cpp
        src/verification_progress_publisher.cpp
        src/verification_admission_controller.cpp
        src/verification_resource_budget.cpp
        src/version.cpp
)
add_library(TorrentUtils::Core ALIAS torrentutils_core)

set_target_properties(
    torrentutils_core
    PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME Core
        OUTPUT_NAME TorrentUtilsCore
)

target_compile_features(torrentutils_core PUBLIC cxx_std_17)
target_include_directories(
    torrentutils_core
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
find_package(LibtorrentRasterbar CONFIG REQUIRED)
find_package(indicators CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(
    torrentutils_core
    PUBLIC
        TorrentUtils::Domain
        "$<BUILD_INTERFACE:LibtorrentRasterbar::torrent-rasterbar>"
        "$<INSTALL_INTERFACE:LibtorrentRasterbar::torrent-rasterbar>"
    PRIVATE
        "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>"
)
torrentutils_set_project_warnings(torrentutils_core)

add_library(
    torrentutils_frontend
    STATIC
        src/config_store.cpp
        src/frontend.cpp
)
add_library(TorrentUtils::Frontend ALIAS torrentutils_frontend)

set_target_properties(
    torrentutils_frontend
    PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME Frontend
        OUTPUT_NAME TorrentUtilsFrontend
)

target_compile_features(torrentutils_frontend PUBLIC cxx_std_17)
target_include_directories(
    torrentutils_frontend
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(
    torrentutils_frontend
    PUBLIC TorrentUtils::Core
    PRIVATE "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>"
)
torrentutils_set_project_warnings(torrentutils_frontend)

add_library(torrentcraft_cli STATIC src/cli/cli.cpp)
target_compile_features(torrentcraft_cli PRIVATE cxx_std_17)
target_include_directories(torrentcraft_cli PRIVATE "${PROJECT_SOURCE_DIR}/src/cli")
target_link_libraries(
    torrentcraft_cli
    PRIVATE
        TorrentUtils::Core
        TorrentUtils::Frontend
        indicators::indicators
        nlohmann_json::nlohmann_json
)
torrentutils_set_project_warnings(torrentcraft_cli)

add_executable(torrentcraft src/cli/main.cpp)
target_compile_features(torrentcraft PRIVATE cxx_std_17)
target_link_libraries(torrentcraft PRIVATE torrentcraft_cli)
torrentutils_set_project_warnings(torrentcraft)
if(WIN32)
    target_sources(torrentcraft PRIVATE
        src/gui/resources/TorrentCraft.rc
        "${PROJECT_BINARY_DIR}/generated/TorrentCraftVersion.rc")
endif()

configure_package_config_file(
    cmake/TorrentUtilsCoreConfig.cmake.in
    "${PROJECT_BINARY_DIR}/TorrentUtilsCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TorrentUtilsCore"
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/TorrentUtilsCoreConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY ExactVersion
)

install(
    TARGETS torrentutils_foundation torrentutils_domain torrentutils_core torrentutils_frontend
            torrentcraft
    EXPORT TorrentUtilsCoreTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    EXPORT TorrentUtilsCoreTargets
    FILE TorrentUtilsCoreTargets.cmake
    NAMESPACE TorrentUtils::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TorrentUtilsCore"
)

install(
    FILES
        "${PROJECT_BINARY_DIR}/TorrentUtilsCoreConfig.cmake"
        "${PROJECT_BINARY_DIR}/TorrentUtilsCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TorrentUtilsCore"
)

install(
    FILES README.md CONTRIBUTING.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(TORRENTUTILS_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg LinguistTools)
    find_package(Git QUIET)

    if(NOT TORRENTCRAFT_GIT_COMMIT AND Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short=7 HEAD
            OUTPUT_VARIABLE TORRENTCRAFT_GIT_COMMIT ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" branch --show-current
            OUTPUT_VARIABLE TORRENTCRAFT_GIT_BRANCH ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT OR NOT TORRENTCRAFT_GIT_BRANCH)
        torrentcraft_read_git_metadata("${PROJECT_SOURCE_DIR}" _torrentcraft_git_file_commit _torrentcraft_git_file_branch)
        if(NOT TORRENTCRAFT_GIT_COMMIT)
            set(TORRENTCRAFT_GIT_COMMIT "${_torrentcraft_git_file_commit}")
        endif()
        if(NOT TORRENTCRAFT_GIT_BRANCH)
            set(TORRENTCRAFT_GIT_BRANCH "${_torrentcraft_git_file_branch}")
        endif()
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT AND DEFINED ENV{CI_COMMIT_SHORT_SHA})
        set(TORRENTCRAFT_GIT_COMMIT "$ENV{CI_COMMIT_SHORT_SHA}")
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT AND DEFINED ENV{CI_COMMIT_SHA})
        set(TORRENTCRAFT_GIT_COMMIT "$ENV{CI_COMMIT_SHA}")
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT AND DEFINED ENV{GITHUB_SHA})
        set(TORRENTCRAFT_GIT_COMMIT "$ENV{GITHUB_SHA}")
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT AND DEFINED ENV{BUILD_SOURCEVERSION})
        set(TORRENTCRAFT_GIT_COMMIT "$ENV{BUILD_SOURCEVERSION}")
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT AND DEFINED ENV{GIT_COMMIT})
        set(TORRENTCRAFT_GIT_COMMIT "$ENV{GIT_COMMIT}")
    endif()
    if(NOT TORRENTCRAFT_GIT_COMMIT)
        set(TORRENTCRAFT_GIT_COMMIT "unknown")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND DEFINED ENV{CI_COMMIT_REF_NAME})
        set(TORRENTCRAFT_GIT_BRANCH "$ENV{CI_COMMIT_REF_NAME}")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND DEFINED ENV{GITHUB_REF_NAME})
        set(TORRENTCRAFT_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND DEFINED ENV{BUILD_SOURCEBRANCHNAME})
        set(TORRENTCRAFT_GIT_BRANCH "$ENV{BUILD_SOURCEBRANCHNAME}")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND DEFINED ENV{GIT_BRANCH})
        set(TORRENTCRAFT_GIT_BRANCH "$ENV{GIT_BRANCH}")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH AND DEFINED ENV{BRANCH_NAME})
        set(TORRENTCRAFT_GIT_BRANCH "$ENV{BRANCH_NAME}")
    endif()
    if(NOT TORRENTCRAFT_GIT_BRANCH)
        set(TORRENTCRAFT_GIT_BRANCH "unknown")
    endif()
    if(NOT TORRENTCRAFT_BUILD_DATE)
        string(TIMESTAMP TORRENTCRAFT_BUILD_DATE "%Y-%m-%d" UTC)
    endif()
    if(CMAKE_BUILD_TYPE)
        set(TORRENTCRAFT_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    else()
        set(TORRENTCRAFT_BUILD_TYPE "Multi-config")
    endif()
    if(DEFINED LibtorrentRasterbar_VERSION)
        set(TORRENTCRAFT_LIBTORRENT_VERSION "${LibtorrentRasterbar_VERSION}")
    else()
        set(TORRENTCRAFT_LIBTORRENT_VERSION "unknown")
    endif()
    set(_torrentcraft_vcpkg_root "")
    if(DEFINED VCPKG_ROOT AND NOT "${VCPKG_ROOT}" STREQUAL "")
        set(_torrentcraft_vcpkg_root "${VCPKG_ROOT}")
    elseif(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(_torrentcraft_vcpkg_root "$ENV{VCPKG_ROOT}")
    elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        get_filename_component(_torrentcraft_vcpkg_toolchain_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
        get_filename_component(_torrentcraft_vcpkg_scripts_dir "${_torrentcraft_vcpkg_toolchain_dir}" DIRECTORY)
        get_filename_component(_torrentcraft_vcpkg_root "${_torrentcraft_vcpkg_scripts_dir}" DIRECTORY)
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/.vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(_torrentcraft_vcpkg_root "${PROJECT_SOURCE_DIR}/.vcpkg")
    endif()
    if(NOT TORRENTCRAFT_VCPKG_COMMIT AND _torrentcraft_vcpkg_root)
        if(Git_FOUND)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_torrentcraft_vcpkg_root}" rev-parse --short=7 HEAD
                OUTPUT_VARIABLE TORRENTCRAFT_VCPKG_COMMIT ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        if(NOT TORRENTCRAFT_VCPKG_COMMIT)
            torrentcraft_read_git_metadata("${_torrentcraft_vcpkg_root}" _torrentcraft_vcpkg_file_commit _torrentcraft_vcpkg_file_branch)
            set(TORRENTCRAFT_VCPKG_COMMIT "${_torrentcraft_vcpkg_file_commit}")
        endif()
    endif()
    if(NOT TORRENTCRAFT_VCPKG_COMMIT AND Git_FOUND AND DEFINED ENV{VCPKG_ROOT})
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "$ENV{VCPKG_ROOT}" rev-parse --short=7 HEAD
            OUTPUT_VARIABLE TORRENTCRAFT_VCPKG_COMMIT ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT TORRENTCRAFT_VCPKG_COMMIT AND DEFINED ENV{VCPKG_COMMIT})
        set(TORRENTCRAFT_VCPKG_COMMIT "$ENV{VCPKG_COMMIT}")
    endif()
    if(NOT TORRENTCRAFT_VCPKG_COMMIT)
        set(TORRENTCRAFT_VCPKG_COMMIT "unknown")
    endif()

    set(TORRENTCRAFT_NLOHMANN_JSON_VERSION "${nlohmann_json_VERSION}")
    set(TORRENTCRAFT_INDICATORS_VERSION "${indicators_VERSION}")
    set(TORRENTCRAFT_OPENSSL_VERSION "${OPENSSL_VERSION}")
    if(NOT TORRENTCRAFT_OPENSSL_VERSION AND DEFINED OpenSSL_VERSION)
        set(TORRENTCRAFT_OPENSSL_VERSION "${OpenSSL_VERSION}")
    endif()
    if(DEFINED Boost_VERSION_STRING)
        set(TORRENTCRAFT_BOOST_VERSION "${Boost_VERSION_STRING}")
    else()
        set(TORRENTCRAFT_BOOST_VERSION "${Boost_VERSION}")
    endif()
    if(BUILD_TESTING)
        find_package(Catch2 CONFIG QUIET)
    endif()
    set(TORRENTCRAFT_CATCH2_VERSION "${Catch2_VERSION}")
    foreach(_torrentcraft_version
            TORRENTCRAFT_NLOHMANN_JSON_VERSION
            TORRENTCRAFT_INDICATORS_VERSION
            TORRENTCRAFT_OPENSSL_VERSION
            TORRENTCRAFT_BOOST_VERSION
            TORRENTCRAFT_CATCH2_VERSION)
        if("${${_torrentcraft_version}}" STREQUAL "")
            set(${_torrentcraft_version} "unknown")
        endif()
    endforeach()
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/TorrentCraftBuildInfo.hpp.in"
        "${PROJECT_BINARY_DIR}/generated/TorrentCraftBuildInfo.hpp"
        @ONLY)

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    add_library(torrentcraft_gui_lib STATIC
        src/gui/AboutDialog.cpp src/gui/AboutDialog.hpp
        src/gui/LicenseDialog.cpp src/gui/LicenseDialog.hpp
        src/gui/Logo.cpp src/gui/Logo.hpp
        src/gui/GuiLogController.cpp src/gui/GuiLogController.hpp
        src/gui/GuiTaskRunner.cpp src/gui/GuiTaskRunner.hpp
        src/gui/FileTreeModel.cpp src/gui/FileTreeModel.hpp
        src/gui/MainWindow.cpp src/gui/MainWindow.hpp
        src/gui/OperationCloseDialog.cpp src/gui/OperationCloseDialog.hpp
        src/gui/TrackerEditDialog.cpp src/gui/TrackerEditDialog.hpp
        src/gui/NotificationDialog.cpp src/gui/NotificationDialog.hpp
        src/gui/PresetPreviewDialog.cpp src/gui/PresetPreviewDialog.hpp
        src/gui/MainWindow.ui src/gui/AboutDialog.ui src/gui/LicenseDialog.ui
        src/gui/NotificationDialog.ui src/gui/OperationCloseDialog.ui
        src/gui/TrackerEditDialog.ui
        src/gui/PresetPreviewDialog.ui
        src/gui/resources/TorrentCraft.qrc)
    set(TORRENTCRAFT_GUI_TS_FILES
        "${PROJECT_SOURCE_DIR}/lang/TorrentCraft_en.ts"
        "${PROJECT_SOURCE_DIR}/lang/TorrentCraft_zh_CN.ts")
    set(TORRENTCRAFT_GUI_QM_FILES)
    foreach(ts_file IN LISTS TORRENTCRAFT_GUI_TS_FILES)
        get_filename_component(ts_name "${ts_file}" NAME_WE)
        set(qm_file "${PROJECT_BINARY_DIR}/${ts_name}.qm")
        add_custom_command(
            OUTPUT "${qm_file}"
            COMMAND Qt6::lrelease "${ts_file}" -qm "${qm_file}"
            DEPENDS "${ts_file}"
            VERBATIM)
        list(APPEND TORRENTCRAFT_GUI_QM_FILES "${qm_file}")
    endforeach()
    add_custom_target(torrentcraft_gui_translations DEPENDS ${TORRENTCRAFT_GUI_QM_FILES})
    add_dependencies(torrentcraft_gui_lib torrentcraft_gui_translations)

    # Embed the project license and any dependency copyright files available in
    # the active vcpkg installation. Missing package metadata is non-fatal; the
    # About dialog reports it as unavailable.
    set(TORRENTCRAFT_LICENSE_FILES "${PROJECT_SOURCE_DIR}/LICENSE")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/LICENSE" PROPERTIES
        QT_RESOURCE_ALIAS "licenses/torrentcraft.txt")
    set(_torrentcraft_license_specs
        "qt|qtbase|qtbase/copyright"
        "libtorrent|libtorrent|libtorrent/copyright"
        "nlohmann-json|nlohmann-json|nlohmann-json/copyright"
        "indicators|indicators|indicators/copyright"
        "openssl|openssl|openssl/copyright"
        "boost|boost-headers|boost-headers/copyright"
        "catch2|catch2|catch2/copyright")
    foreach(_spec IN LISTS _torrentcraft_license_specs)
        string(REPLACE "|" ";" _spec_parts "${_spec}")
        list(GET _spec_parts 0 _license_alias)
        list(GET _spec_parts 1 _license_package)
        list(GET _spec_parts 2 _license_relative)
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(_license_path
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${_license_relative}")
            if(EXISTS "${_license_path}")
                list(APPEND TORRENTCRAFT_LICENSE_FILES "${_license_path}")
                set_source_files_properties("${_license_path}" PROPERTIES
                    QT_RESOURCE_ALIAS "licenses/${_license_alias}.txt")
            endif()
        endif()
    endforeach()
    qt_add_resources(torrentcraft_gui_lib torrentcraft_gui_licenses_qrc
        PREFIX "/torrentcraft"
        FILES ${TORRENTCRAFT_LICENSE_FILES})
    qt_add_resources(torrentcraft_gui_lib torrentcraft_gui_translations_qrc
        PREFIX "/torrentcraft/lang"
        BASE "${PROJECT_BINARY_DIR}"
        FILES ${TORRENTCRAFT_GUI_QM_FILES})
    target_compile_features(torrentcraft_gui_lib PUBLIC cxx_std_17)
    target_include_directories(torrentcraft_gui_lib PUBLIC
        "${PROJECT_SOURCE_DIR}/src/gui"
        "${PROJECT_BINARY_DIR}/generated")
    target_link_libraries(torrentcraft_gui_lib PUBLIC TorrentUtils::Core TorrentUtils::Frontend Qt6::Widgets Qt6::Svg)
    torrentutils_set_project_warnings(torrentcraft_gui_lib)
    if(MSVC)
        # Qt6 q20bit.h emits C4702 under /W4; keep /WX for project sources.
        target_compile_options(torrentcraft_gui_lib PRIVATE /wd4702)
    endif()
    add_executable(torrentcraft-gui src/gui/main.cpp)
    target_compile_features(torrentcraft-gui PRIVATE cxx_std_17)
    target_include_directories(torrentcraft-gui PRIVATE "${PROJECT_SOURCE_DIR}/src/cli")
    target_link_libraries(torrentcraft-gui PRIVATE torrentcraft_gui_lib torrentcraft_cli)
    if(NOT QT6_IS_SHARED_LIBS_BUILD)
        if(WIN32)
            qt_import_plugins(torrentcraft-gui
                INCLUDE_BY_TYPE platforms
                Qt6::QWindowsIntegrationPlugin
                Qt6::QMinimalIntegrationPlugin)
        elseif(UNIX AND NOT APPLE)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                target_compile_definitions(torrentcraft-gui
                    PRIVATE TORRENTCRAFT_STATIC_LINUX_QT=1)
                qt_import_plugins(torrentcraft-gui
                    INCLUDE_BY_TYPE platforms Qt6::QXcbIntegrationPlugin
                    INCLUDE_BY_TYPE platformthemes Qt6::QGtk3ThemePlugin)
            else()
                qt_import_plugins(torrentcraft-gui
                    INCLUDE_BY_TYPE platforms Qt6::QXcbIntegrationPlugin)
            endif()
        endif()
    endif()
    torrentutils_set_project_warnings(torrentcraft-gui)
    if(WIN32)
        target_sources(torrentcraft-gui PRIVATE
            src/gui/resources/TorrentCraft.rc
            "${PROJECT_BINARY_DIR}/generated/TorrentCraftVersion.rc")
        target_link_libraries(torrentcraft-gui PRIVATE Shell32)
        set_target_properties(torrentcraft-gui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(UNIX AND NOT APPLE AND QT6_IS_SHARED_LIBS_BUILD)
        set_target_properties(torrentcraft-gui PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()
    install(TARGETS torrentcraft-gui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES src/gui/resources/TorrentCraft.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME torrentcraft.svg)
    install(FILES src/gui/resources/org.torrentcraft.TorrentCraft.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    if(BUILD_TESTING)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
        add_executable(torrentcraft_gui_tests tests/gui_test.cpp)
        target_compile_definitions(
            torrentcraft_gui_tests
            PRIVATE TORRENTCRAFT_GUI_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        target_link_libraries(torrentcraft_gui_tests PRIVATE torrentcraft_gui_lib Qt6::Test)
        if(NOT QT6_IS_SHARED_LIBS_BUILD AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            qt_import_plugins(torrentcraft_gui_tests
                INCLUDE_BY_TYPE platforms Qt6::QXcbIntegrationPlugin
                INCLUDE_BY_TYPE platformthemes Qt6::QGtk3ThemePlugin)
        endif()
        set(torrentcraft_gui_test_platform offscreen)
        if(UNIX AND NOT APPLE)
            set(torrentcraft_gui_test_platform xcb)
        elseif(WIN32)
            set(torrentcraft_gui_test_platform minimal)
        endif()
        set(torrentcraft_gui_test_environment
            "QT_QPA_PLATFORM=${torrentcraft_gui_test_platform}")
        if(UNIX)
            list(APPEND torrentcraft_gui_test_environment
                "LANG=C.UTF-8"
                "LC_ALL=C.UTF-8")
        endif()
        if(NOT QT6_IS_SHARED_LIBS_BUILD AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            list(APPEND torrentcraft_gui_test_environment
                "QT_QPA_PLATFORMTHEME=gtk3")
        endif()
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(torrentcraft_gui_qt_platform_plugin_paths
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/plugins/platforms"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/plugins/platforms"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                list(REVERSE torrentcraft_gui_qt_platform_plugin_paths)
            endif()
            foreach(torrentcraft_gui_qt_platform_plugin_path
                    IN LISTS torrentcraft_gui_qt_platform_plugin_paths)
                if(EXISTS "${torrentcraft_gui_qt_platform_plugin_path}")
                    list(APPEND torrentcraft_gui_test_environment
                        "QT_QPA_PLATFORM_PLUGIN_PATH=${torrentcraft_gui_qt_platform_plugin_path}")
                    break()
                endif()
            endforeach()
        endif()
        if(WIN32)
            # The Windows CI smoke test must not open a native desktop window or
            # run the full widget test suite. The application exits when the
            # smoke marker is set; minimal is also available in static Qt.
            add_test(NAME torrentcraft_gui_smoke
                COMMAND torrentcraft-gui)
            list(APPEND torrentcraft_gui_test_environment
                "TORRENTCRAFT_GUI_SMOKE=1")
        else()
            add_test(NAME torrentcraft_gui_smoke
                COMMAND torrentcraft_gui_tests -platform ${torrentcraft_gui_test_platform})
        endif()
        set_tests_properties(torrentcraft_gui_smoke PROPERTIES
            ENVIRONMENT "${torrentcraft_gui_test_environment}"
            TIMEOUT 120)
    endif()
endif()
