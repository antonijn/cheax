set (DOXYGEN_MAIN_PAGE "${CMAKE_SOURCE_DIR}/README.md")

set (input_files
	"${DOXYGEN_MAIN_PAGE}"
	"${CMAKE_SOURCE_DIR}/libcheax/include/cheax/cheax.h")

foreach (f IN LISTS input_files)
	string (APPEND DOXYGEN_INPUT " \\\n\t\"${f}\"")
endforeach ()

configure_file (Doxyfile.in "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)

add_custom_command (OUTPUT "html/index.html"
	COMMAND "${DOXYGEN_EXECUTABLE}"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	MAIN_DEPENDENCY Doxyfile
	DEPENDS ${input_files}
	COMMENT "Generating HTML documentation"
	VERBATIM)

add_custom_target (docs ALL SOURCES "html/index.html")
