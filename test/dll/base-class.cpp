#include "base-class.h"
#include <stdio.h>

namespace DLL
{

    INITIALIZE_INTERNAL_CALL(Internal_Add, int);
    INITIALIZE_INTERNAL_CALL(Internal_Add, float);

    ADD_INTERNAL_TEMPLATE_CALL(Internal_Add, int);
    ADD_INTERNAL_TEMPLATE_CALL(Internal_Add,float);

    Base::Base()
    {
        printf("Base Class!\n");
    }
}
