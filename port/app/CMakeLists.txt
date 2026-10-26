# KnightOnline.app pieces that aren't part of upstream's tree: the server supervisor the app starts.
# port/app/make_app.py assembles the bundle from these, the client, the servers and the game data.

add_executable(ko-servers ko-servers/main.cpp)
target_compile_features(ko-servers PRIVATE cxx_std_20)
target_compile_options(ko-servers PRIVATE -Wall -Wextra -Werror)

# The port's game data goes into the player's database with the SQLite backend's collations
# (only in a build with the servers on SQLite, which is what the app bundles).
# (port/app comes before src/db-library in the tree, so this asks the options, not the targets.)
if(OPENKO_BUILD_SERVERS AND OPENKO_DB_BACKEND STREQUAL "SQLITE")
  target_include_directories(ko-servers PRIVATE "${CMAKE_SOURCE_DIR}/src/db-library/sqlite")
  target_link_libraries(ko-servers PRIVATE nanodbc sqlite3)
  target_compile_definitions(ko-servers PRIVATE KO_SERVERS_GAME_DATA=1)
endif()
