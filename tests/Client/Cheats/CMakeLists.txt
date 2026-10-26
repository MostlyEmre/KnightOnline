add_executable(Cheats.Tests
  CheatRules_test.cpp)

set_target_properties(Cheats.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# CheatRules.h and shared/CheatSettings.h are header-only and use the standard library alone.
target_include_directories(Cheats.Tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(Cheats.Tests PRIVATE
  gtest
  gtest_main
  shared)

add_test(
  NAME CheatsTests
  COMMAND Cheats.Tests)
