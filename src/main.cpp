#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <unistd.h>

using namespace std;
namespace fs = filesystem;

bool should_exit(const istream &is, const string &cmd,
                 const vector<string> &args) {
    if (is.eof()) {
        cout << endl;
        return true;
    }

    return cmd == "exit" && args[0] == "0";
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

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    unordered_set<string> builtins = {"echo", "exit", "type"};

    while (true) {
        cout << "$ ";

        string input, command;
        vector<string> args;

        getline(cin, input);

        auto input_ss = stringstream(input);

        input_ss >> command;
        while (input_ss) {
            string arg;
            input_ss >> arg;
            args.push_back(arg);
        }

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

        } else if (command == "echo") {
            for (auto &arg : args)
                cout << arg << " ";
            cout << endl;
        } else if (get_cmd_file_path(command).empty()) {
            cout << command << ": command not found" << endl;
        } else {
            system(input.c_str());
        }
    }

    return 0;
}
