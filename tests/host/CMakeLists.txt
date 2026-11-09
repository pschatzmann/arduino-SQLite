# Host-side tests for the SQLite Arduino library.
#
# These tests compile the SAME sources that ship in src/ (the generated
# sqlite3.c with its config header, and the VFS) for the host, so they check
# the library as it will run on a board, minus the hardware.
#
#   cmake -S tests/host -B build/host
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
#
# TinyFATFS (https://github.com/pschatzmann/TinyFATFS) is needed for the
# FAT backend test. Set FATFS_DIR if it is not next to this repository.

cmake_minimum_required(VERSION 3.16)
project(arduino_sqlite_host_tests C CXX)

set(LIB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(FATFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../TinyFATFS" CACHE PATH
    "TinyFATFS checkout (provides the arduino_fatfs interface target)")

# SQLite core + VFS. The generated sqlite3.c is vendored, so its warnings are
# not promoted to errors; our own VFS is compiled with -Werror.
add_library(sqlite_lib STATIC
  "${LIB_SRC}/sqlite3.c"
  "${LIB_SRC}/sqlite_vfs.cpp"
  "${LIB_SRC}/sqlite_memory.c"
  "${LIB_SRC}/SQLite.cpp")
target_include_directories(sqlite_lib PUBLIC "${LIB_SRC}")
target_compile_definitions(sqlite_lib PRIVATE _POSIX_C_SOURCE=200809L)
set_source_files_properties("${LIB_SRC}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "-w")
target_compile_options(sqlite_lib PRIVATE -Wall -Wextra -Werror)

find_library(MATH_LIB m)

# C-API tests (default stdio backend).
add_executable(test_sqlite test_sqlite.c)
target_compile_options(test_sqlite PRIVATE -Wall -Wextra -Werror -std=c99)
target_link_libraries(test_sqlite PRIVATE sqlite_lib)
if(MATH_LIB)
  target_link_libraries(test_sqlite PRIVATE ${MATH_LIB})
endif()

enable_testing()
add_test(NAME sqlite_host_tests COMMAND test_sqlite)

# PSRAM allocation policy (uses a mock external-RAM heap on the host).
add_executable(test_memory test_memory.c)
target_compile_options(test_memory PRIVATE -Wall -Wextra -Werror -std=c99)
target_link_libraries(test_memory PRIVATE sqlite_lib)
if(MATH_LIB)
  target_link_libraries(test_memory PRIVATE ${MATH_LIB})
endif()
add_test(NAME sqlite_memory_tests COMMAND test_memory)

# C++ wrapper (SQLiteCpp-style API).
add_executable(test_wrapper test_wrapper.cpp)
target_compile_options(test_wrapper PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_wrapper PRIVATE sqlite_lib)
if(MATH_LIB)
  target_link_libraries(test_wrapper PRIVATE ${MATH_LIB})
endif()
add_test(NAME sqlite_wrapper_tests COMMAND test_wrapper)

# TinyFATFS backend tests.
if(EXISTS "${FATFS_DIR}/CMakeLists.txt")
  set(FATFS_EXAMPES OFF CACHE BOOL "" FORCE)
  set(FATFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FATFS_SANITIZE OFF CACHE BOOL "" FORCE)
  add_subdirectory("${FATFS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tinyfatfs")

  add_executable(test_tinyfatfs test_tinyfatfs.cpp)
# TinyFATFS headers are third-party and not warning-clean under -Wextra, so
  # -Werror is not applied to this target (our own VFS is still -Werror above).
  target_compile_options(test_tinyfatfs PRIVATE -Wall)
  target_link_libraries(test_tinyfatfs PRIVATE sqlite_lib arduino_fatfs)
  if(MATH_LIB)
    target_link_libraries(test_tinyfatfs PRIVATE ${MATH_LIB})
  endif()
  add_test(NAME sqlite_tinyfatfs_tests COMMAND test_tinyfatfs)
else()
  message(STATUS "TinyFATFS not found at ${FATFS_DIR}: skipping FAT backend tests")
endif()
