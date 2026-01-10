#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <numeric>

#define UNUSED_ROWS_TRESHOLD 25

class FileManager {
private:
	const std::filesystem::path _recoveryPath;
	const std::filesystem::path _filePath;
	std::vector<std::string> _cache;
	std::vector<size_t> _rowMapping;
	size_t _appendedRows = 0;
	bool _rewriteNecessary = false;
	bool _recoveryExists = false;

	enum class Error {
		FailedOpeningFile,
		RowOutOfBounds,
		FailedSaving
	};

	enum class SaveMode {
		Best,
		Rewrite,
		Append
	};

	/**
	 * @brief
	 * Throws an exception based on the specified error with optional details.
	 *
	 * @param error
	 * An error linked to an exception.
	 * 
	 * @param extra
	 * Optional details which will be added to the end of the exception.
	 */
	[[noreturn]] void _throw(Error error, const std::string& extra = "") const {
		switch (error) {
		case Error::FailedOpeningFile:
			throw std::runtime_error("<FileManager> Couldn't open file " + extra);
		case Error::RowOutOfBounds:
			throw std::out_of_range("<FileManager> Specified row is out of bounds " + extra);
		default:
			throw std::runtime_error("<FileManager> Unknown exception");
		}
	}

	/**
	 * @brief
	 * Initializes the file manager with the content of the specified file path.
	 * 
	 * @param filePath
	 * A path which points to a file the file manager should manage.
	 */
	void _initCache(const std::filesystem::path& filePath) {
		std::ifstream in(filePath);

		if (!in.is_open()) {
			_throw(Error::FailedOpeningFile, filePath.string());
		}

		std::string rowContent;
		size_t row = 0;

		while (std::getline(in, rowContent)) {
			_cache.push_back(std::move(rowContent));
			_rowMapping.push_back(row++);
		}
	}

	/**
	 * @brief
	 * Attempts to save the current state of the file manager to the given file path.
	 * 
	 * @param filePath
	 * A path that points to file which should store the save data.
	 * 
	 * @param saveMode
	 * A mode which decides in which way the file manager will try to save.
	 * 
	 * @return
	 * Whether saving was successful.
	 * 
	 * @note
	 * If the given file doesn't exist, a full rewrite will be forced. Otherwise the specified save mode unless it's
	 * SaveMode::Best. If SaveMode::Best is specified as the save mode, the program will decide itself whether rewriting 
	 * the whole file is really necessary.
	 */
	bool _saveToFile(const std::filesystem::path& filePath, SaveMode saveMode) {
		if (!std::filesystem::exists(filePath)) {
			if (filePath.has_parent_path()) {
				std::filesystem::create_directories(filePath.parent_path());
			}

			saveMode = SaveMode::Rewrite;
		}

		if (saveMode == SaveMode::Best) {
			saveMode = _rewriteNecessary ? SaveMode::Rewrite : SaveMode::Append;
		}

		std::ios::openmode mode = saveMode == SaveMode::Rewrite ? std::ios::out : std::ios::app;
		std::ofstream out{ filePath, mode };

		if (!out.is_open()) {
			return false;
		}

		if (saveMode == SaveMode::Rewrite) {
			for (size_t rowIdx : _rowMapping) {
				out << _cache[rowIdx] << "\n";
			}
		}
		else {
			size_t total = _rowMapping.size();

			for (size_t rowIdx = total - _appendedRows; rowIdx < total; ++rowIdx) {
				out << _cache[_rowMapping[rowIdx]] << "\n";
			}
		}

		return true;
	}

	/**
	 * @brief 
	 * Deletes all unused elements in the cache and rebuilds the internal storage.
	 * 
	 * @note
	 * Important because if the user deletes rows often, there will be unreferenced elements left which can accumulate
	 * and create a memory leak.
	 */
	void _cleanGarbage() {
		if (_rowMapping.empty()) {
			_cache.clear();
			return;
		}

		std::vector<std::string> newCache;
		newCache.reserve(_rowMapping.size());

		for (size_t rowIdx : _rowMapping) {
			newCache.push_back(std::move(_cache[rowIdx]));
		}

		_cache = std::move(newCache);
		_rowMapping.resize(_cache.size());
		std::iota(_rowMapping.begin(), _rowMapping.end(), 0);
	}

public:
	FileManager(std::filesystem::path filePath) :
		_recoveryPath(filePath.parent_path() / ("RECOVERY_" + filePath.filename().string())),
		_filePath(std::move(filePath))
	{
		if (std::filesystem::exists(_recoveryPath)) {
			_initCache(_recoveryPath);
			_recoveryExists = true;
		}
		else if (std::filesystem::exists(_filePath)) {
			_initCache(_filePath);
		}
	}

	~FileManager() {
		save();
	}

	/**
	 * @brief 
	 * Returns the text at the specified row.
	 * 
	 * @param row 
	 * The row to read.
	 */
	std::string read(size_t row) const {
		if (row >= _rowMapping.size()) {
			_throw(Error::RowOutOfBounds);
		}

		return _cache[_rowMapping[row]];
	}

