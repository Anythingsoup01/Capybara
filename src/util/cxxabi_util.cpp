#include "util/cxxabi_util.h"

#include <cxxabi.h>

std::string demangle_symbol_name(const char* name)
{
    int status = 0;
    // Call the ABI demangling function
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (status == 0) {
        std::string result(demangled);
        free(demangled);
        return result;
    }
    return name;
}
