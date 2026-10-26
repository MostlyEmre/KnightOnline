add_executable(Genie.Tests
  GenieRules_test.cpp
  GenieSettings_test.cpp)

set_target_properties(Genie.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# GenieRules.h and GenieSettings.h are header-only: the tests need the headers and shared (CIni).
target_include_directories(Genie.Tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(Genie.Tests PRIVATE
  gtest
  gtest_main
  shared)

add_test(
  NAME GenieTests
  COMMAND Genie.Tests)
