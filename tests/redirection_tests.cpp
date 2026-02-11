#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "core/command.hpp"
#include "execution/redirection.hpp"

using shell::Redirection;
using shell::RedirectionGuard;
using shell::RedirectionOp;

namespace {

void test_stdout_redirection_roundtrip() {
    const std::string path = std::format("/tmp/shell_redirection_test_{}_out.txt", getpid());

    {
        std::array redirections{Redirection{.op = RedirectionOp::StdoutTruncate, .target = path}};
        RedirectionGuard guard(redirections);
        assert(guard.is_valid());

        std::cout << "hello from test" << std::endl;
        std::cout.flush();
    }

    std::ifstream file(path);
    assert(file.is_open());

    std::stringstream buffer;
    buffer << file.rdbuf();

    assert(buffer.str().find("hello from test") != std::string::npos);
    std::remove(path.c_str());
}

void test_invalid_redirection_path_reports_error() {
    constexpr auto invalid_path = "/no/such/directory/shell-redirection-test.txt";

    std::array redirections{Redirection{.op = RedirectionOp::StdoutTruncate, .target = invalid_path}};
    RedirectionGuard guard(redirections);

    assert(!guard.is_valid());
    assert(!guard.error().empty());
}

void test_stderr_redirection_and_append() {
    const std::string path = std::format("/tmp/shell_redirection_test_{}_err.txt", getpid());

    {
        std::array redirections{Redirection{.op = RedirectionOp::StderrTruncate, .target = path}};
        RedirectionGuard guard(redirections);
        assert(guard.is_valid());

        std::cerr << "first error line" << std::endl;
        std::cerr.flush();
    }

    {
        std::array redirections{Redirection{.op = RedirectionOp::StderrAppend, .target = path}};
        RedirectionGuard guard(redirections);
        assert(guard.is_valid());

        std::cerr << "second error line" << std::endl;
        std::cerr.flush();
    }

    const std::string content = [&]() {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }();

    assert(content.find("first error line") != std::string::npos);
    assert(content.find("second error line") != std::string::npos);
    std::remove(path.c_str());
}

void test_multiple_redirections_for_same_fd_reuses_saved_backup() {
    const std::string path1 = std::format("/tmp/shell_redirection_test_{}_first.txt", getpid());
    const std::string path2 = std::format("/tmp/shell_redirection_test_{}_second.txt", getpid());

    std::array redirections{
        Redirection{.op = RedirectionOp::StdoutTruncate, .target = path1},
        Redirection{.op = RedirectionOp::StdoutTruncate, .target = path2},
    };

    {
        RedirectionGuard guard(redirections);
        assert(guard.is_valid());
        std::cout << "goes to second file" << std::endl;
        std::cout.flush();
    }

    std::ifstream file1(path1);
    std::ifstream file2(path2);
    std::stringstream buffer1;
    std::stringstream buffer2;
    buffer1 << file1.rdbuf();
    buffer2 << file2.rdbuf();

    assert(buffer1.str().empty());
    assert(buffer2.str().find("goes to second file") != std::string::npos);

    std::remove(path1.c_str());
    std::remove(path2.c_str());
}

void test_invalid_operator_can_fail_while_saving_fd() {
    const std::string path = std::format("/tmp/shell_redirection_test_{}_invalid.txt", getpid());

    const int stdout_backup = dup(STDOUT_FILENO);
    assert(stdout_backup != -1);
    assert(close(STDOUT_FILENO) == 0);

    std::array redirections{Redirection{.op = static_cast<RedirectionOp>(999), .target = path}};
    RedirectionGuard guard(redirections);
    assert(!guard.is_valid());
    assert(guard.error().find("failed to save file descriptor") != std::string::npos);

    assert(dup2(stdout_backup, STDOUT_FILENO) != -1);
    close(stdout_backup);
    std::remove(path.c_str());
}

void test_invalid_operator_defaults_to_stdout_truncate() {
    const std::string path = std::format("/tmp/shell_redirection_test_{}_default.txt", getpid());

    std::array redirections{Redirection{.op = static_cast<RedirectionOp>(999), .target = path}};
    RedirectionGuard guard(redirections);
    assert(guard.is_valid());

    std::cout << "default-op-targets-stdout" << std::endl;
    std::cout.flush();

    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    assert(buffer.str().find("default-op-targets-stdout") != std::string::npos);

    std::remove(path.c_str());
}

void test_dup2_failure_reports_error() {
    const std::string path = std::format("/tmp/shell_redirection_test_{}_dup2_fail.txt", getpid());

    static const shell::RedirectionSyscalls failing_dup2_syscalls{
        .dup_fn = +[](int fd) { return ::dup(fd); },
        .open_fn = +[](const char *target, int flags, unsigned int mode) { return ::open(target, flags, mode); },
        .dup2_fn = +[](int /*old_fd*/, int /*new_fd*/) {
            errno = EBADF;
            return -1;
        },
        .close_fn = +[](int fd) { return ::close(fd); },
    };

    std::array redirections{Redirection{.op = RedirectionOp::StdoutTruncate, .target = path}};
    RedirectionGuard guard(redirections, &failing_dup2_syscalls);

    assert(!guard.is_valid());
    assert(guard.error().find("failed to redirect file descriptor") != std::string::npos);

    std::remove(path.c_str());
}

} // namespace

int main() {
    test_stdout_redirection_roundtrip();
    test_invalid_redirection_path_reports_error();
    test_stderr_redirection_and_append();
    test_multiple_redirections_for_same_fd_reuses_saved_backup();
    test_invalid_operator_can_fail_while_saving_fd();
    test_invalid_operator_defaults_to_stdout_truncate();
    test_dup2_failure_reports_error();

    return 0;
}
