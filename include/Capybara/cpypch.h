#pragma once

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>
#include <ffi.h>
#include <dlfcn.h>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <memory>

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>
