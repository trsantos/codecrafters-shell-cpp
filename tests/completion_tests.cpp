#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <readline/readline.h>

#define private public
#include "line_editing/completion.hpp"
#undef private

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"
#include "history/history_manager.hpp"

using shell::BuiltinRegistry;
using shell::CompletionEngine;
using shell::HistoryManager;
using shell::PathResolver;

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
    std::string pattern = "/tmp/shell_completion_tests_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    char *created = mkdtemp(buffer.data());
    assert(created != nullptr);
    return created;
}

void make_executable(const fs::path &path) {
    std::ofstream file(path);
    assert(file.is_open());
    file << "#!/bin/sh\nexit 0\n";
    file.close();

    std::error_code ec;
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace,
                    ec);
    assert(!ec);
}

void free_completion_matches(char **matches) {
    if (matches == nullptr) {
        return;
    }

    for (std::size_t i = 0; matches[i] != nullptr; ++i) {
        std::free(matches[i]);
    }
    std::free(matches);
}

void test_collect_matches_and_generator() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    const fs::path exe = fs::path(dir) / "ec_custom_exe";
    make_executable(exe);

    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);
    CompletionEngine engine(registry, resolver);

    auto matches = engine.collect_matches("ec");
    assert(matches.contains("echo"));
    assert(matches.contains("ec_custom_exe"));

    CompletionEngine::instance_ = nullptr;
    assert(CompletionEngine::generator_callback("ec", 0) == nullptr);

    engine.install();
    char *first = CompletionEngine::generator_callback("ec", 0);
    assert(first != nullptr);
    std::free(first);

    for (;;) {
        char *next = CompletionEngine::generator_callback("ec", 1);
        if (next == nullptr) {
            break;
        }
        std::free(next);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_completion_callback_paths() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    const fs::path exe = fs::path(dir) / "ca_custom_exe";
    make_executable(exe);

    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);
    CompletionEngine engine(registry, resolver);

    engine.install();

    rl_attempted_completion_over = 0;
    char **non_command_position = CompletionEngine::completion_callback("ca", 1, 1);
    assert(rl_attempted_completion_over == 1);
    assert(non_command_position == nullptr);

    rl_attempted_completion_over = 0;
    char **command_position = CompletionEngine::completion_callback("ca", 0, 2);
    assert(rl_attempted_completion_over == 1);
    assert(command_position != nullptr);

    free_completion_matches(command_position);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    test_collect_matches_and_generator();
    test_completion_callback_paths();
    return 0;
}
