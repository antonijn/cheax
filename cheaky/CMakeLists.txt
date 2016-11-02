cmake_minimum_required (VERSION 2.6)
add_executable(cheaky cheaky.c)
target_link_libraries(cheaky LINK_PUBLIC readline libcheax gc)
install(TARGETS cheaky DESTINATION bin)
