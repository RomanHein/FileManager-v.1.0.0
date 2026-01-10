# -- Introduction --
### What does the FileManager do? 
The manager is essentially just a class which **handles all the verbose code parts of working with files in C++17**.
The class provides user-friendly methods for file manipulation and makes the developer's work easier by freeing them of the concerns that come with file management (saving changes, reading specific rows, bound checking, etc.).

### What makes it so special? 
- **The file's content is loaded into RAM** during instatiation for lightning fast read/write operations.
- **Ensures that memory usage stays low by clearing garbage (unused rows) regularly.**
- **Very performant if user only appends rows and deletes these before saving.**
- Deleting a row moves all later rows down.
- Methods are idiomatic and easy to understand with additional comments.

# -- Code samples --
```
// The manager is a header only library.
#include "file_manager.h"

// Creates a test.txt file at the root of the C drive, depending on the provided path.
// If the given path/file doesn't exist, the manager creates the according directories/file.
FileManager fm(R"C:/test.txt");

// Appends "Hello world!" at the end of the txt file.
fm.append("Hello world", "!");

// Optional: save changes. Happens automatically when the destructor is called.
fm.save();
```
