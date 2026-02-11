#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

#include "core/command.hpp"

namespace shell {

struct ParseError {
    std::string message;
};

class Parser {
  public:
    [[nodiscard]] std::expected<Pipeline, ParseError> parse(std::span<const std::string> tokens) const;
};

} // namespace shell
