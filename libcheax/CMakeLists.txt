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

configure_file (src/setup.h.in src/setup.h)

# hide all library functions, unless declared with CHX_API
set(CMAKE_C_VISIBILITY_PRESET hidden)

add_library (libcheax
	src/arith.c
	src/config.c
	src/core.c
	src/err.c
	src/eval.c
	src/feat.c
	src/format.c
	src/gc.c
	src/io.c
	src/loc.c
	src/maths.c
	src/print.c
	src/rbtree.c
	src/read.c
	src/strm.c
	src/sym.c
	src/unpack.c)
add_dependencies (libcheax generate_version_header)
if (NOT MSVC)
	target_link_libraries (libcheax m)
endif ()

generate_export_header (libcheax
	BASE_NAME          CHX
	EXPORT_MACRO_NAME  CHX_API
	EXPORT_FILE_NAME   ${CMAKE_CURRENT_SOURCE_DIR}/include/cheax/export.h
	INCLUDE_GUARD_NAME CHEAX_EXPORT_H)

target_include_directories (libcheax PUBLIC include)
set_target_properties (libcheax PROPERTIES OUTPUT_NAME "cheax")
install (TARGETS libcheax DESTINATION lib)
install (FILES include/cheax.h DESTINATION include)
install (FILES include/cheax/export.h DESTINATION include/cheax/)
install (FILES include/cheax/cheax.h DESTINATION include/cheax/)
install (FILES share/cheax/prelude.chx DESTINATION share/cheax/)
install (FILES COPYING DESTINATION share/licenses/libcheax/)
