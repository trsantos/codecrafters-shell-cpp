#include "line_editing/completion.hpp"

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include <readline/readline.h>

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"

namespace shell {

CompletionEngine *CompletionEngine::instance_ = nullptr;

CompletionEngine::CompletionEngine(const BuiltinRegistry &builtin_registry, const PathResolver &path_resolver)
    : builtin_registry_(builtin_registry), path_resolver_(path_resolver) {}

void CompletionEngine::install() {
    instance_ = this;
    rl_attempted_completion_function = &CompletionEngine::completion_callback;
}

char **CompletionEngine::completion_callback(const char *text, int start, int /*end*/) {
    rl_attempted_completion_over = 1;

    if (instance_ == nullptr || start != 0) {
        return nullptr;
    }

    return rl_completion_matches(text, &CompletionEngine::generator_callback);
}

char *CompletionEngine::generator_callback(const char *text, int state) {
    static std::set<std::string> matches;
    static std::set<std::string>::iterator iterator;

    if (instance_ == nullptr) {
        return nullptr;
    }

    if (state == 0) {
        matches = instance_->collect_matches(text);
        iterator = matches.begin();
    }

    if (iterator == matches.end()) {
        return nullptr;
    }

    return ::strdup((iterator++)->c_str());
}

std::set<std::string> CompletionEngine::collect_matches(const std::string &prefix) const {
    auto matches = path_resolver_.executable_candidates(prefix);

    for (const auto &builtin : builtin_registry_.names()) {
        if (builtin.starts_with(prefix)) {
            matches.insert(builtin);
        }
    }

    return matches;
}

} // namespace shell
