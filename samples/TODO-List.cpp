#include <iostream>

#include "../include/filemanager.h"

int main() {
    FileManager file("todo.txt");
    file.erase(0);

    for (const auto& entry : file.all()) {
        std::cout << entry << "\n";
    }
}