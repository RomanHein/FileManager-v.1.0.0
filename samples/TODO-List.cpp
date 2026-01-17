#include <iostream>

#include "../include/filemanager.h"

int main() {
    filemanager file("todo.txt");
    file.overwrite(2, "New entry", " by overwriting", "!");

    for (const auto& entry : file.all()) {
        std::cout << entry << "\n";
    }
}