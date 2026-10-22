# Unauthorized prototype, not a deliverable. The B-track owner decides whether to use it.
cmake_minimum_required(VERSION 3.20)
project(bole_agent_sdk_prototype C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(BOLE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../native/include")

set(BOLE_CORE
  src/agent.c
  src/error.c
  src/json.c
  src/rpc.c
  src/transport.c)

if(WIN32)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform_win32.c")
    message(FATAL_ERROR "Windows platform layer (src/platform_win32.c) is not implemented yet")
  endif()
  list(APPEND BOLE_CORE src/platform_win32.c)
else()
  list(APPEND BOLE_CORE src/platform_posix.c)
endif()

add_library(bole_agent_prototype SHARED ${BOLE_CORE})
target_include_directories(bole_agent_prototype
  PUBLIC  ${BOLE_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(bole_agent_prototype PRIVATE BOLE_AGENT_BUILD)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(bole_agent_prototype PRIVATE Threads::Threads)
endif()

enable_testing()

add_executable(bole_fake_runtime tests/fake_runtime.c src/json.c)
target_include_directories(bole_fake_runtime PRIVATE src)
target_compile_definitions(bole_fake_runtime PRIVATE BOLE_AGENT_STATIC)

add_executable(bole_test_json tests/test_json.c src/json.c)
target_include_directories(bole_test_json PRIVATE src)
target_compile_definitions(bole_test_json PRIVATE BOLE_AGENT_STATIC)

add_executable(bole_test_agent tests/test_agent.c ${BOLE_CORE})
target_include_directories(bole_test_agent PRIVATE ${BOLE_INCLUDE_DIR} src)
target_compile_definitions(bole_test_agent PRIVATE
  BOLE_AGENT_STATIC
  FAKE_RUNTIME_PATH="$<TARGET_FILE:bole_fake_runtime>")
if(NOT WIN32)
  target_link_libraries(bole_test_agent PRIVATE Threads::Threads)
endif()

add_test(NAME json COMMAND bole_test_json)
add_test(NAME agent COMMAND bole_test_agent)
