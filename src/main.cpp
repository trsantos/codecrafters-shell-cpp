#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <memory>
#include <readline/history.h>
#include <readline/readline.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace std;
namespace fs = filesystem;

// Global variable to track where the current session starts in history
// (used by history -a to only append current session commands)
int g_session_start = 0;

// Type alias for builtin command functions
using BuiltinFunc = function<void(vector<string> &, ostream &, ostream &)>;

bool should_exit(const string &cmd, const vector<string> &args) {
    return cmd == "exit" && !args.empty() && args[0] == "0";
}

void scan_path_executables(const string &prefix, function<bool(const string &, const string &)> callback) {
    if (const char *path_env = getenv("PATH")) {
        auto path_ss = stringstream(path_env);
        string dir;

        while (getline(path_ss, dir, ':')) {
            error_code ec;
            for (const auto &entry : fs::directory_iterator(dir, ec)) {
                if (ec)
                    break;

                if (entry.is_regular_file(ec) && !ec) {
                    string filename = entry.path().filename().string();
                    if (filename.starts_with(prefix)) {
                        auto perms = fs::status(entry.path(), ec).permissions();
                        if (!ec && fs::perms::none != (perms & fs::perms::owner_exec)) {
                            if (callback(filename, entry.path().string())) {
                                return; // Early exit if callback returns true
                            }
                        }
                    }
                }
            }
        }
    }
}

string get_cmd_file_path(const string &cmd) {
    string result;
    scan_path_executables(cmd, [&](const string &filename, const string &full_path) {
        if (filename == cmd) {
            result = full_path;
            return true; // Found exact match, stop scanning
        }
        return false;
    });
    return result;
}

auto get_current_path() { return fs::current_path().string(); }

bool set_current_path(fs::path &p) {
    error_code ec;
    fs::current_path(p, ec);
    return !ec;
}

void exec(const string &cmd, const vector<string> &args, ostream &out, ostream &err) {
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1)
        throw std::runtime_error("pipe failed");

    pid_t pid = fork();
    if (pid == -1)
        throw std::runtime_error("fork failed");

    // Child
    if (pid == 0) {
        close(stdout_pipe[0]); // close read ends
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO); // redirect stdout
        dup2(stderr_pipe[1], STDERR_FILENO); // redirect stderr

        close(stdout_pipe[1]); // close write ends
        close(stderr_pipe[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char *>(cmd.c_str()));
        for (auto &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);

        execvp(cmd.c_str(), argv.data());
        perror("exec failed");
        _exit(1);
    }

    // Parent
    close(stdout_pipe[1]); // close write ends
    close(stderr_pipe[1]);

    char buf[4096];
    ssize_t n;

    // Read stdout
    while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0)
        out.write(buf, n);
    close(stdout_pipe[0]);

    // Read stderr
    while ((n = read(stderr_pipe[0], buf, sizeof(buf))) > 0)
        err.write(buf, n);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
}

struct Command {
    string cmd;
    vector<string> args;
};

vector<string> parse_tokens(istringstream &iss) {
    vector<string> tokens;
    string token;
    char c;
    bool single_quoted = false;
    bool double_quoted = false;
    bool escaped = false;

    string double_quoted_escaped_chars = {'\\', '\"'};

    while (iss >> noskipws >> c) {
        if (escaped) {
            if (double_quoted && !double_quoted_escaped_chars.contains(c))
                token += '\\';
            token += c;
            escaped = false;
        } else if (c == '\\' && !single_quoted) {
            escaped = true;
        } else if (c == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
        } else if (c == '"' && !single_quoted) {
            double_quoted = !double_quoted;
        } else if (c == '|' && !single_quoted && !double_quoted) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            tokens.push_back("|");
        } else if (single_quoted || double_quoted || !isspace(c)) {
            token += c;
        } else if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
        }
    }

    if (!token.empty())
        tokens.push_back(token);

    return tokens;
}

