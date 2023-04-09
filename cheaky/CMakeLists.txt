cmake_minimum_required (VERSION 3.10)

add_executable(cheaky cheaky.c)
target_link_libraries (cheaky libcheax)

include (CheckIncludeFiles)

if (CHEAKY_USE_READLINE)
	# stdio.h has to be added since readline.h and history.h depend
	# on it
	check_include_files ("stdio.h;readline/readline.h;readline/history.h" HAVE_READLINE)

	if (HAVE_READLINE)
		add_definitions (-DCHEAKY_USE_READLINE)
		target_link_libraries (cheaky readline)
	else ()
		message (WARNING "Option CHEAKY_USE_READLINE enabled, but no readline library found. Will disable readline support.")
	endif ()
endif ()

add_definitions (-DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")

install(TARGETS cheaky DESTINATION bin)
install(FILES COPYING DESTINATION share/licenses/cheaky/)
install(FILES WARRANTY DESTINATION share/licenses/cheaky/)
install(FILES CONDITIONS DESTINATION share/licenses/cheaky/)
