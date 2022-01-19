cmake_minimum_required (VERSION 3.10)

add_custom_target (
	generate_version_header
	COMMAND
		${CMAKE_COMMAND} -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS} -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in -DOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h -P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateVersion.cmake
	WORKING_DIRECTORY
		${CMAKE_SOURCE_DIR}
)

configure_file (src/config.h.in src/config.h)

add_library (libcheax src/api.c src/read.c src/eval.c src/print.c src/builtins.c)
add_dependencies (libcheax generate_version_header)
target_link_libraries (libcheax gc)
target_include_directories (libcheax PUBLIC include)
set_target_properties (libcheax PROPERTIES OUTPUT_NAME "cheax")
install (TARGETS libcheax DESTINATION lib)
install (FILES include/cheax.h DESTINATION include)
install (FILES share/cheax/prelude.chx DESTINATION share/cheax/)
install (FILES COPYING DESTINATION share/licenses/libcheax/)
