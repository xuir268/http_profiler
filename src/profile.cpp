#include "profile.h"

thread_local std::vector<std::string> CallStack::caller_stack;
