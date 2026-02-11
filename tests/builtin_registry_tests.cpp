#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <readline/history.h>
#include <unistd.h>

#include "builtins/builtin_registry.hpp"
#include "core/path_resolver.hpp"
#include "history/history_manager.hpp"

using shell::BuiltinRegistry;
using shell::HistoryManager;
using shell::PathResolver;

namespace {

namespace fs = std::filesystem;
thread_local bool fail_next_allocation = false;

} // namespace

void *operator new(std::size_t size) {
    if (fail_next_allocation) {
        fail_next_allocation = false;
        throw std::bad_alloc();
    }

    if (void *memory = std::malloc(size); memory != nullptr) {
        return memory;
    }

    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept { std::free(memory); }

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete[](void *memory) noexcept { ::operator delete(memory); }

namespace {

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

class CurrentPathGuard {
  public:
    CurrentPathGuard() : previous_(fs::current_path()) {}
    ~CurrentPathGuard() {
        std::error_code ec;
        fs::current_path(previous_, ec);
    }

  private:
    fs::path previous_;
};

std::string make_temp_dir() {
    std::string pattern = "/tmp/shell_builtin_registry_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    char *created = mkdtemp(buffer.data());
    assert(created != nullptr);
    return created;
}

std::string make_temp_file(std::string_view initial = "") {
    std::string pattern = "/tmp/shell_builtin_registry_file_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    const int fd = mkstemp(buffer.data());
    assert(fd != -1);
    close(fd);

    if (!initial.empty()) {
        std::ofstream file(buffer.data());
        assert(file.is_open());
        file << initial;
    }

    return buffer.data();
}

std::string slurp(const std::string &path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void make_executable(const fs::path &path) {
    std::error_code ec;
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace,
                    ec);
    assert(!ec);
}

void reset_history() {
    using_history();
    clear_history();
}

void test_registry_lookup_and_dispatch() {
    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);

    assert(registry.is_builtin("echo"));
    assert(!registry.is_builtin("definitely_missing_builtin"));

    std::ostringstream out;
    std::ostringstream err;
    assert(registry.execute("definitely_missing_builtin", {}, out, err) == 1);

    const auto names = registry.names();
    assert(names.contains("cd"));
    assert(names.contains("echo"));
    assert(names.contains("exit"));
    assert(names.contains("history"));
    assert(names.contains("pwd"));
    assert(names.contains("type"));
}

void test_cd_echo_pwd_and_exit() {
    EnvVarGuard home_guard("HOME");
    CurrentPathGuard cwd_guard;

    const std::string home_dir = make_temp_dir();
    const std::string other_dir = make_temp_dir();
    setenv("HOME", home_dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);

    std::ostringstream out;
    std::ostringstream err;

    assert(registry.execute("cd", {other_dir}, out, err) == 0);
    assert(fs::current_path() == fs::path(other_dir));

    assert(registry.execute("cd", {}, out, err) == 0);
    assert(fs::current_path() == fs::path(home_dir));

    const auto cd_fail_before = out.str();
    assert(registry.execute("cd", {"/definitely/no/such/dir"}, out, err) == 1);
    assert(out.str().size() > cd_fail_before.size());

    out.str("");
    out.clear();
    assert(registry.execute("echo", {"one", "two"}, out, err) == 0);
    assert(out.str() == "one two\n");

    out.str("");
    out.clear();
    assert(registry.execute("pwd", {}, out, err) == 0);
    assert(out.str() == fs::current_path().string() + "\n");

    assert(!registry.exit_requested());
    assert(registry.execute("exit", {"1"}, out, err) == 0);
    assert(!registry.exit_requested());

    assert(registry.execute("exit", {}, out, err) == 0);
    assert(registry.exit_requested());

    std::error_code ec;
    fs::remove_all(home_dir, ec);
    fs::remove_all(other_dir, ec);
}

void test_type_builtin_for_all_branches() {
    EnvVarGuard path_guard("PATH");

    const std::string dir = make_temp_dir();
    const fs::path exe = fs::path(dir) / "custom_type_exe";

    {
        std::ofstream file(exe);
        assert(file.is_open());
        file << "#!/bin/sh\necho ok\n";
    }

    make_executable(exe);
    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);

    std::ostringstream out;
    std::ostringstream err;

    assert(registry.execute("type", {}, out, err) == 1);
    assert(err.str().find("missing argument") != std::string::npos);

    err.str("");
    err.clear();
    out.str("");
    out.clear();
    assert(registry.execute("type", {"echo"}, out, err) == 0);
    assert(out.str().find("echo is a shell builtin") != std::string::npos);

    out.str("");
    out.clear();
    assert(registry.execute("type", {"custom_type_exe"}, out, err) == 0);
    assert(out.str().find(exe.string()) != std::string::npos);

    out.str("");
    out.clear();
    assert(registry.execute("type", {"missing_type_exe"}, out, err) == 1);
    assert(out.str().find("missing_type_exe: not found") != std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_history_builtin_variants() {
    EnvVarGuard histfile_guard("HISTFILE");

    const std::string histfile = make_temp_file();
    setenv("HISTFILE", histfile.c_str(), 1);

    reset_history();

    PathResolver resolver;
    HistoryManager history_manager;
    history_manager.initialize();
    BuiltinRegistry registry(resolver, history_manager);

    std::ostringstream out;
    std::ostringstream err;

    add_history("echo one");
    add_history("echo two");

    assert(registry.execute("history", {"-r"}, out, err) == 1);
    assert(err.str().find("-r requires a file argument") != std::string::npos);

    err.str("");
    err.clear();
    assert(registry.execute("history", {"-w"}, out, err) == 1);
    assert(err.str().find("-w requires a file argument") != std::string::npos);

    err.str("");
    err.clear();
    assert(registry.execute("history", {"-a"}, out, err) == 1);
    assert(err.str().find("-a requires a file argument") != std::string::npos);

    const std::string read_file = make_temp_file("echo from_file\n");
    assert(registry.execute("history", {"-r", read_file}, out, err) == 0);
    assert(std::string(history_get(history_length)->line) == "echo from_file");

    const std::string write_file = make_temp_file();
    assert(registry.execute("history", {"-w", write_file}, out, err) == 0);
    assert(slurp(write_file).find("echo one") != std::string::npos);

    const std::string append_file = make_temp_file();
    assert(registry.execute("history", {"-a", append_file}, out, err) == 0);
    const auto appended = slurp(append_file);
    assert(appended.find("echo one") != std::string::npos || appended.find("echo two") != std::string::npos);

    out.str("");
    out.clear();
    assert(registry.execute("history", {"2"}, out, err) == 0);
    assert(out.str().find("history 2") != std::string::npos || out.str().find("echo from_file") != std::string::npos);

    err.str("");
    err.clear();
    assert(registry.execute("history", {"invalid"}, out, err) == 1);
    assert(err.str().find("invalid numeric argument") != std::string::npos);

    out.str("");
    out.clear();
    assert(registry.execute("history", {}, out, err) == 0);
    assert(!out.str().empty());

    std::error_code ec;
    fs::remove(histfile, ec);
    fs::remove(read_file, ec);
    fs::remove(write_file, ec);
    fs::remove(append_file, ec);
}

void test_names_handles_allocation_failure_path() {
    PathResolver resolver;
    HistoryManager history_manager;
    BuiltinRegistry registry(resolver, history_manager);

    bool threw = false;
    fail_next_allocation = true;
    try {
        (void)registry.names();
    } catch (const std::bad_alloc &) {
        threw = true;
    }

    assert(threw);
}

} // namespace

int main() {
    test_registry_lookup_and_dispatch();
    test_cd_echo_pwd_and_exit();
    test_type_builtin_for_all_branches();
    test_history_builtin_variants();
    test_names_handles_allocation_failure_path();

    return 0;
}
