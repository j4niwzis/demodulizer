cmake_minimum_required(VERSION 4.3.3 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(demodulizer VERSION 0.1 LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
CPMAddPackage(NAME libtooling-wrapper
  GIT_REPOSITORY https://github.com/j4niwzis/libtooling-wrapper.git
  GIT_TAG main
  GIT_SHALLOW TRUE)

add_library(demodulizer_lib STATIC src/demodulizer.cc)
target_sources(demodulizer_lib PUBLIC FILE_SET CXX_MODULES FILES src/demodulizer.cc)
target_link_libraries(demodulizer_lib PUBLIC libtooling)
target_compile_features(demodulizer_lib PUBLIC cxx_std_23)

add_executable(demodulizer src/main.cc)
target_link_libraries(demodulizer PRIVATE demodulizer_lib)
