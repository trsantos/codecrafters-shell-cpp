#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace std;
namespace fs = filesystem;

bool should_exit(const istream &is, const string &cmd, const vector<string> &args) {
    if (is.eof()) {
        cout << endl;
        return true;
    }

    return cmd == "exit" && !args.empty() && args[0] == "0";
}

string get_cmd_file_path(const string &cmd) {
    auto path_ss = stringstream(getenv("PATH"));
    string dir;

    while (getline(path_ss, dir, ':')) {
        fs::path full_path = dir + "/" + cmd;
        if (fs::is_regular_file(full_path)) {
            auto perms = fs::status(full_path).permissions();
            if (fs::perms::none != (perms & fs::perms::owner_exec))
                return full_path;
        }
    }

    return "";
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

pair<string, vector<string>> parse_args(istringstream &iss) {
    vector<string> args;
    string arg;
    char c;
    bool single_quoted = false;
    bool double_quoted = false;
    bool escaped = false;

    string double_quoted_escaped_chars = {'\\', '\"'};

    while (iss >> noskipws >> c) {
        if (escaped) {
            if (double_quoted && !double_quoted_escaped_chars.contains(c))
                arg += '\\';
            arg += c;
            escaped = false;
        } else if (c == '\\' && !single_quoted) {
            escaped = true;
        } else if (c == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
        } else if (c == '"' && !single_quoted) {
            double_quoted = !double_quoted;
        } else if (single_quoted || double_quoted || !isspace(c)) {
            arg += c;
        } else if (!arg.empty()) {
            args.push_back(arg);
            arg.clear();
        }
    }

    if (!arg.empty())
        args.push_back(arg);

    if (args.empty())
        return {};

    return {args[0], {args.begin() + 1, args.end()}};
}

void discard_redirection_args(vector<string> &args,
                              const vector<string> &operators = {">", "1>", ">>", "1>>", "2>", "2>>"}) {
    auto pred = [&operators](const auto &s) { return ranges::find(operators, s) != operators.end(); };
    auto it = ranges::find_if(args, pred);

    if (it != args.end())
        args.erase(it, args.end());
}

ostream &get_redirection_stream(const vector<string> &args, const vector<string> &operators, ostream &default_stream) {
    static unique_ptr<ofstream> file_stream;

    auto pred = [&operators](const auto &s) { return ranges::find(operators, s) != operators.end(); };
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
    return get_redirection_stream(args, {">", "1>", ">>", "1>>"}, cout);
}

ostream &get_error_stream(const vector<string> &args) { return get_redirection_stream(args, {"2>", "2>>"}, cerr); }

void maybe_close(ostream &stream, ostream &default_stream) {
    if (&stream != &default_stream)
        if (auto f = dynamic_cast<ofstream *>(&stream))
            f->close();
}

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    unordered_set<string> builtins = {"cd", "echo", "exit", "pwd", "type"};

    while (true) {
        cout << "$ ";

        string input;

        getline(cin, input);

        auto iss = istringstream(input);
        auto [cmd, args] = parse_args(iss);

        ostream &out = get_output_stream(args);
        ostream &err = get_error_stream(args);
        discard_redirection_args(args);

        if (should_exit(cin, cmd, args))
            break;

        if (cmd == "type") {

            auto name = args[0];

            if (builtins.contains(name)) {
                out << name << " is a shell builtin" << endl;
                continue;
            }

            string file_path = get_cmd_file_path(name);

            if (!file_path.empty())
                out << name << " is " << file_path << endl;
            else
                out << name << ": not found" << endl;

        } else if (cmd == "cd") {

            fs::path p(args.size() ? args[0] : "~");
            if (p == "~")
                p = getenv("HOME");
            if (!set_current_path(p))
                out << "cd: " << p.string() << ": No such file or directory" << endl;

        } else if (cmd == "echo") {

            for (auto &arg : args)
                out << arg << " ";
            out << endl;

        } else if (cmd == "pwd") {

            out << get_current_path() << endl;

        } else if (get_cmd_file_path(cmd).empty()) {

            out << cmd << ": command not found" << endl;

        } else {

            exec(cmd, args, out, err);
        }

        maybe_close(out, cout);
        maybe_close(err, cerr);
    }

    return 0;
}
