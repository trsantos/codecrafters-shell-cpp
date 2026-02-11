#pragma once

#include <set>
#include <string>

namespace shell {

class BuiltinRegistry;
class PathResolver;

class CompletionEngine {
  public:
    CompletionEngine(const BuiltinRegistry &builtin_registry, const PathResolver &path_resolver);

    void install();

  private:
    const BuiltinRegistry &builtin_registry_;
    const PathResolver &path_resolver_;

    static CompletionEngine *instance_;

    static char **completion_callback(const char *text, int start, int end);
    static char *generator_callback(const char *text, int state);

    [[nodiscard]] std::set<std::string> collect_matches(const std::string &prefix) const;
};

} // namespace shell
