#include <iostream>

#include "../include/fmanager.h"

int main() {
    fmanager file("todo.txt");
    file.append("Hello World");
}