#include <cstdlib>
#include <filesystem>
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

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    unordered_set<string> builtins = {"cd", "echo", "exit", "pwd", "type"};

    while (true) {
        cout << "$ ";

        string input, command;
        vector<string> args;

        getline(cin, input);

        auto input_ss = stringstream(input);

        input_ss >> command;

        string arg;
        while (input_ss >> arg)
            args.push_back(arg);

        if (should_exit(cin, command, args))
            break;

        if (command == "type") {
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
        } else if (command == "cd") {
            fs::path p(args.size() ? args[0] : "~");
            if (!set_current_path(p))
                cout << "cd: " << p.string() << ": No such file or directory"
                     << endl;
        } else if (command == "echo") {
            for (auto &arg : args)
                cout << arg << " ";
            cout << endl;
        } else if (command == "pwd") {
            cout << get_current_path() << endl;
        } else if (get_cmd_file_path(command).empty()) {
            cout << command << ": command not found" << endl;
        } else {
            exec(command, args);
        }
    }

    return 0;
}
