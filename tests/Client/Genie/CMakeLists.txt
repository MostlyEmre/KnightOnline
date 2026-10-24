add_executable(Genie.Tests
  GenieRules_test.cpp)

set_target_properties(Genie.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# GenieRules.h is header-only and needs nothing from the client but the header.
target_include_directories(Genie.Tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(Genie.Tests PRIVATE
  gtest
  gtest_main)

add_test(
  NAME GenieTests
  COMMAND Genie.Tests)
