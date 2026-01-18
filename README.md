# Introduction
### What does the FileManager do? 
The file manager is a wrapper class for **C++17**'s filestreams. It takes control of a specified file and **provides idiomatic methods to manipulate the given file**.
As a **standalone library**, it **handles all verbose code parts of file streams** under the hood and offers a balance between efficiency, ease of use, reliability and features.

### What are the features?
- File content is **loaded into RAM** for performance.
- Creates **recovery files in case of a failed save**.
- **Keeps memory low** by cleaning garbage regularly.
- **Saves efficiently** by evaluating whether a full rewrite is necessary.
- Standalone, **independent library** which can just be dropped into the project folder.

# Installation
1. Download the latest release under 'Releases'.
2. Extract the 'file_manager.h' file from the zip into your project folder.
3. Make sure to use C++17 or later.
4. Include the file manager. e.g. `#include "file_manager.h"`.

# Code samples
### Creating a .txt file and adding 'Hello world!'
```
#include "file_manager.h"

int main() {
    // Creates a test.txt file at the root of the C drive, depending on the provided path.
    // If the given path/file doesn't exist, the manager creates the according directories/file.
    FileManager fm(R"C:/test.txt");

    // Appends "Hello world!" at the end of the .txt file.
    fm.append("Hello world", "!");

    // Saves automatically on deconstruction!
}
```

### Basic task schedeuler
```
#include <iostream>
#include <string>

#include "file_manager.h"

int main() {
    FileManager fm(R"(C:\Users\Roman Hein\Desktop\todo.txt)");

	int input;
	std::string task;

	while (true) {
		if (!fm.empty()) {
			std::cout << "Next task: " << fm.first() << "\n";
		}
		else {
			std::cout << "No tasks present.\n";
		}

		std::cout << "1) Add new task\n"
			  	  << "2) Mark current task as completed\n"
			  	  << "3) Exit\n"
			      << ": ";

		std::cin >> input;
		std::cin.ignore();

		switch (input) {
		case 1:
			std::cout << "Type a task: ";
			std::getline(std::cin, task);

			fm.append(task);
			break;

		case 2:
			if (fm.empty()) {
				std::cout << "No tasks present.\n";
				break;
			}

			fm.erase(0);
			break;

		case 3:
			return 0;

		default:
			std::cout << "Invalid input.\n";
		}
	}
}
```

# Class Methods
| Method  | Explanation |
|---------|-------------|
| FileManager(filePath) | Creates a new FileManager instance that manages the specified file. |
| read(row) | Returns the text at the specified row. |
| split(row, delimiter) | Returns the text parts of split text at the specified row by the specified delimiter. |
| first() | Returns a copy of the text at the first row. |
| last() | Returns a copy of the text at the last row. |
| all() | Returns a copy of the text at every row. |
| append(args) | Adds the given arguments to a new row at the end of the file. |
| overwrite(row, args) | Overwrites the specified row with the specified arguments |
| erase(row) | Deletes the specified row, shifting all later elements down. |
| clear() | Deletes all rows. |
| save() | Saves all changes back to the file. |
| empty() | Returns true if there are no present rows. |
| size() | Returns the number of present rows. |
