cmake_minimum_required (VERSION 3.12)

include(GenerateExportHeader)

add_custom_target (
	generate_version_header
	COMMAND
		${CMAKE_COMMAND} -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS} -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in -DOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h -P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateVersion.cmake
	WORKING_DIRECTORY
		${CMAKE_SOURCE_DIR}
)

include (CheckSymbolExists)
include (CheckIncludeFiles)
include (CheckFunctionExists)

check_symbol_exists (__GNU_LIBRARY__ "features.h" _GNU_SOURCE)
if (_GNU_SOURCE)
	string (APPEND CMAKE_C_FLAGS "-D_GNU_SOURCE")
endif ()

check_include_files ("xlocale.h"   HAVE_XLOCALE_H)
check_include_files ("malloc.h"    HAVE_MALLOC_H)
check_include_files ("malloc_np.h" HAVE_MALLOC_NP_H)

function (CHECK_PLATFORM_FUNC FUNC_NAME FEAT_VAR)
	check_function_exists (${FUNC_NAME} ${FEAT_VAR})
	if (${FEAT_VAR})
		list (APPEND FEATURES "${FUNC_NAME}")
		set (FEATURES ${FEATURES} PARENT_SCOPE)
	endif ()
endfunction ()

check_platform_func (newlocale          HAVE_NEWLOCALE)
check_platform_func (strtod_l           HAVE_STRTOD_L)
check_platform_func (vfprintf_l         HAVE_VFPRINTF_L)
check_platform_func (vsnprintf_l        HAVE_VSNPRINTF_L)
check_platform_func (malloc_usable_size HAVE_MALLOC_USABLE_SIZE)
check_platform_func (_create_locale     HAVE_WINDOWS_CREATE_LOCALE)
check_platform_func (_strtod_l          HAVE_WINDOWS_STRTOD_L)
check_platform_func (_vfprintf_l        HAVE_WINDOWS_VFPRINTF_L)
check_platform_func (_vsnprintf_l       HAVE_WINDOWS_VSNPRINTF_L)
check_platform_func (_msize             HAVE_WINDOWS_MSIZE)

if (NOT (HAVE_NEWLOCALE OR HAVE_WINDOWS_CREATE_LOCALE))
	message(SEND_ERROR "Target platform must support newlocale() or _create_locale().")
endif()
if (NOT (HAVE_STRTOD_L OR HAVE_NEWLOCALE OR (HAVE_WINDOWS_STRTOD_L AND HAVE_WINDOWS_CREATE_LOCALE)))
	message(SEND_ERROR "Target platform must support strtod_l(), uselocale(), or both _strtod_l() and _create_locale().")
endif()
if (NOT (HAVE_VFPRINTF_L OR HAVE_NEWLOCALE OR (HAVE_WINDOWS_VFPRINTF_L AND HAVE_WINDOWS_CREATE_LOCALE)))
	message(SEND_ERROR "Target platform must support vfprintf_l(), uselocale(), or both _vfprintf_l() and _create_locale().")
endif()
if (NOT (HAVE_VSNPRINTF_L OR HAVE_NEWLOCALE OR (HAVE_WINDOWS_VSNPRINTF_L AND HAVE_WINDOWS_CREATE_LOCALE)))
	message(SEND_ERROR "Target platform must support vsnprintf_l(), uselocale(), or both _vsnprintf_l() and _create_locale().")
endif()

if (BUILD_SHARED_LIBS)
	list (APPEND FEATURES "shared")
else ()
	list (APPEND FEATURES "static")
endif ()
list (JOIN FEATURES " " VERSION_SUFFIX)

configure_file (src/setup.h.in src/setup.h)

# hide all library functions, unless declared with CHX_API
set(CMAKE_C_VISIBILITY_PRESET hidden)

add_library (libcheax
	src/arith.c
	src/cinfo.c
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
