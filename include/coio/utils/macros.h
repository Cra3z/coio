#pragma once
#include "../detail/config.h"

#define COIO_STRINGTIFY_IMPL(x) #x

#define COIO_STRINGTIFY(x) COIO_STRINGTIFY_IMPL(x)
