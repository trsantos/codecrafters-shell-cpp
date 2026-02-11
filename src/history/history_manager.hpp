#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>

namespace shell {

class HistoryManager {
  public:
    void initialize();
    void save() const;
    void record_input(const std::string &input) const;

    void read_from_file(const std::string &filepath) const;
    void write_to_file(const std::string &filepath);
    void append_session_to_file(const std::string &filepath);

    void print(std::ostream &out, int limit) const;

  private:
    std::string history_file_path_;
    int session_start_{0};
    std::unordered_map<std::string, int> last_appended_position_;
};

} // namespace shell
