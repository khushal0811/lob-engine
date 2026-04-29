# Use the locally installed GoogleTest (e.g., via brew)
find_package(GTest REQUIRED)

# Alias to match the targets created by FetchContent if needed
if(NOT TARGET GTest::gtest AND TARGET GTest::GTest)
  add_library(GTest::gtest ALIAS GTest::GTest)
endif()
if(NOT TARGET GTest::gmock AND TARGET GTest::GMock)
  add_library(GTest::gmock ALIAS GTest::GMock)
endif()
if(NOT TARGET GTest::gtest_main AND TARGET GTest::Main)
  add_library(GTest::gtest_main ALIAS GTest::Main)
endif()
