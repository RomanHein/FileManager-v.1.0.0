#ifndef FILEMANAGER_FILEMANAGER_H
#define FILEMANAGER_FILEMANAGER_H

#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#define COMMAND_DELIMITER ';'
#define ESTIMATED_CHARS_PER_ROW 64
#define CACHE_BUFFER_SIZE 10
#define JOURNAL_FLUSH_THRESHOLD 16

class FileManager {
    enum class Command : char {
        Append = 'A',
        Clear = 'C',
        Erase = 'E',
        Overwrite = 'O'
    };

    class Journal {
        struct Token {
            bool isValid = false;
            std::string value;
        };

    public:
        explicit Journal(std::filesystem::path journal_path) :
            _journal_path(std::move(journal_path))
        {}

        /**
         * @brief Creates a journal entry for a file manager method call
         * @param command Type of the method call
         * @param args Arguments during call (optional)
         * @note Doesn't write to the journal instantly, stores it in a vector instead which is
         * flushed on the next save. Saves if amount of unsaved records exceed JOURNAL_FLUSH_THRESHOLD
         */
        template <typename... Args>
        void record(const Command command, Args... args) {
            std::string entry;
            entry.push_back(static_cast<char>(command));
            entry.push_back(COMMAND_DELIMITER);

            if (sizeof...(Args) > 0) {
                ((entry += tokenize(std::forward<Args>(args))), ...);
            }

            _pending_commands.push_back(std::move(entry));
            _outdated = true;

            if (_pending_commands.size() >= JOURNAL_FLUSH_THRESHOLD) {
                save();
            }
        }

        /**
         * @brief Calls every method recorded in the journal
         * @param callback Function which handles internal file manager method calls from journal
         */
        template<typename F>
        void replay(F&& callback) const {
            std::ifstream in(_journal_path);
            std::string line;
            std::vector<std::string> args;
            args.reserve(2);

            if (!in.is_open()) throw std::runtime_error("couldnt open file");

            while (std::getline(in, line)) {
                const auto command = static_cast<Command>(line[0]);
                size_t cursor = 2;
                args.clear();

                while (cursor != std::string::npos) {
                    if (auto [isValid, value] = _extract_token(line, cursor); isValid) {
                        args.push_back(std::move(value));
                    }
                    else {
                        break;
                    }
                }

                callback(command, args);
            }
        }

        /**
         * @brief Removes the journal file
         */
        void destroy() const {
            std::filesystem::remove(_journal_path);
        }

        /**
         * @brief Appends unsaved commands to the journal
         */
        void save() {
            if (!_outdated) return;

            std::ofstream out(_journal_path, std::ios::app);

            for (const auto& command : _pending_commands) {
                out << command << "\n";
            }

            out.close();
            out.flush();
            _pending_commands.clear();
            _outdated = false;
        }

        /**
         * @brief Checks whether the journal file exists
         * @return True if the journal file exists
         */
        [[nodiscard]] bool exists() const {
            return std::filesystem::exists(_journal_path);
        }

    private:
        /**
         * @brief Checks whether a string only contains numbers
         * @param str String to check
         * @return True if the string only contains numbers
         */
        [[nodiscard]] static bool _is_numerical(const std::string& str) {
            return std::find_if(str.begin(), str.end(), [](const char c) {
                return !std::isdigit(c);
            }) == str.end();
        }

        /**
         * @brief Attempts to extract a token from a command line
         * @param line Base command line
         * @param offset Where to start reading the new token from
         * @return Extracted token
         */
        static Token _extract_token(const std::string& line, size_t& offset) {
            const size_t delimiter = line.find(COMMAND_DELIMITER, offset);

            if (delimiter == std::string::npos) {
                offset = std::string::npos;
                return {};
            }

            std::string data = line.substr(offset, delimiter - offset);

            if (!_is_numerical(data)) {
                offset = std::string::npos;
                return {};
            }

            // Each token has a fixed length, e.g. 11;Hello world; (11 in this case)
            const size_t length = std::stoull(data);

            if (line.size() - delimiter - 1 <= length) {
                offset = std::string::npos;
                return {};
            }

            data = line.substr(delimiter + 1, length);
            const size_t new_offset = delimiter + 1 + length + 1;
            offset = new_offset < line.size() ? new_offset : std::string::npos;

            return {true, std::move(data)};
        }

