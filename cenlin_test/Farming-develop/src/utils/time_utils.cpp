#include "utils/time_utils.h"
#include <ctime>

const std::string getCurrentTime() {
	std::time_t now = std::time(nullptr);
	std::tm* tmNow = std::localtime(&now);

	if (!tmNow) {
		return "1970-01-01 00:00:00";
	}

	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmNow);
	return std::string(buf);
}