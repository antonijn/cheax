# Based on <https://gist.github.com/Tordan/c4d4d14aac0e85ebf7122ed2189ea387>

set (GIT_HASH "unknown")

find_package (Git QUIET)
if (GIT_FOUND)
	execute_process (
		COMMAND git describe --always --dirty
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
endif ()

configure_file (${IN_FILE} ${OUT_FILE})
