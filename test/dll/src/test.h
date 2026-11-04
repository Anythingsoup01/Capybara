#pragma once

#include "base.h"

namespace John {

class Test : public Base {
  Test();
  ~Test();
  virtual void PrintI(const int &in) override;

private:
  int m_MyInt = 0;
};

}
