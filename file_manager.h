#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>

class FileManager {
private:
	const std::filesystem::path _recoveryPath;
	const std::filesystem::path _filePath;
	std::vector<std::string> _cache;
	std::vector<size_t> _rowMapping;
	size_t _appendedRows = 0;
	bool _modified = false;
	bool _recoveryMode = false;

	enum class Error {
		FailedOpeningFile
	};

	[[noreturn]] void _throw(Error e, const std::string& extra = "") {
		switch (e) {
		case Error::FailedOpeningFile:
			throw std::runtime_error("<FileManager> Couldn't open file " + extra);
		}
	}

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

public:
	FileManager(std::filesystem::path filePath) :
		_recoveryPath(filePath.parent_path() / ("RECOVERY_" + filePath.filename().string())),
		_filePath(std::move(filePath))
	{
		if (std::filesystem::exists(_recoveryPath)) {
			_initCache(_recoveryPath);
			_recoveryMode = true;
		}
		else {
			if (!std::filesystem::exists(_filePath)) {
				std::filesystem::create_directories(_filePath.parent_path());
				std::ofstream{ _filePath }.close();
			}

			_initCache(_filePath);
		}
	}

	~FileManager() {
		save();
	}

	template<typename... Args>
	void append(Args... args) {
		std::stringstream ss;
		(ss << ... << args);

		_cache.push_back(ss.str());
		_rowMapping.push_back(_cache.size() - 1);
		++_appendedRows;
	}

	void save() {
		bool successful = false;

		if (_modified || _recoveryMode) {
			std::ofstream out(_filePath);

			if (out.is_open()) {
				for (size_t row : _rowMapping) {
					out << _cache[row] << "\n";
				}

				successful = true;
			}
		}
		else if (_appendedRows > 0) {
			std::ofstream out(_filePath, std::ios::app);

			if (out.is_open()) {
				for (size_t totalRows = _rowMapping.size(), row = totalRows - _appendedRows; row < totalRows; ++row) {
					out << _cache[row] << "\n";
				}

				successful = true;
			}
		}

		if (successful) {
			_appendedRows = 0;
			_modified = false;

			if (_recoveryMode) {
				std::error_code ec;
				std::filesystem::remove(_recoveryPath, ec);

				_recoveryMode = false;
			}
		}
	}
};