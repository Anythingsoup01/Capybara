#include "Capybara/cpch.h"
#include "ObjectAllocator.h"

#include <dlfcn.h>

namespace Capybara {

std::shared_ptr<CapyObject> ObjectAllocator::CreateCapyObject(void *dlHandle, const ClassLayoutInfo &layout) {
  auto obj = std::make_shared<CapyObject>();
  obj->ClassName = layout.ClassName;
  obj->TotalSize = layout.TotalSize;
  obj->BaseClasses = layout.BaseClasses;

  // Copy Fields
  for (const auto &field : layout.Members) {
    FieldInfo info;
    info.Name = field.Name;
    info.ByteOffset = field.ByteOffset;
    info.SizeInBytes = field.SizeInBytes;
    info.TypeName = field.TypeName;
    obj->Fields.push_back(info);
  }

  // Initialize Methods
  for (const auto &method : layout.MethodSymbols) {
    MethodInfo info;
    info.Name = method.DemangledName;
    info.SymbolPointer = dlsym(dlHandle, method.Name.c_str());
    if (!info.SymbolPointer) {
      dlerror();
    }
    obj->Methods.push_back(std::move(info));
  }

  obj->ConstructorPointer = dlsym(dlHandle, layout.ConstructorName.c_str());
  obj->DestructorPointer = dlsym(dlHandle, layout.DestructorName.c_str());

  dlerror();

  return obj;
}

}
