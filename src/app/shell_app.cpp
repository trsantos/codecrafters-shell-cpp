#include "app/shell_app.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include <readline/readline.h>

namespace shell {

ShellApp::ShellApp()
    : path_resolver_(),
      history_manager_(),
      builtin_registry_(path_resolver_, history_manager_),
      completion_engine_(builtin_registry_, path_resolver_),
      tokenizer_(),
      parser_(),
      process_executor_(path_resolver_) {}

int ShellApp::run() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    completion_engine_.install();
    history_manager_.initialize();

    while (true) {
        char *line = readline("$ ");
        if (line == nullptr) {
            std::cout << std::endl;
            break;
        }

        std::string input(line);
        std::free(line);

        history_manager_.record_input(input);

        const auto tokens = tokenizer_.tokenize(input);
        if (tokens.empty()) {
            continue;
        }

        auto pipeline_result = parser_.parse(tokens);
        if (!pipeline_result.has_value()) {
            std::cerr << pipeline_result.error().message << std::endl;
            continue;
        }

        const Pipeline &pipeline = pipeline_result.value();

        if (pipeline.stages.size() == 1) {
            process_executor_.execute_single(pipeline.stages.front(), builtin_registry_);
        } else {
            process_executor_.execute_pipeline(pipeline, builtin_registry_);
        }

        if (builtin_registry_.exit_requested()) {
            break;
        }
    }

    history_manager_.save();
    return 0;
}

} // namespace shell
