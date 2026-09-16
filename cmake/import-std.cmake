# Enable CMake's experimental `import std` support.
#
# The UUID is a gate: CMake compares it against the value of the release it
# ships in, and refuses anything else with
#
#   Experimental `import std` support not enabled when detecting toolchain;
#   it must be set before `CXX` is enabled (usually a `project()` call).
#
# So a hard-coded value ties the project to exactly one CMake release, and a
# project that pins a different one than its dependencies cannot be configured
# at all (a dependency's own `project()` call re-detects the toolchain with its
# own value). Choose from the CMake that is actually running instead, which is
# what lets the same tree build on a distribution that ships CMake 4.3 (Alpine)
# and against Kitware's 4.4 binaries.
#
# Each value is the one published in `Help/dev/experimental.rst` of that
# release, and can be read back out of the cmake binary itself:
#   strings $(command -v cmake) | grep -E '^[0-9a-f]{8}-'
#
# Must be included BEFORE `project()`, which is when CXX is enabled.

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
  # Also used for releases newer than we know about: if the gate moved again,
  # CMake reports it and the value here needs another entry.
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "1942b4fa-b2c5-4546-9385-83f254070067")
else()
  message(FATAL_ERROR
    "modulizer is built as C++20 modules against `import std`, which needs "
    "CMake 4.2 or newer (found ${CMAKE_VERSION}).")
endif()

set(CMAKE_CXX_MODULE_STD 1)
