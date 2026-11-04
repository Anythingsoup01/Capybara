#pragma once

#include "ReflectionTypes.h"
namespace Capybara {

class ObjectAllocator {
public:
  static std::shared_ptr<CapyObject> CreateCapyObject(void *dlHandle, const ClassLayoutInfo &layout);
};

}
