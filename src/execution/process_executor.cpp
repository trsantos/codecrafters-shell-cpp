#include "execution/process_executor.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"
#include "execution/redirection.hpp"

namespace shell {

namespace {

[[nodiscard]] std::vector<char *> build_argv(const Command &command) {
    std::vector<char *> argv;
    argv.reserve(command.args.size() + 2);

    argv.push_back(const_cast<char *>(command.name.c_str()));
    for (const auto &arg : command.args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    return argv;
}

} // namespace

ProcessExecutor::ProcessExecutor(const PathResolver &path_resolver) : path_resolver_(path_resolver) {}

int ProcessExecutor::execute_single(const Command &command, BuiltinRegistry &builtin_registry) {
    RedirectionGuard redirection_guard(command.redirections);
    if (!redirection_guard.is_valid()) {
        std::cerr << redirection_guard.error() << std::endl;
        return 1;
    }

    if (builtin_registry.is_builtin(command.name)) {
        return builtin_registry.execute(command.name, command.args, std::cout, std::cerr);
    }

    if (path_resolver_.find_command_path(command.name).empty()) {
        std::cout << command.name << ": command not found" << std::endl;
        return 127;
    }

    return execute_external(command);
}

int ProcessExecutor::execute_pipeline(const Pipeline &pipeline, BuiltinRegistry &builtin_registry) {
    if (pipeline.stages.empty()) {
        return 0;
    }

    std::vector<int> pipes((pipeline.stages.size() - 1) * 2, -1);
    for (std::size_t i = 0; i + 1 < pipeline.stages.size(); ++i) {
        if (pipe(&pipes[i * 2]) == -1) {
            throw std::runtime_error("pipe failed");
        }
    }

    std::vector<pid_t> pids;
    pids.reserve(pipeline.stages.size());

    for (std::size_t i = 0; i < pipeline.stages.size(); ++i) {
        const auto &command = pipeline.stages[i];

        const pid_t pid = fork();
        if (pid == -1) {
            throw std::runtime_error("fork failed");
        }

        if (pid == 0) {
            if (i > 0) {
                dup2(pipes[(i - 1) * 2], STDIN_FILENO);
            }

            if (i + 1 < pipeline.stages.size()) {
                dup2(pipes[i * 2 + 1], STDOUT_FILENO);
            }

            for (const int fd : pipes) {
                close(fd);
            }

            RedirectionGuard redirection_guard(command.redirections);
            if (!redirection_guard.is_valid()) {
                std::cerr << redirection_guard.error() << std::endl;
                _exit(1);
            }

            if (builtin_registry.is_builtin(command.name)) {
                const int status = builtin_registry.execute(command.name, command.args, std::cout, std::cerr);
                _exit(status);
            }

            if (path_resolver_.find_command_path(command.name).empty()) {
                std::cout << command.name << ": command not found" << std::endl;
                _exit(127);
            }

            execute_external_in_child(command);
        }

        pids.push_back(pid);
    }

    for (const int fd : pipes) {
        close(fd);
    }

    int last_status = 0;
    for (const pid_t pid : pids) {
        last_status = wait_for_process(pid);
    }

    return last_status;
}

int ProcessExecutor::execute_external(const Command &command) const {
    const pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        execute_external_in_child(command);
    }

    return wait_for_process(pid);
}

void ProcessExecutor::execute_external_in_child(const Command &command) const {
    auto argv = build_argv(command);
    execvp(command.name.c_str(), argv.data());
    std::perror("exec failed");
    _exit(1);
}

int ProcessExecutor::wait_for_process(pid_t pid) {
    int status = 0;

    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }

        throw std::runtime_error("waitpid failed");
    }

    return wait_status_to_exit_code(status);
}

int ProcessExecutor::wait_status_to_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

} // namespace shell
