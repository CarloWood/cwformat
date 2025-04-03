#pragma once

#include "cwds/debug.h"

#ifdef HAVE_UTILS_CONFIG_H
// Add support for classes with a print_on method, defined in global namespace.
#include "utils/has_print_on.h"
// Add catch all for global namespace.
using utils::has_print_on::operator<<;

#endif // HAVE_UTILS_CONFIG_H

