#pragma once

#include <Object.h>

class Base : public CapyRuntime::Object {
public:
  virtual void Print();
  virtual void PrintI(const int &in) = 0;
};
