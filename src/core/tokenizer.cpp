#include "core/tokenizer.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace shell {

std::vector<std::string> Tokenizer::tokenize(std::string_view input) const {
    std::vector<std::string> tokens;
    std::string token;

    bool single_quoted = false;
    bool double_quoted = false;
    bool escaped = false;

    auto flush_token = [&]() {
        if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
        }
    };

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char current = input[i];

        if (escaped) {
            if (double_quoted && current != '\\' && current != '"') {
                token.push_back('\\');
            }
            token.push_back(current);
            escaped = false;
            continue;
        }

        if (current == '\\' && !single_quoted) {
            escaped = true;
            continue;
        }

        if (current == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            continue;
        }

        if (current == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            continue;
        }

        const bool in_quotes = single_quoted || double_quoted;

        if (!in_quotes) {
            if (std::isspace(static_cast<unsigned char>(current))) {
                flush_token();
                continue;
            }

            if (current == '|') {
                flush_token();
                tokens.emplace_back("|");
                continue;
            }

            if (
                token.empty() && (current == '1' || current == '2') && i + 1 < input.size() &&
                input[i + 1] == '>') {
                flush_token();
                std::string op;
                op.push_back(current);
                op.push_back('>');

                if (i + 2 < input.size() && input[i + 2] == '>') {
                    op.push_back('>');
                    ++i;
                }

                ++i;
                tokens.push_back(std::move(op));
                continue;
            }

            if (current == '>') {
                flush_token();
                if (i + 1 < input.size() && input[i + 1] == '>') {
                    tokens.emplace_back(">>");
                    ++i;
                } else {
                    tokens.emplace_back(">");
                }
                continue;
            }
        }

        token.push_back(current);
    }

    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }

    return tokens;
}

} // namespace shell
