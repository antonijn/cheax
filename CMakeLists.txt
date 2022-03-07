cmake_minimum_required (VERSION 3.10)
project (cheax LANGUAGES C)

option (BUILD_SHARED_LIBS "Build shared libraries" ON)
option (USE_BOEHM_GC "Use Boehm GC rather than custom one" OFF)

set (CMAKE_C_STANDARD 99)

if (NOT MSVC)
	add_compile_options (-Wall -Wextra -Wno-unused-parameter -pedantic-errors)
endif()

add_subdirectory (cheaky)
add_subdirectory (cheax)
add_subdirectory (libcheax)
