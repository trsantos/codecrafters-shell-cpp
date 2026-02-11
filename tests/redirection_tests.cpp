#include <array>
#include <cassert>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

} // namespace

int main() {
    test_stdout_redirection_roundtrip();
    test_invalid_redirection_path_reports_error();

    return 0;
}
