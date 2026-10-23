# Database parity probe (proof 2): built against whichever OPENKO_DB_BACKEND is configured.
add_executable(db-parity-probe
  DbParityProbe.cpp
)

target_link_libraries(db-parity-probe PRIVATE
  db-library
  db-models
  nanodbc
)

if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET db-parity-probe PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_compile_options(db-parity-probe PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra>
)