	/**
	 * @brief 
	 * Splits the text at the specified row by the specified delimiter and returns the parts.
	 * 
	 * @param row
	 * The row at which the text should be split at.
	 * 
	 * @param delimiter 
	 * The delimiter the text should be split after.
	 * 
	 * @return
	 * Every part of the split text.
	 */
	std::vector<std::string> split(size_t row, char delimiter) const {
		if (row >= _rowMapping.size()) {
			_throw(Error::RowOutOfBounds);
		}

		std::vector<std::string> result;
		std::stringstream ss(_cache[_rowMapping[row]]);
		std::string part;

		while (std::getline(ss, part, delimiter)) {
			result.push_back(std::move(part));
		}

		return result;
	}

	/**
	 * @brief 
	 * Returns the text at the first row of the file.
	 */
	std::string first() const {
		if (_rowMapping.empty()) {
			_throw(Error::RowOutOfBounds);
		}

		return _cache[_rowMapping[0]];
	}

	/**
	 * @brief
	 * Returns the text at the last row of the file.
	 */
	std::string last() const {
		if (_rowMapping.empty()) {
			_throw(Error::RowOutOfBounds);
		}

		return _cache[_rowMapping[_rowMapping.size() - 1]];
	}

	/**
	 * @brief 
	 * Returns a copy of every row.
	 */
	std::vector<std::string> all() const {
		std::vector<std::string> result;
		result.reserve(_rowMapping.size());

		for (size_t rowIdx : _rowMapping) {
			result.push_back(_cache[rowIdx]);
		}

		return result;
	}

	/**
	 * @brief
	 * Adds the given arguments to a new row at the end of the file.
	 * 
	 * @param ...args
	 * The content to add.
	 */
	template<typename... Args>
	void append(Args... args) {
		std::stringstream ss;
		(ss << ... << args);

		_cache.push_back(ss.str());
		_rowMapping.push_back(_cache.size() - 1);
		++_appendedRows;
	}

	/**
	 * @brief 
	 * Overwrites the text at the specified row with the specified arguments.
	 * 
	 * @param row 
	 * The row at which the text will be overwritten.
	 * 
	 * @param ...args
	 * The content which the row will be overwritten with.
	 */
	template<typename... Args>
	void overwrite(size_t row, Args... args) {
		if (row >= _rowMapping.size()) {
			_throw(Error::RowOutOfBounds);
		}

		std::stringstream ss;
		(ss << ... << args);

		_cache[_rowMapping[row]] = ss.str();
		_rewriteNecessary = true;
	}

	/**
	 * @brief 
	 * Deletes the specified row, shifting all later elements down.
	 * 
	 * @param row 
	 * The row which will be deleted.
	 * 
	 * @note
	 * If user deletes row which isn't saved yet (appended row), appended rows count will be reduced instead of
	 * forcing a full rewrite on next save.
	 * Cleans garbage after the cache exceeds 25 unused elements. Can lead to a slight delay, depending on the
	 * size of the cache.
	 */
	void erase(size_t row) {
		if (row >= _rowMapping.size()) {
			_throw(Error::RowOutOfBounds);
		}

		if (row >= _rowMapping.size() - _appendedRows) {
			--_appendedRows;
		}
		else {
			_rewriteNecessary = true;
		}

		_rowMapping.erase(_rowMapping.begin() + row);

		if (_cache.size() > _rowMapping.size() + UNUSED_ROWS_TRESHOLD) {
			_cleanGarbage();
		}
	}

	/**
	 * @brief 
	 * Deletes all rows.
	 * 
	 * @note
	 * Cleans garbage to reduce memory usage.
	 */
	void clear() {
		_rowMapping.clear();
		_rewriteNecessary = true;
		_cleanGarbage();
	}

	/**
	 * @brief 
	 * Saves all changes to the file or the recovery file.
	 * 
	 * @note
	 * If the file manager is in recovery state, he will always firstly try saving the data to the main file.
	 * If saving doesn't succeed, the file manager will continue to use the recovery file, otherwise he will delete
	 * the recovery file. If saving fails in both files, the manager throws an exception.
	 */
	void save() {
		if (!_recoveryExists && !_rewriteNecessary && _appendedRows == 0) {
			return;
		}

		bool success;

		if (_recoveryExists) {
			success = _saveToFile(_filePath, SaveMode::Rewrite);
		}
		else {
			success = _saveToFile(_filePath, SaveMode::Best);
		}

		if (!success && !_saveToFile(_recoveryPath, SaveMode::Best)) {
			_throw(Error::FailedSaving);
		}

		if (success && _recoveryExists) {
			std::error_code ec;
			std::filesystem::remove(_recoveryPath, ec);
			_recoveryExists = false;
		}

		_rewriteNecessary = false;
		_appendedRows = 0;
	}

	/**
	 * @brief
	 * Returns whether there are no present rows.
	 */
	bool empty() const noexcept {
		return _rowMapping.empty();
	}

	/**
	 * @brief
	 * Returns the number of present rows.
	 */
	size_t size() const noexcept {
		return _rowMapping.size();
	}
};