vector<Command> parse_pipeline(const vector<string> &tokens) {
    vector<Command> commands;
    vector<string> current_args;

    for (const auto &token : tokens) {
        if (token == "|") {
            if (!current_args.empty()) {
                commands.push_back({current_args[0], {current_args.begin() + 1, current_args.end()}});
                current_args.clear();
            }
        } else {
            current_args.push_back(token);
        }
    }

    if (!current_args.empty()) {
        commands.push_back({current_args[0], {current_args.begin() + 1, current_args.end()}});
    }

    return commands;
}

void discard_redirection_args(vector<string> &args,
                              const vector<string> &operators = {">", "1>", ">>", "1>>", "2>", "2>>"}) {
    auto pred = [&operators](const auto &s) { return ranges::contains(operators, s); };
    auto it = ranges::find_if(args, pred);

    if (it != args.end())
        args.erase(it, args.end());
}

ostream &get_redirection_stream(const vector<string> &args, const vector<string> &operators, ostream &default_stream,
                                unique_ptr<ofstream> &file_stream) {
    auto pred = [&operators](const auto &s) { return ranges::contains(operators, s); };
    auto it = ranges::find_if(args, pred);

    if (it != args.end()) {
        bool append_mode = (*it == ">>" || *it == "1>>" || *it == "2>>");
        auto mode = append_mode ? (ofstream::out | ofstream::app) : ofstream::out;
        file_stream = make_unique<ofstream>(*next(it), mode);
        return *file_stream;
    }

    return default_stream;
}

ostream &get_output_stream(const vector<string> &args) {
    static unique_ptr<ofstream> stdout_stream;
    return get_redirection_stream(args, {">", "1>", ">>", "1>>"}, cout, stdout_stream);
}

ostream &get_error_stream(const vector<string> &args) {
    static unique_ptr<ofstream> stderr_stream;
    return get_redirection_stream(args, {"2>", "2>>"}, cerr, stderr_stream);
}

void maybe_close(ostream &stream, ostream &default_stream) {
    if (&stream != &default_stream)
        if (auto f = dynamic_cast<ofstream *>(&stream))
            f->close();
}

void setup_fd_redirection(vector<string> &args) {
    ostream &out = get_output_stream(args);
    ostream &err = get_error_stream(args);

    // Set up file descriptor redirection using native handles
    if (&out != &cout) {
        auto *file_stream = dynamic_cast<ofstream *>(&out);
        auto fd = file_stream->native_handle();
        dup2(fd, STDOUT_FILENO);
    }

    if (&err != &cerr) {
        auto *file_stream = dynamic_cast<ofstream *>(&err);
        auto fd = file_stream->native_handle();
        dup2(fd, STDERR_FILENO);
    }

    // Remove redirection arguments
    discard_redirection_args(args);
}

// Tab completion functionality
const unordered_set<string> *current_builtins = nullptr;

char *command_generator(const char *text, int state) {
    static set<string> matches;
    static set<string>::iterator match_iter;

    if (!state) {
        matches.clear();
        string prefix(text);

        // Add builtin commands that match the prefix
        if (current_builtins) {
            for (const auto &builtin : *current_builtins) {
                if (builtin.starts_with(prefix)) {
                    matches.insert(builtin);
                }
            }
        }

        // Add external executable commands from PATH
        scan_path_executables(prefix, [&](const string &filename, const string &full_path) {
            matches.insert(filename);
            return false; // Continue scanning for all matches
        });

        match_iter = matches.begin();
    }

    if (match_iter != matches.end()) {
        return strdup((match_iter++)->c_str());
    }

    return nullptr;
}

char **command_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;

    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }

    return nullptr;
}

void setup_completion(const unordered_set<string> &builtins) {
    current_builtins = &builtins;
    rl_attempted_completion_function = command_completion;
}

// Builtin command implementations

void builtin_cd(vector<string> &args, ostream &out, ostream &err) {
    fs::path p(args.size() ? args[0] : "~");
    if (p == "~")
        p = getenv("HOME");
    if (!set_current_path(p))
        out << "cd: " << p.string() << ": No such file or directory" << endl;
}

