# SQLite database backend. Provides the `nanodbc` target (the subset of the nanodbc API
# the servers use) so db-library, db-models and the servers build unchanged against it.
add_library(nanodbc STATIC
  include/nanodbc/nanodbc.h
  Backend.cpp
  Backend.h
  Collation.cpp
  Collation.h
  nanodbc.cpp
  ProcContext.cpp
  ProcContext.h
  Procedures.cpp
)

set_target_properties(nanodbc PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

target_compile_features(nanodbc PUBLIC cxx_std_20)

target_include_directories(nanodbc
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(nanodbc PRIVATE sqlite3)

# Lets code that needs to know (tests, tools) tell the backends apart.
target_compile_definitions(nanodbc PUBLIC OPENKO_DB_BACKEND_SQLITE=1)
target_compile_definitions(nanodbc PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET nanodbc PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_compile_options(nanodbc PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra>
)

# Loads a SQL script (as produced by port/db/openkodb_to_sqlite.py) into a database file
# with the backend's collations registered, so the seed database's indexes match the
# comparisons the servers make.
add_executable(openko-sqlite-load
  tools/SqliteLoad.cpp
)

target_include_directories(openko-sqlite-load PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(openko-sqlite-load PRIVATE nanodbc sqlite3)

if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET openko-sqlite-load PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_compile_options(openko-sqlite-load PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra>
)
