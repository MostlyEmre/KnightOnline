# KnightOnline.app pieces that aren't part of upstream's tree: the server supervisor the app starts.
# port/app/make_app.py assembles the bundle from these, the client, the servers and the game data.

add_executable(ko-servers ko-servers/main.cpp)
target_compile_features(ko-servers PRIVATE cxx_std_20)
target_compile_options(ko-servers PRIVATE -Wall -Wextra -Werror)
