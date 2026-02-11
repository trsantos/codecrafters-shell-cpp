#pragma once

#include <cstddef>
#include <iosfwd>
#include <span>
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
    [[noreturn]] void execute_external_in_child(const Command &command) const noexcept;
    [[noreturn]] void execute_pipeline_stage_in_child(
        const Command &command,
        std::size_t stage_index,
        std::size_t stage_count,
        std::span<const int> pipes,
        BuiltinRegistry &builtin_registry) const noexcept;

    [[nodiscard]] static int wait_for_process(pid_t pid);
    [[nodiscard]] static int wait_status_to_exit_code(int status);
};

} // namespace shell
