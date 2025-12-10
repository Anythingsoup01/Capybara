#pragma once

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <ffi.h>
#include <dlfcn.h>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <algorithm>


#include <sys/mman.h>
#include <link.h>
#include <cerrno>
#include <cstring>

