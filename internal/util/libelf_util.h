#pragma once

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>

#include "capybara/runtime.h"

// This function lets us traverse a DWARF Die and collect symbol
// information, this is a CORE function and should only be used by
// capybara
void traverse_and_collect(const dwarf::die& d, std::vector<CapyString>& scope_stack, RuntimeStorage& storage, std::vector<_SymbolMetaData>& outSymbols);

std::vector<_SymbolMetaData> process_library(const elf::elf& ef, const std::vector<_SymbolMetaData>& symbols, CapyActiveDomain& storage);
