include (CTest)
set (CMAKE_CTEST_ARGUMENTS "--output-on-failure")
add_test (NAME Prelude
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          COMMAND
            "${CMAKE_BINARY_DIR}/cheax/cheax" -p
              stdlib/prelude.chx
              stdlib/testing.chx
              test/prelude_test.chx)
add_test (NAME Limits
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          COMMAND
            "${CMAKE_BINARY_DIR}/cheax/cheax" -p
              stdlib/prelude.chx
              stdlib/testing.chx
              test/limits_test.chx)
