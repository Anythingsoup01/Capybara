#include "util/string_util.h"

#include <string.h>

bool strs_n_equal(const std::string& mainString, const std::vector<std::string>& comparedTo)
{
    bool isEqual = false;

    for (auto& check : comparedTo)
    {
        if (strncmp(mainString.c_str(), check.c_str(), check.length()) == 0)
        {
            isEqual = true;
            break;
        }
    }
    return isEqual;
}

// This one checks the length
bool str_n_equal_length_check(const std::string& mainString, const std::vector<std::string>& comparedTo)
{


    bool isEqual = false;

    for (auto& check : comparedTo)
    {
        if (mainString.length() != check.length())
            continue;

        if (strncmp(mainString.c_str(), check.c_str(), check.length()) == 0)
        {
            isEqual = true;
            break;
        }
    }
    return isEqual;
}
