include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW ON)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
add_executable(common_test common_test.cpp)
target_link_libraries(common_test PRIVATE qlib::qlib GTest::gtest_main)
gtest_discover_tests(common_test)
