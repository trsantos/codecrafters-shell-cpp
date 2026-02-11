#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#define private public
#include "execution/process_executor.hpp"
#include "line_editing/completion.hpp"
#undef private

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"
#include "execution/redirection.hpp"
#include "history/history_manager.hpp"

using shell::BuiltinRegistry;
using shell::Command;
using shell::CompletionEngine;
using shell::HistoryManager;
using shell::PathResolver;
using shell::ProcessExecutor;
using shell::Redirection;
using shell::RedirectionGuard;
using shell::RedirectionOp;

namespace {

namespace fs = std::filesystem;

thread_local int fail_after_allocations = -1;

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
    std::string pattern = "/tmp/shell_exception_coverage_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    char *created = mkdtemp(buffer.data());
    assert(created != nullptr);
    return created;
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

template <typename Fn>
void expect_bad_alloc(Fn &&fn, int max_attempts = 64) {
    for (int attempt = 0; attempt <= max_attempts; ++attempt) {
        fail_after_allocations = attempt;
        try {
            fn();
        } catch (const std::bad_alloc &) {
            fail_after_allocations = -1;
            return;
        }
    }

    fail_after_allocations = -1;
    assert(false && "expected bad_alloc");
}

void test_path_resolver_exception_edges() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    make_executable_script(fs::path(dir) / "alpha_cmd", "#!/bin/sh\nexit 0\n");
    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    expect_bad_alloc([&]() { (void)resolver.find_command_path("alpha_cmd"); });
    expect_bad_alloc([&]() { (void)resolver.executable_candidates("a"); });

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_completion_exception_edge() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    make_executable_script(fs::path(dir) / "echo_external", "#!/bin/sh\nexit 0\n");
    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);
    CompletionEngine engine(registry, resolver);

    bool handled = false;
    for (int attempt = 0; attempt <= 64; ++attempt) {
        fail_after_allocations = attempt;
        const auto matches = engine.collect_matches("e");
        if (matches.empty()) {
            handled = true;
            break;
        }
    }
    fail_after_allocations = -1;
    assert(handled);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_redirection_constructor_exception_edge() {
    const std::string path = "/tmp/shell_exception_redirection_out.txt";
    std::array redirections{Redirection{.op = RedirectionOp::StdoutTruncate, .target = path}};

    fail_after_allocations = 0;
    bool threw = false;
    try {
        RedirectionGuard guard(redirections);
    } catch (const std::bad_alloc &) {
        threw = true;
    }
    fail_after_allocations = -1;

    assert(threw);
    std::remove(path.c_str());
}

void test_process_executor_exception_edges() {
    PathResolver resolver;
    ProcessExecutor executor(resolver);

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            Command command{.name = "missing_exec_for_alloc_test", .args = {"arg1"}, .redirections = {}};
            fail_after_allocations = 0;
            executor.execute_external_in_child(command);
        }

        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 1);
    }

    {
        const pid_t pid = fork();
        assert(pid != -1);
        if (pid == 0) {
            HistoryManager history_manager;
            BuiltinRegistry builtins(resolver, history_manager);
            Command command{
                .name = "echo",
                .args = {"x"},
                .redirections = {{.op = RedirectionOp::StdoutTruncate, .target = "/tmp/shell_exception_pipe_out.txt"}}};
            fail_after_allocations = 0;
            executor.execute_pipeline_stage_in_child(command, 0, 1, std::span<const int>{}, builtins);
        }

        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 1);
    }

    std::remove("/tmp/shell_exception_pipe_out.txt");
}

} // namespace

void *operator new(std::size_t size) {
    if (fail_after_allocations == 0) {
        throw std::bad_alloc();
    }

    if (fail_after_allocations > 0) {
        --fail_after_allocations;
    }

    if (void *memory = std::malloc(size); memory != nullptr) {
        return memory;
    }

    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept { std::free(memory); }

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete[](void *memory) noexcept { ::operator delete(memory); }

int main() {
    test_path_resolver_exception_edges();
    test_completion_exception_edge();
    test_redirection_constructor_exception_edge();
    test_process_executor_exception_edges();
    return 0;
}
