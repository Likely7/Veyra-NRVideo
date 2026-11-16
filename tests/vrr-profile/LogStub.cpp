#include "veyra/Log.h"

// Isolated tests do not initialize the player's logger or write application logs.
namespace veyra::log {
void info(const char*, const std::string&) {}
void warn(const char*, const std::string&) {}
} // namespace veyra::log
