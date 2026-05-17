#pragma once

#include <sys/stat.h>
#include <errno.h>
#include <string>

bool createDirectory(const std::string& path);