void builtin_echo(vector<string> &args, ostream &out, ostream &err) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0)
            out << " ";
        out << args[i];
    }
    out << endl;
}

void builtin_pwd(vector<string> &args, ostream &out, ostream &err) { out << get_current_path() << endl; }

void builtin_history(vector<string> &args, ostream &out, ostream &err) {
    static unordered_map<string, int> last_appended_position;

    if (!args.empty() && args[0] == "-r" && args.size() > 1) {
        // Read history from file
        string filepath = args[1];
        ifstream file(filepath);
        if (file.is_open()) {
            string line;
            while (getline(file, line)) {
                if (!line.empty()) {
                    add_history(line.c_str());
                }
            }
            file.close();
        }
    } else if (!args.empty() && args[0] == "-w" && args.size() > 1) {
        // Write history to file
        string filepath = args[1];
        ofstream file(filepath);
        if (file.is_open()) {
            for (int i = 1; i <= history_length; ++i) {
                HIST_ENTRY *entry = history_get(i);
                if (entry) {
                    file << entry->line << "\n";
                }
            }
            file.close();
            last_appended_position[filepath] = history_length;
        }
    } else if (!args.empty() && args[0] == "-a" && args.size() > 1) {
        // Append new history from current session to file
        string filepath = args[1];
        int start_pos = max(g_session_start, last_appended_position[filepath]) + 1;

        ofstream file(filepath, ios::app);
        if (file.is_open()) {
            for (int i = start_pos; i <= history_length; ++i) {
                HIST_ENTRY *entry = history_get(i);
                if (entry) {
                    file << entry->line << "\n";
                }
            }
            file.close();
            last_appended_position[filepath] = history_length;
        }
    } else {
        // Display history
        int limit = history_length;
        if (!args.empty()) {
            limit = stoi(args[0]);
        }
        int start = max(1, history_length - limit + 1);
        for (int i = start; i <= history_length; ++i) {
            HIST_ENTRY *entry = history_get(i);
            if (entry) {
                out << format("    {}  {}\n", i, entry->line);
            }
        }
    }
}

// Forward declare the registry for type builtin
// (will be defined in main)
extern unordered_map<string, BuiltinFunc> builtin_registry;

void builtin_type(vector<string> &args, ostream &out, ostream &err) {
    auto name = args[0];
    if (builtin_registry.contains(name)) {
        out << name << " is a shell builtin" << endl;
    } else {
        string file_path = get_cmd_file_path(name);
        if (!file_path.empty())
            out << name << " is " << file_path << endl;
        else
            out << name << ": not found" << endl;
    }
}

bool is_builtin(const string &cmd, const unordered_map<string, BuiltinFunc> &registry) {
    return registry.contains(cmd);
}

void execute_builtin(const string &cmd, vector<string> &args, const unordered_map<string, BuiltinFunc> &registry,
                     ostream &out = cout, ostream &err = cerr) {
    auto it = registry.find(cmd);
    if (it != registry.end()) {
        it->second(args, out, err);
    }
}

