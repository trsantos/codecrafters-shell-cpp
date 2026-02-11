#include <cassert>
#include <string>
#include <vector>

#include "core/parser.hpp"
#include "core/tokenizer.hpp"

using shell::Parser;
using shell::RedirectionOp;
using shell::Tokenizer;

namespace {

void test_tokenizer_quotes_and_pipeline() {
    Tokenizer tokenizer;
    const auto tokens = tokenizer.tokenize(R"(echo "hello world" | wc -c)");

    const std::vector<std::string> expected{"echo", "hello world", "|", "wc", "-c"};
    assert(tokens == expected);
}

void test_parser_extracts_redirections() {
    Tokenizer tokenizer;
    Parser parser;

    const auto tokens = tokenizer.tokenize("echo hi > out.txt 2>> err.txt");
    auto pipeline_result = parser.parse(tokens);

    assert(pipeline_result.has_value());
    const auto &pipeline = pipeline_result.value();

    assert(pipeline.stages.size() == 1);
    const auto &command = pipeline.stages.front();

    assert(command.name == "echo");
    assert(command.args == std::vector<std::string>({"hi"}));
    assert(command.redirections.size() == 2);
    assert(command.redirections[0].op == RedirectionOp::StdoutTruncate);
    assert(command.redirections[0].target == "out.txt");
    assert(command.redirections[1].op == RedirectionOp::StderrAppend);
    assert(command.redirections[1].target == "err.txt");
}

void test_parser_rejects_invalid_syntax() {
    Tokenizer tokenizer;
    Parser parser;

    const auto malformed_redirection = tokenizer.tokenize("echo hi >");
    assert(!parser.parse(malformed_redirection).has_value());

    const auto leading_pipe = tokenizer.tokenize("| echo hi");
    assert(!parser.parse(leading_pipe).has_value());

    const auto trailing_pipe = tokenizer.tokenize("echo hi |");
    assert(!parser.parse(trailing_pipe).has_value());
}

void test_redirection_fd_digits_only_at_token_start() {
    Tokenizer tokenizer;
    Parser parser;

    {
        const auto tokens = tokenizer.tokenize("echo hi1>/tmp/out");
        const std::vector<std::string> expected{"echo", "hi1", ">", "/tmp/out"};
        assert(tokens == expected);

        auto parsed = parser.parse(tokens);
        assert(parsed.has_value());
        const auto &command = parsed->stages.front();
        assert(command.args == std::vector<std::string>({"hi1"}));
        assert(command.redirections.size() == 1);
        assert(command.redirections.front().op == RedirectionOp::StdoutTruncate);
    }

    {
        const auto tokens = tokenizer.tokenize("echo v2>/tmp/out");
        const std::vector<std::string> expected{"echo", "v2", ">", "/tmp/out"};
        assert(tokens == expected);
    }

    {
        const auto tokens = tokenizer.tokenize("echo 2>/tmp/err");
        const std::vector<std::string> expected{"echo", "2>", "/tmp/err"};
        assert(tokens == expected);
    }
}

} // namespace

int main() {
    test_tokenizer_quotes_and_pipeline();
    test_parser_extracts_redirections();
    test_parser_rejects_invalid_syntax();
    test_redirection_fd_digits_only_at_token_start();

    return 0;
}
