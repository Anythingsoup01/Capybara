#pragma once

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>

#include "capybara/runtime.h"

// This function will get the name at any give die, if no name is
// found <anon> will be returned
std::string get_short_name(const dwarf::die& die);

// This function will recursively build the ValueType as a string
std::string resolve_type(const dwarf::die& type_die);

// This function will give the return type name of a give dwarf::die
std::string get_return_type(const dwarf::die& die);

// This function lets us traverse a DWARF Die and collect symbol
// information, this is a CORE function and should only be used by
// capybara
void traverse_and_collect(const dwarf::die& d, std::vector<std::string>& scope_stack, RuntimeStorage& storage, std::vector<_SymbolMetaData>& outSymbols);

std::vector<_SymbolMetaData> process_library(const elf::elf& ef, const std::vector<_SymbolMetaData>& symbols, CapyStorage& storage);
