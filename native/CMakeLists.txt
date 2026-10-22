# Bole Agent SDK — C library (deliverables: bole_agent.dll/.lib/.h).
# The core is platform-neutral except src/platform_*.c, so POSIX runs the same
# contract tests locally while Windows ships the DLL.
#
# Build (POSIX):
#   cmake -S native -B build/native -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/native -j4
#   ctest --test-dir build/native --output-on-failure
#
# Build (Windows x64, MSVC static runtime):
#   cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
#   cmake --build native/build --config Release

cmake_minimum_required(VERSION 3.20)
project(bole_agent C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

set(BOLE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(BOLE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(BOLE_CORE
  ${BOLE_SOURCE_DIR}/agent.c
  ${BOLE_SOURCE_DIR}/error.c
  ${BOLE_SOURCE_DIR}/json.c
  ${BOLE_SOURCE_DIR}/rpc.c
  ${BOLE_SOURCE_DIR}/transport.c)

if(WIN32)
  if(NOT EXISTS "${BOLE_SOURCE_DIR}/platform_win32.c")
    message(FATAL_ERROR "src/platform_win32.c is required for the Windows build")
  endif()
  list(APPEND BOLE_CORE ${BOLE_SOURCE_DIR}/platform_win32.c)
else()
  list(APPEND BOLE_CORE ${BOLE_SOURCE_DIR}/platform_posix.c)
endif()

add_library(bole_agent SHARED ${BOLE_CORE})
target_include_directories(bole_agent
  PUBLIC  ${BOLE_INCLUDE_DIR}
  PRIVATE ${BOLE_SOURCE_DIR})
target_compile_definitions(bole_agent PRIVATE BOLE_AGENT_BUILD)
if(WIN32)
  set_target_properties(bole_agent PROPERTIES PREFIX "" OUTPUT_NAME "bole_agent")
else()
  set_target_properties(bole_agent PROPERTIES C_VISIBILITY_PRESET hidden)
  find_package(Threads REQUIRED)
  target_link_libraries(bole_agent PRIVATE Threads::Threads)
endif()

if(MSVC)
  target_compile_options(bole_agent PRIVATE /W4)
else()
  target_compile_options(bole_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS bole_agent
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${BOLE_INCLUDE_DIR}/bole_agent.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

enable_testing()

add_executable(bole_test_json tests/test_json.c ${BOLE_SOURCE_DIR}/json.c)
target_include_directories(bole_test_json PRIVATE ${BOLE_SOURCE_DIR})
target_compile_definitions(bole_test_json PRIVATE BOLE_AGENT_STATIC)

add_executable(bole_test_error tests/test_error.c ${BOLE_SOURCE_DIR}/error.c)
target_include_directories(bole_test_error PRIVATE
  ${BOLE_INCLUDE_DIR} ${BOLE_SOURCE_DIR})
target_compile_definitions(bole_test_error PRIVATE BOLE_AGENT_STATIC)

add_test(NAME json  COMMAND bole_test_json)
add_test(NAME error COMMAND bole_test_error)

# The end-to-end contract test uses POSIX temp directories, getline, and a
# shell wrapper for node.exe; Windows bring-up validates the DLL separately.
if(NOT WIN32)
  add_executable(bole_fake_runtime tests/fake_runtime.c ${BOLE_SOURCE_DIR}/json.c)
  target_include_directories(bole_fake_runtime PRIVATE ${BOLE_SOURCE_DIR})
  target_compile_definitions(bole_fake_runtime PRIVATE BOLE_AGENT_STATIC)

  add_executable(bole_test_agent tests/test_agent.c ${BOLE_CORE})
  target_include_directories(bole_test_agent PRIVATE
    ${BOLE_INCLUDE_DIR} ${BOLE_SOURCE_DIR})
  target_compile_definitions(bole_test_agent PRIVATE
    BOLE_AGENT_STATIC
    FAKE_RUNTIME_PATH="$<TARGET_FILE:bole_fake_runtime>")
  target_link_libraries(bole_test_agent PRIVATE Threads::Threads)
  add_test(NAME agent COMMAND bole_test_agent)
endif()

find_program(BOLE_NODE_EXECUTABLE node)
if(BOLE_NODE_EXECUTABLE)
  add_test(NAME fixtures
    COMMAND ${BOLE_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/validate.mjs)
endif()