        /**
         * @brief Serializes a parameter for the journal
         * @param value What to convert
         * @return Serialized value
         */
        template <typename T>
        static std::string tokenize(T value) {
            std::string result;

            if constexpr (std::is_same_v<T, std::string>) {
                result = std::move(value);
            }
            else if constexpr (std::is_same_v<T, const char*>) {
                result = std::string(value);
            }
            else {
                result = std::to_string(value);
            }

            return std::to_string(result.size()) + COMMAND_DELIMITER + result + COMMAND_DELIMITER;
        }

        const std::filesystem::path _journal_path;
        std::vector<std::string> _pending_commands;
        bool _outdated = false;
    };

public:
    explicit FileManager(std::filesystem::path file_path) :
        _journal(file_path.parent_path() / (file_path.stem().string() + "_journal" + file_path.extension().string())),
        _root_path(std::move(file_path))
    {
        if (std::filesystem::path tmp_path = _root_path ; std::filesystem::exists(tmp_path.replace_extension(".tmp"))) {
            std::filesystem::remove(tmp_path);
        }

        if (std::filesystem::exists(_root_path)) {
            _init_cache();
        }

        if (_journal.exists()) {
            _journal.replay([this](const Command command, const std::vector<std::string>& args) {
                _execute_command(command, args);
            });
            _consolidate();
        }
    }

