#include "test.h"

#include <iostream>

namespace John {

Test::Test() {
  m_MyInt = 6;
  std::cout << "My Int Is: " << m_MyInt << "\n";
}

Test::~Test() {

}

void Test::PrintI(const int &in) {
  std::cout << "Your Int Is: " << in << "\n";
}

}
