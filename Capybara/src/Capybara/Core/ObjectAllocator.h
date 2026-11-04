#pragma once

#include "Core/ReflectionTypes.h"
namespace Capybara {

class ObjectAllocator {
public:
  static std::shared_ptr<CapyObject> CreateCapyObject(void *dlHandle, const ClassLayoutInfo &layout);
};

}
