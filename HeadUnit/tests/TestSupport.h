#pragma once
#include "logging/Logger.h"
#include <string>

void Check(bool isValid, const std::string& message);
headunit::Logger& TestLogger();
