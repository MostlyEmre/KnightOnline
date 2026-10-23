# Get SQLite3 (amalgamation) package
#
# Makes the sqlite3 target available. Used by the SQLite database backend
# (OPENKO_DB_BACKEND=SQLITE).

fetchcontent_declare(
  sqlite3_amalgamation
  URL                         "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip"
  URL_HASH                    "SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d"
  DOWNLOAD_EXTRACT_TIMESTAMP  ON
  EXCLUDE_FROM_ALL
)

fetchcontent_makeavailable(sqlite3_amalgamation)

add_library(sqlite3 STATIC
  "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c"
  "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.h"
)

target_include_directories(sqlite3 PUBLIC
  "${sqlite3_amalgamation_SOURCE_DIR}"
)

target_compile_definitions(sqlite3
  PUBLIC
    SQLITE_THREADSAFE=1
  PRIVATE
    SQLITE_DQS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_DEPRECATED
)

# Third-party code: build it quietly.
target_compile_options(sqlite3 PRIVATE
  $<$<C_COMPILER_ID:MSVC>:/W0>
  $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)

find_package(Threads REQUIRED)
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

set(SQLite3Amalgamation_FOUND TRUE)
