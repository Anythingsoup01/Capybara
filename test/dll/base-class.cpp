#include "base-class.h"
#include <stdexcept>
#include <stdio.h>
#include <iostream>

INITIALIZE_INTERNAL_CALL(Internal_Add, int);
INITIALIZE_INTERNAL_CALL(Internal_Add, float);

ADD_INTERNAL_TEMPLATE_CALL(Internal_Add, int);
ADD_INTERNAL_TEMPLATE_CALL(Internal_Add,float);

namespace DLL
{
    Base::Base()
    {
        printf("Base Class!\n");
    }
}
