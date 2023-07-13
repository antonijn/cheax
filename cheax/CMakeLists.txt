cmake_minimum_required (VERSION 3.10)

include (GNUInstallDirs)

set (CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR})
add_executable (cheax cheax.c)
target_link_libraries (cheax libcheax)

install (TARGETS cheax DESTINATION ${CMAKE_INSTALL_BINDIR})
install (FILES COPYING DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cheax/)
