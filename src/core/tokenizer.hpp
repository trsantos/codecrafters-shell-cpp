#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace shell {

class Tokenizer {
  public:
    [[nodiscard]] std::vector<std::string> tokenize(std::string_view input) const;
};

} // namespace shell
