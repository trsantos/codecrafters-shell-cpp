#include "core/parser.hpp"

#include <optional>
#include <utility>

namespace shell {

namespace {

[[nodiscard]] std::optional<RedirectionOp> redirection_from_token(std::string_view token) {
    if (token == ">" || token == "1>") {
        return RedirectionOp::StdoutTruncate;
    }

    if (token == ">>" || token == "1>>") {
        return RedirectionOp::StdoutAppend;
    }

    if (token == "2>") {
        return RedirectionOp::StderrTruncate;
    }

    if (token == "2>>") {
        return RedirectionOp::StderrAppend;
    }

    return std::nullopt;
}

} // namespace

std::expected<Pipeline, ParseError> Parser::parse(std::span<const std::string> tokens) const {
    Pipeline pipeline;
    Command current;
    bool last_token_was_pipe = false;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto &token = tokens[i];

        if (token == "|") {
            if (current.name.empty()) {
                return std::unexpected(ParseError{"syntax error near unexpected token `|'"});
            }

            pipeline.stages.push_back(std::move(current));
            current = Command{};
            last_token_was_pipe = true;
            continue;
        }

        last_token_was_pipe = false;

        if (const auto redirection = redirection_from_token(token); redirection.has_value()) {
            if (current.name.empty()) {
                return std::unexpected(ParseError{"redirection requires a command"});
            }

            if (i + 1 >= tokens.size() || tokens[i + 1] == "|") {
                return std::unexpected(ParseError{"redirection missing target file"});
            }

            current.redirections.push_back(Redirection{.op = *redirection, .target = tokens[i + 1]});
            ++i;
            continue;
        }

        if (current.name.empty()) {
            current.name = token;
        } else {
            current.args.push_back(token);
        }
    }

    if (last_token_was_pipe) {
        return std::unexpected(ParseError{"syntax error near unexpected token `|'"});
    }

    if (!current.name.empty()) {
        pipeline.stages.push_back(std::move(current));
    }

    return pipeline;
}

} // namespace shell
