cmake_minimum_required (VERSION 2.8.11)
include(CheckFunctionExists)
project (CHEAX)

add_definitions (-DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")
add_library (cheax SHARED src/read.c src/eval.c src/builtins.c)
target_include_directories (cheax PUBLIC include)
install (TARGETS cheax DESTINATION lib)
install (FILES include/cheax.h DESTINATION include)
install (FILES share/cheax/prelude.chx DESTINATION share/cheax)

add_executable(cheaky cheaky.c)
target_link_libraries(cheaky LINK_PUBLIC cheax gc)
install(TARGETS cheaky DESTINATION bin)
