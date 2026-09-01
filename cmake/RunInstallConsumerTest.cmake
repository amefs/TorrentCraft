cmake_minimum_required(VERSION 3.25)

foreach(required_variable IN ITEMS
        CORE_BINARY_DIR
        CORE_SOURCE_DIR
        TEST_BUILD_TYPE
        TEST_CXX_COMPILER
        TEST_CXX_FLAGS
        TEST_EXE_LINKER_FLAGS
        TEST_GENERATOR
        TEST_GENERATOR_PLATFORM
        TEST_GENERATOR_TOOLSET
        TEST_ROOT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} must be defined")
    endif()
endforeach()

set(install_prefix "${TEST_ROOT}/prefix")
set(consumer_binary_dir "${TEST_ROOT}/build")
set(consumer_prefix_path "${install_prefix}")
if(DEFINED TEST_DEPENDENCY_PREFIX_PATH_COUNT)
    if(TEST_DEPENDENCY_PREFIX_PATH_COUNT GREATER 0)
        math(EXPR test_dependency_prefix_last_index
            "${TEST_DEPENDENCY_PREFIX_PATH_COUNT} - 1")
        foreach(test_dependency_prefix_index RANGE 0 ${test_dependency_prefix_last_index})
            set(test_dependency_prefix_variable
                "TEST_DEPENDENCY_PREFIX_PATH_${test_dependency_prefix_index}")
            if(DEFINED ${test_dependency_prefix_variable})
                list(APPEND consumer_prefix_path
                    "${${test_dependency_prefix_variable}}")
            endif()
        endforeach()
    endif()
elseif(DEFINED TEST_DEPENDENCY_PREFIX_PATH AND NOT TEST_DEPENDENCY_PREFIX_PATH STREQUAL "")
    list(APPEND consumer_prefix_path "${TEST_DEPENDENCY_PREFIX_PATH}")
endif()
string(REPLACE ";" "\\;" consumer_prefix_path_argument "${consumer_prefix_path}")

file(REMOVE_RECURSE "${TEST_ROOT}")

set(build_config_arguments)
set(ctest_config_arguments)
if(DEFINED TEST_CONFIG AND NOT TEST_CONFIG STREQUAL "")
    list(APPEND build_config_arguments --config "${TEST_CONFIG}")
    list(APPEND ctest_config_arguments -C "${TEST_CONFIG}")
endif()

set(generator_arguments -G "${TEST_GENERATOR}")
if(NOT TEST_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND generator_arguments -A "${TEST_GENERATOR_PLATFORM}")
endif()
if(NOT TEST_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND generator_arguments -T "${TEST_GENERATOR_TOOLSET}")
endif()

set(consumer_configure_arguments
    -S "${CORE_SOURCE_DIR}/tests/install-consumer"
    -B "${consumer_binary_dir}"
    ${generator_arguments}
    "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}"
    "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${TEST_CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${TEST_EXE_LINKER_FLAGS}"
    "-DCMAKE_PREFIX_PATH=${consumer_prefix_path_argument}")
if(DEFINED TEST_MSVC_RUNTIME_LIBRARY AND NOT TEST_MSVC_RUNTIME_LIBRARY STREQUAL "")
    list(APPEND consumer_configure_arguments
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${TEST_MSVC_RUNTIME_LIBRARY}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${CORE_BINARY_DIR}"
        --prefix "${install_prefix}"
        ${build_config_arguments}
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        ${consumer_configure_arguments}
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build "${consumer_binary_dir}"
        ${build_config_arguments}
    COMMAND_ERROR_IS_FATAL ANY
)

set(consumer_runtime_path)
if(DEFINED TEST_DEPENDENCY_RUNTIME_PATH_COUNT)
    if(TEST_DEPENDENCY_RUNTIME_PATH_COUNT GREATER 0)
        math(EXPR test_dependency_runtime_last_index
            "${TEST_DEPENDENCY_RUNTIME_PATH_COUNT} - 1")
        foreach(test_dependency_runtime_index RANGE 0 ${test_dependency_runtime_last_index})
            set(test_dependency_runtime_variable
                "TEST_DEPENDENCY_RUNTIME_PATH_${test_dependency_runtime_index}")
            if(DEFINED ${test_dependency_runtime_variable})
                list(APPEND consumer_runtime_path
                    "${${test_dependency_runtime_variable}}")
            endif()
        endforeach()
    endif()
elseif(DEFINED TEST_DEPENDENCY_RUNTIME_PATH AND NOT TEST_DEPENDENCY_RUNTIME_PATH STREQUAL "")
    foreach(runtime_dir IN LISTS TEST_DEPENDENCY_RUNTIME_PATH)
        list(APPEND consumer_runtime_path "${runtime_dir}")
    endforeach()
endif()
list(APPEND consumer_runtime_path "$ENV{PATH}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${consumer_runtime_path}"
        "${CMAKE_CTEST_COMMAND}"
        --test-dir "${consumer_binary_dir}"
        --output-on-failure
        ${ctest_config_arguments}
    COMMAND_ERROR_IS_FATAL ANY
)
