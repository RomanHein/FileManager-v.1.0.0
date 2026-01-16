#ifndef FILEMANAGER_FILEMANAGER_H
#define FILEMANAGER_FILEMANAGER_H

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <utility>
#include <vector>

#define COMMAND_DELIMITER ';'

class fmanager {
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

        template <typename... Args>
        void record(const Command command, Args... args) {
            std::string entry;
            entry += static_cast<char>(command) + COMMAND_DELIMITER;

            if (sizeof...(Args) > 0) {
                ((entry += tokenize(std::forward<Args>(args))), ...);
            }

            _pending_commands.emplace_back(std::move(entry));
        }

        void replay(const std::function<void(const Command, const std::vector<std::string>&)>& callback) const {
            std::ifstream in(_journal_path);
            std::string line;
            std::vector<std::string> args;
            args.reserve(2);

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

        [[nodiscard]] bool destroy() const {
            std::error_code ec;
            std::filesystem::remove(_journal_path, ec);
            return ec ? false : true;
        }

        void save() {
            if (_pending_commands.empty()) return;

            std::ofstream out(_journal_path, std::ios::app);

            for (const auto& command : _pending_commands) {
                out << command << "\n";
            }

            out.close();
            _pending_commands.clear();
        }

        [[nodiscard]] bool exists() const {
            return std::filesystem::exists(_journal_path);
        }

    private:
        static Token _extract_token(const std::string& line, size_t& offset) {
            const size_t delimiter = line.find(COMMAND_DELIMITER, offset);

            if (delimiter == std::string::npos) {
                offset = std::string::npos;
                return {};
            }

            const size_t length = std::stoull(line.substr(offset, delimiter - offset));

            if (line.size() - delimiter - 1 <= length) {
                offset = std::string::npos;
                return {};
            }

            std::string data = line.substr(delimiter + 1, length);
            const size_t new_offset = delimiter + 1 + length + 1;
            offset = new_offset < line.size() ? new_offset : std::string::npos;

            return {true, std::move(data)};
        }

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
    };

public:
    explicit fmanager(std::filesystem::path file_path) :
        _journal(file_path.parent_path() / (file_path.stem().string() + "_journal" + file_path.extension().string())),
        _root_path(std::move(file_path))
    {
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

    ~fmanager() {
        try {
            _consolidate();
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    template <typename... Args>
    void append(Args... args) {
        std::string text;
        (text.append(std::forward<Args>(args)), ...);

        _apply_append(text);
        _journal.record(Command::Append, std::move(text));
    }

    void overwrite(const size_t index, std::string text) {
        _apply_overwrite(index, text);
        _journal.record(Command::Overwrite, index, std::move(text));
    }

    void erase(const size_t index) {
        _apply_erase(index);
        _journal.record(Command::Erase, index);
    }

    void clear() {
        _apply_clear();
        _journal.record(Command::Clear);
    }

    void save() {
        _journal.save();
    }

private:
    void _init_cache() {
        std::ifstream in(_root_path);
        std::string line;
        size_t index = 0;

        while (std::getline(in, line)) {
            _cache.emplace_back(std::move(line));
            _index_order.emplace_back(index);
            ++index;
        }
    }

    void _consolidate() {
        if (!_needs_consolidation) return;

        std::filesystem::path write_path = _root_path;
        write_path.replace_extension(".tmp");
        std::ofstream out(write_path, std::ios::trunc);

        for (const auto index : _index_order) {
            out << _cache[index] << "\n";
        }

        out.close();
        std::error_code ec;
        std::filesystem::rename(write_path, _root_path, ec);

        if (ec || !_journal.destroy()) {
            _journal.save();
            return;
        };

        _needs_consolidation = false;
    }

    void _apply_append(std::string text) {
        _cache.emplace_back(std::move(text));
        _index_order.emplace_back(_cache.size() - 1);
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

    void _compact() {
        std::vector<std::string> new_cache;
        new_cache.reserve(_index_order.size());

        for (const auto index : _index_order) {
            new_cache.emplace_back(std::move(_cache[index]));
        }

        _cache = std::move(new_cache);
        _index_order.resize(_cache.size());
        std::iota(_index_order.begin(), _index_order.end(), 0);
    }

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
    std::filesystem::path _root_path;
    std::vector<std::string> _cache;
    std::vector<size_t> _index_order;
    bool _needs_consolidation = false;
};

#endif //FILEMANAGER_FILEMANAGER_H