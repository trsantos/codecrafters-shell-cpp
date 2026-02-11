#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "core/path_resolver.hpp"

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
    std::string pattern = "/tmp/shell_path_resolver_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    char *created = mkdtemp(buffer.data());
    assert(created != nullptr);
    return created;
}

void write_file(const fs::path &path, std::string_view content) {
    std::ofstream file(path);
    assert(file.is_open());
    file << content;
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

void test_unset_path_returns_no_matches() {
    EnvVarGuard guard("PATH");
    unsetenv("PATH");

    PathResolver resolver;
    assert(resolver.find_command_path("echo").empty());
    assert(resolver.executable_candidates("ec").empty());
}

void test_find_command_path_ignores_non_executables() {
    EnvVarGuard guard("PATH");

    const fs::path dir1 = make_temp_dir();
    const fs::path dir2 = make_temp_dir();

    const fs::path non_executable = dir1 / "my_cmd";
    const fs::path executable = dir2 / "my_cmd";

    write_file(non_executable, "#!/bin/sh\necho nonexec\n");
    write_file(executable, "#!/bin/sh\necho exec\n");

    std::error_code ec;
    fs::permissions(non_executable,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace,
                    ec);
    assert(!ec);

    make_executable(executable);

    const std::string path_env = dir1.string() + ":" + "/definitely/missing/path" + ":" + dir2.string();
    setenv("PATH", path_env.c_str(), 1);

    PathResolver resolver;
    assert(resolver.find_command_path("my_cmd") == executable.string());

    const auto candidates = resolver.executable_candidates("my");
    assert(candidates.contains("my_cmd"));

    fs::remove_all(dir1, ec);
    fs::remove_all(dir2, ec);
}

void test_prefix_filtering_and_callback_early_stop_behavior() {
    EnvVarGuard guard("PATH");

    const fs::path dir = make_temp_dir();
    const fs::path file_a = dir / "alpha";
    const fs::path file_b = dir / "beta";
    const fs::path subdir = dir / "nested";

    write_file(file_a, "#!/bin/sh\nexit 0\n");
    write_file(file_b, "#!/bin/sh\nexit 0\n");
    std::error_code ec;
    fs::create_directory(subdir, ec);
    assert(!ec);

    make_executable(file_a);
    make_executable(file_b);

    setenv("PATH", dir.c_str(), 1);

    PathResolver resolver;
    const auto alpha = resolver.find_command_path("alpha");
    assert(alpha == file_a.string());

    // Exercises callback continuation path when a prefix matches but exact name does not.
    assert(resolver.find_command_path("alp").empty());

    const auto empty = resolver.executable_candidates("zz");
    assert(empty.empty());

    const auto all = resolver.executable_candidates("");
    assert(all.contains("alpha"));
    assert(all.contains("beta"));
    assert(!all.contains("nested"));

    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    test_unset_path_returns_no_matches();
    test_find_command_path_ignores_non_executables();
    test_prefix_filtering_and_callback_early_stop_behavior();
    return 0;
}
