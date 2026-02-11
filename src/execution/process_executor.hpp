#pragma once

#include <iosfwd>
#include <sys/types.h>

#include "core/command.hpp"

namespace shell {

class BuiltinRegistry;
class PathResolver;

class ProcessExecutor {
  public:
    explicit ProcessExecutor(const PathResolver &path_resolver);

    int execute_single(const Command &command, BuiltinRegistry &builtin_registry);
    int execute_pipeline(const Pipeline &pipeline, BuiltinRegistry &builtin_registry);

  private:
    const PathResolver &path_resolver_;

    [[nodiscard]] int execute_external(const Command &command) const;
    [[noreturn]] void execute_external_in_child(const Command &command) const;

    [[nodiscard]] static int wait_for_process(pid_t pid);
    [[nodiscard]] static int wait_status_to_exit_code(int status);
};

} // namespace shell
