#pragma once

#include "Transform.h"
#include <cstdint>

namespace CapyRuntime {

class Object {
protected:
  uint64_t m_EntityID = 0;

public:
  Object() = default;
  Object(uint64_t entityID) : m_EntityID(entityID) {}

  Transform transform() { return Transform{m_EntityID}; }

};
} // namespace CatRuntime
