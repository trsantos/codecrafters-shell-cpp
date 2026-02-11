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

add_executable(parser_tests tests/parser_tests.cpp)
target_include_directories(parser_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(parser_tests PRIVATE shell_core)
add_test(NAME parser_tests COMMAND parser_tests)

add_executable(redirection_tests tests/redirection_tests.cpp)
target_include_directories(redirection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(redirection_tests PRIVATE shell_core)
add_test(NAME redirection_tests COMMAND redirection_tests)

add_executable(path_resolver_tests tests/path_resolver_tests.cpp)
target_include_directories(path_resolver_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(path_resolver_tests PRIVATE shell_core)
add_test(NAME path_resolver_tests COMMAND path_resolver_tests)

add_executable(history_manager_tests tests/history_manager_tests.cpp)
target_include_directories(history_manager_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(history_manager_tests PRIVATE shell_core)
add_test(NAME history_manager_tests COMMAND history_manager_tests)

add_executable(builtin_registry_tests tests/builtin_registry_tests.cpp)
target_include_directories(builtin_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(builtin_registry_tests PRIVATE shell_core)
add_test(NAME builtin_registry_tests COMMAND builtin_registry_tests)

add_executable(completion_tests tests/completion_tests.cpp)
target_include_directories(completion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(completion_tests PRIVATE shell_core)
add_test(NAME completion_tests COMMAND completion_tests)

add_executable(process_executor_tests tests/process_executor_tests.cpp)
target_include_directories(process_executor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(process_executor_tests PRIVATE shell_core)
add_test(NAME process_executor_tests COMMAND process_executor_tests)

add_executable(exception_coverage_tests tests/exception_coverage_tests.cpp)
target_include_directories(exception_coverage_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(exception_coverage_tests PRIVATE shell_core)
add_test(NAME exception_coverage_tests COMMAND exception_coverage_tests)

add_test(
    NAME shell_repl_eof_test
    COMMAND sh -c
            "printf 'echo eof-check\\n' | HISTFILE=/tmp/shell_cov_hist_eof ./shell >/tmp/shell_cov_eof_out.txt 2>/tmp/shell_cov_eof_err.txt"
)
set_tests_properties(shell_repl_eof_test PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(
    NAME shell_parse_error_test
    COMMAND sh -c
            "printf '|\\nexit\\n' | HISTFILE=/tmp/shell_cov_hist_parse ./shell >/tmp/shell_cov_parse_out.txt 2>/tmp/shell_cov_parse_err.txt"
)
set_tests_properties(shell_parse_error_test PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(
    NAME shell_pipeline_test
    COMMAND sh -c
            "printf '\\necho alpha | wc -c\\nexit\\n' | HISTFILE=/tmp/shell_cov_hist_pipeline ./shell >/tmp/shell_cov_pipeline_out.txt 2>/tmp/shell_cov_pipeline_err.txt"
)
set_tests_properties(shell_pipeline_test PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
