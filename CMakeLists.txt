cmake_minimum_required(VERSION 3.28)
project(qlib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(qlib INTERFACE)
add_library(qlib::qlib ALIAS qlib)
target_include_directories(qlib INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(qlib INTERFACE cxx_std_23)

option(QLIB_BUILD_TESTS "构建 qlib 测试" ${PROJECT_IS_TOP_LEVEL})
if(QLIB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
