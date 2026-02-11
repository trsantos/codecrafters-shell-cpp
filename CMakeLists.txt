cmake_minimum_required(VERSION 3.13)

project(shell-starter-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SHELL_SOURCES
    src/app/shell_app.cpp
    src/builtins/builtin_registry.cpp
    src/core/parser.cpp
    src/core/path_resolver.cpp
    src/core/tokenizer.cpp
    src/execution/process_executor.cpp
    src/execution/redirection.cpp
    src/history/history_manager.cpp
    src/line_editing/completion.cpp
)

add_library(shell_core STATIC ${SHELL_SOURCES})
target_include_directories(shell_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shell_core PUBLIC readline)

add_executable(shell src/main.cpp)
target_link_libraries(shell PRIVATE shell_core)

enable_testing()

add_executable(parser_tests tests/parser_tests.cpp src/core/parser.cpp src/core/tokenizer.cpp)
target_include_directories(parser_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME parser_tests COMMAND parser_tests)

add_executable(redirection_tests tests/redirection_tests.cpp src/execution/redirection.cpp)
target_include_directories(redirection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME redirection_tests COMMAND redirection_tests)
