#ifndef APP_DOCTEST_H
#define APP_DOCTEST_H

#include "thirdparty/doctest/doctest.h"

// some additional helpers
#include <string>

template <typename T>
std::string type_string() {
    return doctest::detail::type_to_string<T>();
}

template <typename T>
std::string type_string(T const&) {
    return type_string<T>();
}

#endif
