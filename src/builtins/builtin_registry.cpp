#include "builtins/builtin_registry.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include <readline/history.h>

#include "core/path_resolver.hpp"
#include "history/history_manager.hpp"

namespace shell {

namespace fs = std::filesystem;

BuiltinRegistry::BuiltinRegistry(PathResolver &path_resolver, HistoryManager &history_manager)
    : path_resolver_(path_resolver), history_manager_(history_manager) {
    register_builtins();
}

bool BuiltinRegistry::is_builtin(std::string_view command) const {
    return registry_.contains(std::string(command));
}

int BuiltinRegistry::execute(
    std::string_view command, const std::vector<std::string> &args, std::ostream &out, std::ostream &err) {
    auto it = registry_.find(std::string(command));
    if (it == registry_.end()) {
        return 1;
    }

    return it->second(args, out, err);
}

std::unordered_set<std::string> BuiltinRegistry::names() const {
    std::unordered_set<std::string> result;
    result.reserve(registry_.size());

    for (const auto &[name, _] : registry_) {
        result.insert(name);
    }

    return result;
}

bool BuiltinRegistry::exit_requested() const noexcept { return exit_requested_; }

void BuiltinRegistry::register_builtins() {
    registry_["cd"] = [this](const auto &args, auto &out, auto &err) { return builtin_cd(args, out, err); };
    registry_["echo"] = [this](const auto &args, auto &out, auto &err) { return builtin_echo(args, out, err); };
    registry_["pwd"] = [this](const auto &args, auto &out, auto &err) { return builtin_pwd(args, out, err); };
    registry_["type"] = [this](const auto &args, auto &out, auto &err) { return builtin_type(args, out, err); };
    registry_["history"] = [this](const auto &args, auto &out, auto &err) { return builtin_history(args, out, err); };
    registry_["exit"] = [this](const auto &args, auto &out, auto &err) { return builtin_exit(args, out, err); };
}

int BuiltinRegistry::builtin_cd(const std::vector<std::string> &args, std::ostream &out, std::ostream & /*err*/) {
    fs::path target_path(args.empty() ? "~" : args.front());
    if (target_path == "~") {
        const char *home = std::getenv("HOME");
        if (home != nullptr) {
            target_path = home;
        }
    }

    std::error_code ec;
    fs::current_path(target_path, ec);
    if (ec) {
        out << "cd: " << target_path.string() << ": No such file or directory" << std::endl;
        return 1;
    }

    return 0;
}

int BuiltinRegistry::builtin_echo(const std::vector<std::string> &args, std::ostream &out, std::ostream & /*err*/) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }

        out << args[i];
    }

    out << std::endl;
    return 0;
}

int BuiltinRegistry::builtin_pwd(const std::vector<std::string> & /*args*/, std::ostream &out, std::ostream & /*err*/) {
    out << fs::current_path().string() << std::endl;
    return 0;
}

int BuiltinRegistry::builtin_type(const std::vector<std::string> &args, std::ostream &out, std::ostream &err) {
    if (args.empty()) {
        err << "type: missing argument" << std::endl;
        return 1;
    }

    const auto &name = args.front();
    if (is_builtin(name)) {
        out << name << " is a shell builtin" << std::endl;
        return 0;
    }

    const auto file_path = path_resolver_.find_command_path(name);
    if (!file_path.empty()) {
        out << name << " is " << file_path << std::endl;
        return 0;
    }

    out << name << ": not found" << std::endl;
    return 1;
}

int BuiltinRegistry::builtin_history(const std::vector<std::string> &args, std::ostream &out, std::ostream &err) {
    if (!args.empty() && args[0] == "-r") {
        if (args.size() < 2) {
            err << "history: -r requires a file argument" << std::endl;
            return 1;
        }

        history_manager_.read_from_file(args[1]);
        return 0;
    }

    if (!args.empty() && args[0] == "-w") {
        if (args.size() < 2) {
            err << "history: -w requires a file argument" << std::endl;
            return 1;
        }

        history_manager_.write_to_file(args[1]);
        return 0;
    }

    if (!args.empty() && args[0] == "-a") {
        if (args.size() < 2) {
            err << "history: -a requires a file argument" << std::endl;
            return 1;
        }

        history_manager_.append_session_to_file(args[1]);
        return 0;
    }

    int limit = history_length;
    if (!args.empty()) {
        const auto &token = args[0];
        const char *first = token.data();
        const char *last = token.data() + token.size();
        auto [ptr, ec] = std::from_chars(first, last, limit);

        if (ec != std::errc{} || ptr != last || limit < 0) {
            err << "history: invalid numeric argument" << std::endl;
            return 1;
        }
    }

    history_manager_.print(out, limit);
    return 0;
}

int BuiltinRegistry::builtin_exit(const std::vector<std::string> &args, std::ostream & /*out*/, std::ostream & /*err*/) {
    if (args.empty() || args[0] == "0") {
        exit_requested_ = true;
    }

    return 0;
}

} // namespace shell
