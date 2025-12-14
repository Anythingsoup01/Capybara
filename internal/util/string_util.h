#pragma once

// This function lets you compare multiple strings to a main string
// however this one doesn't check length.
// TODO: Check if this is being used
bool strs_n_equal(const std::string& mainString, const std::vector<std::string>& comparedTo);

// This function let's you check against a single string while
// also checking the length of the strings, if they aren't equal
// this will return false
bool str_n_equal_length_check(const std::string& mainString, const std::vector<std::string>& comparedTo);


