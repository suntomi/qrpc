#pragma once

// Wrapper header for nlohmann/json.hpp to suppress GCC 13 false positive warnings
// with std::function and std::optional in regex automaton and functional headers

#include "base/macros.h"

DISABLE_MAYBE_UNINITIALIZED_WARNING_PUSH
#include <nlohmann/json.hpp>
DISABLE_MAYBE_UNINITIALIZED_WARNING_POP
