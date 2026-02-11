#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <readline/history.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define private public
#include "execution/process_executor.hpp"
#undef private

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"
#include "history/history_manager.hpp"

using shell::BuiltinRegistry;
using shell::Command;
using shell::HistoryManager;
using shell::PathResolver;
using shell::Pipeline;
using shell::ProcessExecutor;
using shell::Redirection;
using shell::RedirectionOp;

namespace {

namespace fs = std::filesystem;

class EnvVarGuard {
  public:
    explicit EnvVarGuard(const char *name) : name_(name) {
        const char *value = std::getenv(name_.c_str());
        if (value != nullptr) {
            had_value_ = true;
            value_ = value;
        }
    }

    ~EnvVarGuard() {
        if (had_value_) {
            setenv(name_.c_str(), value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    bool had_value_{false};
    std::string value_;
};

std::string make_temp_dir() {
    std::string pattern = "/tmp/shell_process_executor_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    char *created = mkdtemp(buffer.data());
    assert(created != nullptr);
    return created;
}

std::string make_temp_file() {
    std::string pattern = "/tmp/shell_process_executor_file_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    const int fd = mkstemp(buffer.data());
    assert(fd != -1);
    close(fd);

    return buffer.data();
}

std::string slurp(const std::string &path) {
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

void make_executable_script(const fs::path &path, std::string_view body) {
    std::ofstream file(path);
    assert(file.is_open());
    file << body;
    file.close();

    std::error_code ec;
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace,
                    ec);
    assert(!ec);
}

class FdCapture {
  public:
    explicit FdCapture(int fd) : fd_(fd), path_(make_temp_file()) {
        backup_fd_ = dup(fd_);
        assert(backup_fd_ != -1);

        redirected_fd_ = open(path_.c_str(), O_WRONLY | O_TRUNC);
        assert(redirected_fd_ != -1);
        assert(dup2(redirected_fd_, fd_) != -1);
    }

    ~FdCapture() {
        fsync(redirected_fd_);
        close(redirected_fd_);
        dup2(backup_fd_, fd_);
        close(backup_fd_);
    }

    [[nodiscard]] std::string content() const { return slurp(path_); }

  private:
    int fd_;
    int backup_fd_;
    int redirected_fd_;
    std::string path_;
};

void reap_all_children() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
}

void test_execute_single_paths() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    const fs::path exe = fs::path(dir) / "ext_echo";
    const fs::path broken_exe = fs::path(dir) / "broken_exec";
    make_executable_script(exe, "#!/bin/sh\necho external:$1\n");
    make_executable_script(broken_exe, "#!/definitely/missing/interpreter\n");

    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry builtins(resolver, history_manager);
    ProcessExecutor executor(resolver);

    {
        FdCapture stdout_capture(STDOUT_FILENO);
        Command command{.name = "echo", .args = {"builtin"}, .redirections = {}};
        assert(executor.execute_single(command, builtins) == 0);
        assert(stdout_capture.content().find("builtin") != std::string::npos);
    }

    {
        FdCapture stdout_capture(STDOUT_FILENO);
        Command command{.name = "missing_external_cmd", .args = {}, .redirections = {}};
        assert(executor.execute_single(command, builtins) == 127);
        assert(stdout_capture.content().find("command not found") != std::string::npos);
    }

    {
        FdCapture stdout_capture(STDOUT_FILENO);
        Command command{.name = "ext_echo", .args = {"ok"}, .redirections = {}};
        assert(executor.execute_single(command, builtins) == 0);
        assert(stdout_capture.content().find("external:ok") != std::string::npos);
    }

    {
        FdCapture stderr_capture(STDERR_FILENO);
        Command command{.name = "broken_exec", .args = {"arg1", "arg2"}, .redirections = {}};
        assert(executor.execute_single(command, builtins) == 1);
        assert(stderr_capture.content().find("exec failed") != std::string::npos);
    }

    {
        FdCapture stderr_capture(STDERR_FILENO);
        Command command{.name = "echo",
                        .args = {"oops"},
                        .redirections = {{.op = RedirectionOp::StdoutTruncate, .target = "/no/such/dir/out.txt"}}};
        assert(executor.execute_single(command, builtins) == 1);
        assert(stderr_capture.content().find("failed to open") != std::string::npos);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_execute_pipeline_paths() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    const fs::path exe = fs::path(dir) / "ext_pass";
    const fs::path broken_exe = fs::path(dir) / "broken_exec";
    make_executable_script(exe, "#!/bin/sh\ncat\n");
    make_executable_script(broken_exe, "#!/definitely/missing/interpreter\n");

    const char *current_path = std::getenv("PATH");
    const std::string path_env =
        current_path != nullptr ? dir + ":" + std::string(current_path) : dir;
    setenv("PATH", path_env.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry builtins(resolver, history_manager);
    ProcessExecutor executor(resolver);

    Pipeline empty;
    assert(executor.execute_pipeline(empty, builtins) == 0);

    {
        const std::string output_file = make_temp_file();

        Pipeline pipeline;
        pipeline.stages.push_back(Command{.name = "echo", .args = {"hello"}, .redirections = {}});
        pipeline.stages.push_back(
            Command{.name = "wc", .args = {"-c"}, .redirections = {{.op = RedirectionOp::StdoutTruncate, .target = output_file}}});

        assert(executor.execute_pipeline(pipeline, builtins) == 0);
        assert(slurp(output_file).find('6') != std::string::npos);

        std::error_code ec;
        fs::remove(output_file, ec);
    }

    {
        FdCapture stdout_capture(STDOUT_FILENO);
        Pipeline pipeline;
        pipeline.stages.push_back(Command{.name = "definitely_missing_pipeline_cmd", .args = {}, .redirections = {}});
        assert(executor.execute_pipeline(pipeline, builtins) == 127);
        assert(stdout_capture.content().find("command not found") != std::string::npos);
    }

    {
        FdCapture stderr_capture(STDERR_FILENO);
        Pipeline pipeline;
        pipeline.stages.push_back(Command{.name = "echo",
                                          .args = {"boom"},
                                          .redirections = {{.op = RedirectionOp::StdoutTruncate,
                                                            .target = "/no/such/dir/pipeline-out.txt"}}});
        assert(executor.execute_pipeline(pipeline, builtins) == 1);
        assert(stderr_capture.content().find("failed to open") != std::string::npos);
    }

    {
        FdCapture stderr_capture(STDERR_FILENO);
        Pipeline pipeline;
        pipeline.stages.push_back(Command{.name = "echo", .args = {"input"}, .redirections = {}});
        pipeline.stages.push_back(Command{.name = "broken_exec", .args = {"arg"}, .redirections = {}});
        assert(executor.execute_pipeline(pipeline, builtins) == 1);
        assert(stderr_capture.content().find("exec failed") != std::string::npos);
    }

    {
        struct rlimit old_nofile {};
        assert(getrlimit(RLIMIT_NOFILE, &old_nofile) == 0);
        struct rlimit constrained_nofile = old_nofile;
        constrained_nofile.rlim_cur = std::min<rlim_t>(old_nofile.rlim_cur, 64);
        assert(setrlimit(RLIMIT_NOFILE, &constrained_nofile) == 0);

        Pipeline huge;
        huge.stages.resize(2048);
        for (auto &stage : huge.stages) {
            stage.name = "echo";
            stage.args = {"x"};
        }

        bool threw = false;
        try {
            (void)executor.execute_pipeline(huge, builtins);
        } catch (const std::runtime_error &error) {
            threw = std::string(error.what()).find("pipe failed") != std::string::npos;
        }
        assert(setrlimit(RLIMIT_NOFILE, &old_nofile) == 0);
        assert(threw);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_private_process_helpers() {
    PathResolver resolver;
    ProcessExecutor executor(resolver);

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            _exit(7);
        }

        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        assert(ProcessExecutor::wait_status_to_exit_code(status) == 7);
    }

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            raise(SIGTERM);
            _exit(0);
        }

        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        assert(ProcessExecutor::wait_status_to_exit_code(status) == 128 + SIGTERM);
    }

    assert(ProcessExecutor::wait_status_to_exit_code(0x7f) == 1);

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            sleep(2);
            _exit(0);
        }

