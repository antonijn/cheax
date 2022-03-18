cmake_minimum_required (VERSION 3.10)

add_executable(cheaky cheaky.c)
add_definitions (-DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")
target_link_libraries(cheaky readline libcheax)
install(TARGETS cheaky DESTINATION bin)
install(FILES COPYING DESTINATION share/licenses/cheaky/)
install(FILES WARRANTY DESTINATION share/licenses/cheaky/)
install(FILES CONDITIONS DESTINATION share/licenses/cheaky/)
