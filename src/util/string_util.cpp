#include "string_util.h"

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
bool str_n_equal(const std::string& mainString, const std::string& comparedTo)
{
    if (mainString.length() != comparedTo.length())
        return false;

    if (strncmp(mainString.c_str(), comparedTo.c_str(), mainString.length()) == 0)
    {
        return true;
    }

    return false;
}
