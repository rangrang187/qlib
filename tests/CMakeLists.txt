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

find_package(Threads REQUIRED)
add_executable(single_queue_test single_queue_test.cpp)
target_link_libraries(single_queue_test PRIVATE qlib::qlib GTest::gtest_main Threads::Threads)
# 进程级超时兜底；并发用例还在两侧检查截止时间，并先停止、join 再断言。
gtest_discover_tests(single_queue_test PROPERTIES TIMEOUT 120)

# 基准单独运行，不作为 CTest 功能测试，也不链接测试框架。
add_executable(map_bench map_bench.cpp)
target_link_libraries(map_bench PRIVATE qlib::qlib)
