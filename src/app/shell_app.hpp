#pragma once

#include "builtins/builtin_registry.hpp"
#include "core/parser.hpp"
#include "core/path_resolver.hpp"
#include "core/tokenizer.hpp"
#include "execution/process_executor.hpp"
#include "history/history_manager.hpp"
#include "line_editing/completion.hpp"

namespace shell {

class ShellApp {
  public:
    ShellApp();

    int run();

  private:
    PathResolver path_resolver_;
    HistoryManager history_manager_;
    BuiltinRegistry builtin_registry_;
    CompletionEngine completion_engine_;
    Tokenizer tokenizer_;
    Parser parser_;
    ProcessExecutor process_executor_;
};

} // namespace shell
