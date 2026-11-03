#include "utils/file_utils.h"

bool createDirectory(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	struct stat st;
	if (stat(path.c_str(), &st) == 0) {
		return true;
	}
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0;
#else
	return mkdir(path.c_str(), 0755) == 0;
#endif
}