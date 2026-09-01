# This file is loaded by CMAKE_PROJECT_INCLUDE for both the main musl release
# project and target ports using the x64-linux-musl-static triplet. It runs
# after CMake's platform toolchain has initialized CMAKE_FIND_LIBRARY_SUFFIXES
# and before each project is configured.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
list(PREPEND CMAKE_LIBRARY_PATH "/usr/local/lib")