void exec_pipeline(vector<Command> &commands, const unordered_map<string, BuiltinFunc> &registry) {
    vector<int> pipes((commands.size() - 1) * 2);
    vector<pid_t> pids(commands.size());

    // Create inter-process pipes
    for (size_t i = 0; i < commands.size() - 1; ++i) {
        if (pipe(&pipes[i * 2]) == -1)
            throw std::runtime_error("pipe failed");
    }

    // Fork each command
    for (size_t i = 0; i < commands.size(); ++i) {
        pids[i] = fork();
        if (pids[i] == -1)
            throw std::runtime_error("fork failed");

        if (pids[i] == 0) {
            // Child process

            // Set up stdin from previous command's pipe
            if (i > 0) {
                dup2(pipes[(i - 1) * 2], STDIN_FILENO);
            }

            // Set up stdout to next command's pipe
            if (i < commands.size() - 1) {
                dup2(pipes[i * 2 + 1], STDOUT_FILENO);
            }

            // Close all pipe descriptors
            for (size_t j = 0; j < pipes.size(); ++j) {
                close(pipes[j]);
            }

            // Execute the command
            const string &cmd = commands[i].cmd;
            vector<string> &args = commands[i].args;

            // Try builtin first
            if (is_builtin(cmd, registry)) {
                setup_fd_redirection(args);
                execute_builtin(cmd, args, registry);
                _exit(0);
            } else if (get_cmd_file_path(cmd).empty()) {
                cout << cmd << ": command not found" << endl;
                _exit(1);
            } else {
                // External command - handle file redirection
                setup_fd_redirection(args);

                // Build argv
                vector<char *> argv;
                argv.reserve(args.size() + 2);
                argv.push_back(const_cast<char *>(cmd.c_str()));
                for (auto &arg : args)
                    argv.push_back(const_cast<char *>(arg.c_str()));
                argv.push_back(nullptr);

                // Execute command
                execvp(cmd.c_str(), argv.data());
                perror("exec failed");
                _exit(1);
            }
        }
    }

    // Parent process: close all pipes and wait for children
    for (size_t i = 0; i < pipes.size(); ++i) {
        close(pipes[i]);
    }

    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
    }
}

// Define the builtin registry (used by builtin_type)
unordered_map<string, BuiltinFunc> builtin_registry;

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    // Create builtin registry
    builtin_registry["cd"] = builtin_cd;
    builtin_registry["echo"] = builtin_echo;
    builtin_registry["pwd"] = builtin_pwd;
    builtin_registry["type"] = builtin_type;
    builtin_registry["history"] = builtin_history;
    // Note: exit is not in the registry because it's handled specially by should_exit()
    // but we need it for type command
    builtin_registry["exit"] = [](vector<string> &, ostream &, ostream &) {};

    // Legacy set for compatibility (will be removed)
    unordered_set<string> builtins = {"cd", "echo", "exit", "history", "pwd", "type"};

    setup_completion(builtins);

    // Initialize readline history
    using_history();

    // Load existing history file if it exists
    // Use HISTFILE environment variable if set, otherwise default to ~/.shell_history
    const char *histfile_env = getenv("HISTFILE");
    string history_file =
        histfile_env ? string(histfile_env) : string(getenv("HOME") ? getenv("HOME") : "") + "/.shell_history";
    read_history(history_file.c_str());

    // Track where current session starts (after loading persistent history)
    g_session_start = history_length;

    while (true) {
        char *line = readline("$ ");

        if (!line) {
            cout << endl;
            break;
        }

        string input(line);
        free(line);

        // Add non-empty commands to history (avoid consecutive duplicates)
        if (!input.empty()) {
            if (history_length == 0 || strcmp(input.c_str(), history_get(history_length)->line) != 0) {
                add_history(input.c_str());
            }
        }

        auto iss = istringstream(input);
        auto tokens = parse_tokens(iss);

        if (tokens.empty())
            continue;

        auto commands = parse_pipeline(tokens);

        if (commands.size() == 1) {
            // Single command - use existing logic
            const string &cmd = commands[0].cmd;
            vector<string> &args = commands[0].args;

            ostream &out = get_output_stream(args);
            ostream &err = get_error_stream(args);
            discard_redirection_args(args);

            if (should_exit(cmd, args))
                break;

            if (is_builtin(cmd, builtin_registry)) {
                execute_builtin(cmd, args, builtin_registry, out, err);

            } else if (get_cmd_file_path(cmd).empty()) {
                out << cmd << ": command not found" << endl;

            } else {
                exec(cmd, args, out, err);
            }

            maybe_close(out, cout);
            maybe_close(err, cerr);
        } else {
            // Multi-command pipeline
            exec_pipeline(commands, builtin_registry);
        }
    }

    // Save history to file before exit
    write_history(history_file.c_str());

    return 0;
}
