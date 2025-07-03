#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace std;
namespace fs = filesystem;

bool should_exit(const istream &is, const string &cmd,
                 const vector<string> &args) {
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

void exec(const string &cmd, const vector<string> &args) {
    vector<char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char *>(cmd.c_str()));
    for (auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

    auto pid = fork();

    if (pid == 0) {
        execvp(cmd.c_str(), argv.data());
        perror("exec failed");
        _exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

vector<string> get_args(istringstream &is) {
    char c;
    string arg;
    vector<string> args;
    bool quoted = false;

    while (is >> noskipws >> c) {
        if (c == '\'') {
            quoted = !quoted;
            continue;
        }

        if (quoted || !isspace(c))
            arg.push_back(c);
        else if (isspace(c) && !arg.empty()) {
            args.push_back(arg);
            arg.clear();
        }
    }

    if (!arg.empty())
        args.push_back(arg);

    return args;
}

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    unordered_set<string> builtins = {"cd", "echo", "exit", "pwd", "type"};

    while (true) {
        cout << "$ ";

        string input, cmd;

        getline(cin, input);

        auto iss = istringstream(input);
        iss >> cmd;
        vector<string> args = get_args(iss);

        if (should_exit(cin, cmd, args))
            break;

        if (cmd == "type") {

            auto name = args[0];

            if (builtins.contains(name)) {
                cout << name << " is a shell builtin" << endl;
                continue;
            }

            string file_path = get_cmd_file_path(name);

            if (!file_path.empty())
                cout << name << " is " << file_path << endl;
            else
                cout << name << ": not found" << endl;

        } else if (cmd == "cd") {

            fs::path p(args.size() ? args[0] : "~");
            if (p == "~") p = getenv("HOME");
            if (!set_current_path(p))
                cout << "cd: " << p.string() << ": No such file or directory"
                     << endl;

        } else if (cmd == "echo") {

            for (auto &arg : args)
                cout << arg << " ";
            cout << endl;

        } else if (cmd == "pwd") {

            cout << get_current_path() << endl;

        } else if (get_cmd_file_path(cmd).empty()) {

            cout << cmd << ": command not found" << endl;

        } else {

            exec(cmd, args);

        }
    }

    return 0;
}
