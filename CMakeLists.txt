cmake_minimum_required (VERSION 3.10)
project (cheax)

option (BUILD_SHARED_LIBS "Build shared libraries" ON)

add_subdirectory (cheaky)
add_subdirectory (cheax)
add_subdirectory (libcheax)
