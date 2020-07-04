#pragma once
#include <ida.hpp>

#ifndef NDEBUG
#define wc_debug(format, ...) msg (format, ##__VA_ARGS__)
#else
#define wc_debug
#endif

#define wc_error(format, ...) msg (format, ##__VA_ARGS__)
