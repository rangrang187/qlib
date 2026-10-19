include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW ON)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
foreach(component IN ITEMS common hive map memory_pool pool)
  add_executable(${component}_test ${component}_test.cpp)
  target_link_libraries(${component}_test PRIVATE qlib::qlib GTest::gtest_main)
  gtest_discover_tests(${component}_test)
endforeach()
