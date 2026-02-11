#pragma once

#include <string>
#include <vector>

namespace shell {

enum class RedirectionOp {
    StdoutTruncate,
    StdoutAppend,
    StderrTruncate,
    StderrAppend,
};

struct Redirection {
    RedirectionOp op;
    std::string target;
};

struct Command {
    std::string name;
    std::vector<std::string> args;
    std::vector<Redirection> redirections;
};

struct Pipeline {
    std::vector<Command> stages;

    [[nodiscard]] bool empty() const noexcept { return stages.empty(); }
};

} // namespace shell
