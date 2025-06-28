#include <iostream>
#include <ostream>
#include <string>
#include <unordered_set>

using namespace std;

int main() {
    // Flush after every std::cout / std:cerr
    cout << unitbuf;
    cerr << unitbuf;

    unordered_set<string> builtins = {"echo", "exit", "type"};

    while (true) {
        cout << "$ ";
        string input;
        getline(cin, input);

        if (cin.eof() || input == "exit 0")
            break;
        else if (input.starts_with("type ")) {
            auto cmd = input.substr(5);
            if (builtins.contains(cmd))
                cout << cmd << " is a shell builtin" << endl;
            else
                cout << cmd << ": not found" << endl;
        }
        else if (input.starts_with("echo "))
            cout << input.substr(5) << endl;
        else
            cout << input << ": command not found" << endl;
    }

    return 0;
}