        struct sigaction action {};
        action.sa_handler = +[](int) {};
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGALRM, &action, nullptr);

        alarm(1);
        assert(ProcessExecutor::wait_for_process(pid) == 0);
        alarm(0);
    }

    reap_all_children();
    bool wait_threw = false;
    try {
        (void)ProcessExecutor::wait_for_process(-1);
    } catch (const std::runtime_error &error) {
        wait_threw = std::string(error.what()).find("waitpid failed") != std::string::npos;
    }
    assert(wait_threw);

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            Command command{.name = "definitely_missing_execvp_cmd", .args = {}, .redirections = {}};
            executor.execute_external_in_child(command);
        }

        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 1);
    }
}

void test_fork_failure_paths_when_nproc_limit_is_low() {
    struct rlimit old_limit {};
    if (getrlimit(RLIMIT_NPROC, &old_limit) != 0) {
        return;
    }

    struct rlimit constrained_limit = old_limit;
    constrained_limit.rlim_cur = 0;
    if (setrlimit(RLIMIT_NPROC, &constrained_limit) != 0) {
        return;
    }

    PathResolver resolver;
    ProcessExecutor executor(resolver);

    bool external_throw = false;
    try {
        Command command{.name = "true", .args = {}, .redirections = {}};
        (void)executor.execute_external(command);
    } catch (const std::runtime_error &error) {
        external_throw = std::string(error.what()).find("fork failed") != std::string::npos;
    }

    HistoryManager history_manager;
    BuiltinRegistry builtins(resolver, history_manager);

    bool pipeline_throw = false;
    try {
        Pipeline pipeline;
        pipeline.stages.push_back(Command{.name = "echo", .args = {"x"}, .redirections = {}});
        (void)executor.execute_pipeline(pipeline, builtins);
    } catch (const std::runtime_error &error) {
        pipeline_throw = std::string(error.what()).find("fork failed") != std::string::npos;
    }

    (void)setrlimit(RLIMIT_NPROC, &old_limit);

    assert(external_throw || pipeline_throw);
}

} // namespace

int main() {
    using_history();
    clear_history();

    test_execute_single_paths();
    test_execute_pipeline_paths();
    test_private_process_helpers();
    test_fork_failure_paths_when_nproc_limit_is_low();

    return 0;
}