    ~FileManager() {
        try {
            _consolidate();
        }
        catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    /**
     * @brief Returns a copy of the text at the specified index
     * @param index Line you want to read
     * @return Copy of the text at the given index
     */
    [[nodiscard]] std::string read(const size_t index) const {
        if (index >= _index_order.size()) throw std::out_of_range("index out of range");
        return _cache[_index_order[index]];
    }

    /**
     * @brief Returns a copy of the first line of the file
     * @return Copy of the first line
     */
    [[nodiscard]] std::string first() const {
        if (_index_order.empty()) throw std::out_of_range("file is empty");
        return _cache[_index_order.front()];
    }

    /**
     * @brief Returns a copy of the last line of the file
     * @return Copy of the last line
     */
    [[nodiscard]] std::string last() const {
        if (_index_order.empty()) throw std::out_of_range("file is empty");
        return _cache[_index_order.back()];
    }

    /**
     * @brief Copies every line of the file
     * @return Copy of every line
     */
    [[nodiscard]] std::vector<std::string> all() const {
        std::vector<std::string> result;
        result.reserve(_index_order.size());

        for (const auto index : _index_order) {
            result.push_back(_cache[index]);
        }

        return result;
    }

    /**
     * @brief Append the given arguments to the file
     * @param args Content to append
     */
    template <typename... Args>
    void append(Args... args) {
        std::stringstream ss;
        (ss << ... << args);

        _apply_append(ss.str());
        _journal.record(Command::Append, ss.str());
    }

    /**
     * @brief Overwrites the specified index with the given arguments
     * @param index Line to overwrite
     * @param args Content to overwrite with
     */
    template <typename... Args>
    void overwrite(const size_t index, Args... args) {
        std::stringstream ss;
        (ss << ... << args);

        _apply_overwrite(index, ss.str());
        _journal.record(Command::Overwrite, index, ss.str());
    }

    /**
     * @brief Deletes a line, shifting later elements down
     * @param index Which line to erase
     */
    void erase(const size_t index) {
        _apply_erase(index);
        _journal.record(Command::Erase, index);
    }

    /**
     * @brief Deletes everything
     */
    void clear() {
        _apply_clear();
        _journal.record(Command::Clear);
    }

    /**
     * @brief Saves all changes
     * @note Changes aren't saved to the main file, the journal is flushed instead
     * to increase performance
     */
    void save() {
        _journal.save();
    }

    [[nodiscard]] size_t size() const {
        return _index_order.size();
    }

    [[nodiscard]] bool empty() const {
        return _index_order.empty();
    }

private:
    /**
     * @brief Initializes the cache with the content of the root path
     */
    void _init_cache() {
        std::ifstream in(_root_path);
        std::string line;
        size_t index = 0;

        if (!in.is_open()) throw std::runtime_error("could not open file");

        // Reserve vector space by guessing how many lines the file has
        if (const auto file_size = std::filesystem::file_size(_root_path); file_size > 0) {
            const size_t estimated_rows = file_size / ESTIMATED_CHARS_PER_ROW + 1;
            _cache.reserve(estimated_rows);
            _index_order.reserve(estimated_rows);
        }

        while (std::getline(in, line)) {
            _cache.push_back(std::move(line));
            _index_order.push_back(index);
            ++index;
        }
    }

    /**
     * @brief Attempts to rewrite the file to save all changes
     * @note Saving isn't guaranteed. In case of a failure, the journal file is kept alive
     */
    void _consolidate() {
        if (!_needs_consolidation) return;

        std::filesystem::path write_path = _root_path;
        write_path.replace_extension(".tmp");
        std::ofstream out(write_path, std::ios::trunc);

        if (!out.is_open()) {
            _journal.save();
            return;
        }

        for (const auto index : _index_order) {
            out << _cache[index] << "\n";
        }

        out.close();
        std::error_code ec;
        std::filesystem::rename(write_path, _root_path, ec);

        if (ec) {
            std::filesystem::remove(write_path, ec);
            _journal.save();
            return;
        }

        _journal.destroy();
        _needs_consolidation = false;
    }

    void _apply_append(std::string text) {
        _cache.push_back(std::move(text));
        _index_order.push_back(_cache.size() - 1);
        _needs_consolidation = true;
    }

    void _apply_overwrite(const size_t index, std::string text) {
        if (index >= _index_order.size()) throw std::invalid_argument("Invalid index");
        _cache[_index_order[index]] = std::move(text);
        _needs_consolidation = true;
    }

    void _apply_erase(const size_t index) {
        if (index >= _index_order.size()) throw std::invalid_argument("Invalid index");
        _index_order.erase(_index_order.begin() + static_cast<int>(index));
        _needs_consolidation = true;
        if (_cache.size() >= _index_order.size() + 50) _compact();
    }

    void _apply_clear() {
        if (_index_order.empty()) return;
        _cache.clear();
        _index_order.clear();
        _needs_consolidation = true;
    }

    /**
     * @brief Rebuilds internal cache to let go of unused lines
     * @note Should only be called when calling erase() multiple times
     */
    void _compact() {
        std::vector<std::string> new_cache;
        new_cache.reserve(_index_order.size());

        for (const auto index : _index_order) {
            new_cache.push_back(std::move(_cache[index]));
        }

        _cache = std::move(new_cache);
        _index_order.clear();
        _index_order.resize(_cache.size());
        std::iota(_index_order.begin(), _index_order.end(), 0);
    }

    /**
     * @brief Calls internal file manager methods based on arguments. Necessary for Journal::replay()
     * @param command Type of command to execute e.g. Append
     * @param args Arguments to pass during function call
     */
    void _execute_command(const Command command, const std::vector<std::string>& args) {
        switch (command) {
            case Command::Append:
                if (args.empty()) break;
                _apply_append(args[0]);
                break;
            case Command::Overwrite:
                if (args.size() < 2) break;
                _apply_overwrite(std::stoull(args[0]), args[1]);
                break;
            case Command::Erase:
                if (args.empty()) break;
                _apply_erase(std::stoull(args[0]));
                break;
            case Command::Clear:
                _apply_clear();
                break;
            default:
                throw std::invalid_argument("Invalid command");
        }
    }

    Journal _journal;
    const std::filesystem::path _root_path;
    std::vector<std::string> _cache;
    std::vector<size_t> _index_order;
    bool _needs_consolidation = false;
};

#endif //FILEMANAGER_FILEMANAGER_H