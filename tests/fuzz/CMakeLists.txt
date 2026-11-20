# libFuzzer targets, built with -DTORRENTUTILS_BUILD_FUZZERS=ON (preset linux-clang-fuzz).
# The libraries are instrumented with -fsanitize=fuzzer-no-link from the preset; only these
# executables link the libFuzzer driver.
foreach(target IN ITEMS fuzz_torrent_decode fuzz_tracker_rewrite fuzz_config_json)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE TorrentUtils::Core TorrentUtils::Frontend)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endforeach()
