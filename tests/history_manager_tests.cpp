#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <readline/history.h>
#include <unistd.h>

#include "history/history_manager.hpp"

using shell::HistoryManager;

namespace {

namespace fs = std::filesystem;

class EnvVarGuard {
  public:
    explicit EnvVarGuard(const char *name) : name_(name) {
        const char *value = std::getenv(name_.c_str());
        if (value != nullptr) {
            had_value_ = true;
            value_ = value;
        }
    }

    ~EnvVarGuard() {
        if (had_value_) {
            setenv(name_.c_str(), value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    bool had_value_{false};
    std::string value_;
};

std::string make_temp_file(std::string_view initial = "") {
    std::string pattern = "/tmp/shell_history_manager_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    const int fd = mkstemp(buffer.data());
    assert(fd != -1);
    close(fd);

    if (!initial.empty()) {
        std::ofstream file(buffer.data());
        assert(file.is_open());
        file << initial;
    }

    return buffer.data();
}

std::string slurp(const std::string &path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void reset_history() {
    using_history();
    clear_history();
}

void test_initialize_and_save_with_histfile() {
    EnvVarGuard histfile_guard("HISTFILE");
    EnvVarGuard home_guard("HOME");

    const std::string histfile = make_temp_file("echo old\n");
    setenv("HISTFILE", histfile.c_str(), 1);

    reset_history();

    HistoryManager manager;
    manager.initialize();

    assert(history_length == 1);
    assert(std::string(history_get(1)->line) == "echo old");

    add_history("echo new");
    manager.save();

    const auto content = slurp(histfile);
    assert(content.find("echo old") != std::string::npos);
    assert(content.find("echo new") != std::string::npos);

    fs::remove(histfile);
}

void test_record_input_deduplicates_consecutive_commands() {
    reset_history();
    HistoryManager manager;

    manager.record_input("");
    assert(history_length == 0);

    manager.record_input("echo first");
    assert(history_length == 1);

    manager.record_input("echo first");
    assert(history_length == 1);

    manager.record_input("echo second");
    assert(history_length == 2);
}

void test_read_write_append_and_print_variants() {
    reset_history();
    HistoryManager manager;

    const std::string read_file = make_temp_file("echo a\n\necho b\n");
    manager.read_from_file(read_file);
    assert(history_length == 2);

    manager.read_from_file("/no/such/file/for/history");

    const std::string write_file = make_temp_file();
    manager.write_to_file(write_file);
    const auto written = slurp(write_file);
    assert(written.find("echo a") != std::string::npos);
    assert(written.find("echo b") != std::string::npos);

    manager.write_to_file("/no/such/directory/history.txt");

    std::stringstream out;
    manager.print(out, 1);
    assert(out.str().find("echo b") != std::string::npos);

    std::stringstream out_zero;
    manager.print(out_zero, 0);
    assert(out_zero.str().empty());

    std::stringstream out_large;
    manager.print(out_large, 100);
    assert(out_large.str().find("echo a") != std::string::npos);
    assert(out_large.str().find("echo b") != std::string::npos);

    fs::remove(read_file);
    fs::remove(write_file);
}

void test_append_session_to_file_tracks_last_append_position() {
    EnvVarGuard histfile_guard("HISTFILE");

    const std::string histfile = make_temp_file("echo old1\necho old2\n");
    const std::string append_file = make_temp_file();

    setenv("HISTFILE", histfile.c_str(), 1);

    reset_history();

    HistoryManager manager;
    manager.initialize();

    add_history("echo new1");
    add_history("echo new2");

    manager.append_session_to_file(append_file);
    const auto first_append = slurp(append_file);
    assert(first_append.find("echo new1") != std::string::npos);
    assert(first_append.find("echo new2") != std::string::npos);
    assert(first_append.find("echo old1") == std::string::npos);

    manager.append_session_to_file(append_file);
    const auto second_append = slurp(append_file);
    assert(second_append == first_append);

    manager.append_session_to_file("/no/such/directory/append-history.txt");

    fs::remove(histfile);
    fs::remove(append_file);
}

} // namespace

int main() {
    test_initialize_and_save_with_histfile();
    test_record_input_deduplicates_consecutive_commands();
    test_read_write_append_and_print_variants();
    test_append_session_to_file_tracks_last_append_position();

    return 0;
}
