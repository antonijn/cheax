cmake_minimum_required (VERSION 3.10)
project (cheax LANGUAGES C)

option (BUILD_SHARED_LIBS   "Build shared libraries"   ON)
option (BUILD_DOCS          "Build Doxygen html pages" ON)
option (CHEAKY_USE_READLINE "Use readline in cheaky"   ON)

set (CMAKE_C_STANDARD 11)

include (GNUInstallDirs)

if (NOT MSVC)
	add_compile_options (-Wall -Wextra -Wno-unused-parameter -pedantic-errors)
endif ()

if (BUILD_DOCS)
	set (DOXYGEN_SKIP_DOT TRUE)
	find_package (Doxygen)
endif ()

enable_testing ()

add_subdirectory (cheaky)
add_subdirectory (cheax)
add_subdirectory (libcheax)
add_subdirectory (test)

if (DOXYGEN_FOUND AND BUILD_DOCS)
	add_subdirectory (docs)
endif ()

install (FILES stdlib/prelude.chx DESTINATION ${CMAKE_INSTALL_DATADIR}/cheax/)
install (FILES stdlib/testing.chx DESTINATION ${CMAKE_INSTALL_DATADIR}/cheax/)
