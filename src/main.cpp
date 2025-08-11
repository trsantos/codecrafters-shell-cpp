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

void exec(const string &cmd, const vector<string> &args, ostream &out) {
    int pipefd[2];
    if (pipe(pipefd) == -1)
        throw std::runtime_error("pipe failed");

    pid_t pid = fork();
    if (pid == -1)
        throw std::runtime_error("fork failed");

    // Child
    if (pid == 0) {
        close(pipefd[0]);               // close read end
        dup2(pipefd[1], STDOUT_FILENO); // redirect stdout only
        close(pipefd[1]);

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
    close(pipefd[1]); // close write end
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        out.write(buf, n);
    close(pipefd[0]);

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

ostream &get_output_stream(vector<string> &args) {
    static unique_ptr<ofstream> file_stream;

    auto it = ranges::find_if(args, [](auto &s) { return s == ">" || s == "1>"; });

    if (it != args.end()) {
        auto marker = *it;
        file_stream = make_unique<ofstream>(*next(it), ofstream::out);
        args.erase(it, args.end());
        return *file_stream;
    }

    return cout;
}

void maybe_close(ostream &out) {
    if (&out != &cout)
        if (auto f = dynamic_cast<ofstream *>(&out))
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

        ostream &os = get_output_stream(args);

        if (should_exit(cin, cmd, args))
            break;

        if (cmd == "type") {

            auto name = args[0];

            if (builtins.contains(name)) {
                os << name << " is a shell builtin" << endl;
                continue;
            }

            string file_path = get_cmd_file_path(name);

            if (!file_path.empty())
                os << name << " is " << file_path << endl;
            else
                os << name << ": not found" << endl;

        } else if (cmd == "cd") {

            fs::path p(args.size() ? args[0] : "~");
            if (p == "~")
                p = getenv("HOME");
            if (!set_current_path(p))
                os << "cd: " << p.string() << ": No such file or directory" << endl;

        } else if (cmd == "echo") {

            for (auto &arg : args)
                os << arg << " ";
            os << endl;

        } else if (cmd == "pwd") {

            os << get_current_path() << endl;

        } else if (get_cmd_file_path(cmd).empty()) {

            os << cmd << ": command not found" << endl;

        } else {

            exec(cmd, args, os);
        }

        maybe_close(os);
    }

    return 0;
}
