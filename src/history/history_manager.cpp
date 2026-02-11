#include "history/history_manager.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <ostream>

#include <readline/history.h>

namespace shell {

void HistoryManager::initialize() {
    using_history();

    const char *histfile_env = std::getenv("HISTFILE");
    const char *home = std::getenv("HOME");

    history_file_path_ =
        histfile_env != nullptr ? std::string(histfile_env) : std::string(home != nullptr ? home : "") + "/.shell_history";

    read_history(history_file_path_.c_str());
    session_start_ = history_length;
}

void HistoryManager::save() const { write_history(history_file_path_.c_str()); }

void HistoryManager::record_input(const std::string &input) const {
    if (input.empty()) {
        return;
    }

    if (history_length == 0) {
        add_history(input.c_str());
        return;
    }

    const HIST_ENTRY *last_entry = history_get(history_length);
    if (last_entry == nullptr || std::strcmp(input.c_str(), last_entry->line) != 0) {
        add_history(input.c_str());
    }
}

void HistoryManager::read_from_file(const std::string &filepath) const {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            add_history(line.c_str());
        }
    }
}

void HistoryManager::write_to_file(const std::string &filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return;
    }

    for (int i = 1; i <= history_length; ++i) {
        const HIST_ENTRY *entry = history_get(i);
        if (entry != nullptr) {
            file << entry->line << '\n';
        }
    }

    last_appended_position_[filepath] = history_length;
}

void HistoryManager::append_session_to_file(const std::string &filepath) {
    const int start_pos = std::max(session_start_, last_appended_position_[filepath]) + 1;

    std::ofstream file(filepath, std::ios::app);
    if (!file.is_open()) {
        return;
    }

    for (int i = start_pos; i <= history_length; ++i) {
        const HIST_ENTRY *entry = history_get(i);
        if (entry != nullptr) {
            file << entry->line << '\n';
        }
    }

    last_appended_position_[filepath] = history_length;
}

void HistoryManager::print(std::ostream &out, int limit) const {
    const int normalized_limit = std::clamp(limit, 0, history_length);
    const int start = std::max(1, history_length - normalized_limit + 1);

    for (int i = start; i <= history_length; ++i) {
        const HIST_ENTRY *entry = history_get(i);
        if (entry != nullptr) {
            out << std::format("    {}  {}\n", i, entry->line);
        }
    }
}

} // namespace shell
