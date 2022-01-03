cmake_minimum_required (VERSION 2.6)

include(CheckFunctionExists)
check_function_exists(isatty HAVE_ISATTY)
configure_file(config.h.in config.h)

add_executable(cheaky cheaky.c)
add_definitions (-DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")
target_link_libraries(cheaky LINK_PUBLIC readline libcheax)
install(TARGETS cheaky DESTINATION bin)
install(FILES COPYING DESTINATION share/licenses/cheaky/)
install(FILES WARRANTY DESTINATION share/licenses/cheaky/)
install(FILES CONDITIONS DESTINATION share/licenses/cheaky/)
