add_executable(DbSqlite.Tests
  SqliteBackend_test.cpp
)

target_include_directories(DbSqlite.Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(DbSqlite.Tests PRIVATE
  nanodbc
  sqlite3
  gtest
  gtest_main
)

set_target_properties(DbSqlite.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET DbSqlite.Tests PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_compile_options(DbSqlite.Tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra>
)

include(GoogleTest)
add_test(NAME DbSqliteTests COMMAND DbSqlite.Tests)
