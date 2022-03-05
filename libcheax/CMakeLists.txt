cmake_minimum_required (VERSION 3.12)

include(GenerateExportHeader)

add_custom_target (
	generate_version_header
	COMMAND
		${CMAKE_COMMAND} -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS} -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in -DOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h -P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateVersion.cmake
	WORKING_DIRECTORY
		${CMAKE_SOURCE_DIR}
)

include(CheckFunctionExists)
check_function_exists(strtod_l       HAVE_STRTOD_L)
check_function_exists(newlocale      HAVE_NEWLOCALE)
check_function_exists(_strtod_l      HAVE_STUPID_WINDOWS_STRTOD_L)
check_function_exists(_create_locale HAVE_STUPID_WINDOWS_CREATE_LOCALE)
if (NOT (HAVE_NEWLOCALE OR HAVE_STUPID_WINDOWS_CREATE_LOCALE))
	message(SEND_ERROR "Target platform must support newlocale() or _create_locale().")
endif()
if (NOT (HAVE_STRTOD_L OR HAVE_NEWLOCALE OR (HAVE_STUPID_WINDOWS_STRTOD_L AND HAVE_STUPID_WINDOWS_CREATE_LOCALE)))
	message(SEND_ERROR "Target platform must support strtod_l(), uselocale(), or both _strtod_l() and _create_locale().")
endif()

configure_file (src/config.h.in src/config.h)

# hide all library functions by, unless declared with CHX_API
set(CMAKE_C_VISIBILITY_PRESET hidden)

add_library (libcheax
	src/api.c
	src/stream.c
	src/rbtree.c
	src/loc.c
	src/gc.c
	src/read.c
	src/eval.c
	src/print.c
	src/format.c
	src/builtins.c
	src/unpack.c)
add_dependencies (libcheax generate_version_header)

generate_export_header (libcheax
	BASE_NAME          CHX
	EXPORT_MACRO_NAME  CHX_API
	EXPORT_FILE_NAME   ${CMAKE_CURRENT_SOURCE_DIR}/include/cheax/export.h
	INCLUDE_GUARD_NAME CHEAX_EXPORT_H)

if (USE_BOEHM_GC)
	target_link_libraries (libcheax gc)
endif ()

target_include_directories (libcheax PUBLIC include)
set_target_properties (libcheax PROPERTIES OUTPUT_NAME "cheax")
install (TARGETS libcheax DESTINATION lib)
install (FILES include/cheax.h DESTINATION include)
install (FILES include/cheax/export.h DESTINATION include/cheax/)
install (FILES include/cheax/cheax.h DESTINATION include/cheax/)
install (FILES share/cheax/prelude.chx DESTINATION share/cheax/)
install (FILES COPYING DESTINATION share/licenses/libcheax/)
