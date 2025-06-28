#include <iostream>
#include <ostream>
#include <string>

int main() {
    // Flush after every std::cout / std:cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    while (true) {
        std::cout << "$ ";
        std::string input;
        std::getline(std::cin, input);

        if (std::cin.eof() || input == "exit 0")
            break;
        else if (auto i = input.starts_with("echo "))
            std::cout << input.substr(5) << std::endl;
        else
            std::cout << input << ": command not found" << std::endl;
    }
}
