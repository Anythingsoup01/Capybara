#pragma once
#include <cstdint>
#include "internal_calls.h"


namespace DLL
{
    struct BigChungus
    {
        uint64_t Doc;

        void Test();
    };

    class Base
    {
    public:
        Base();

        int Add(int a, int b);


        uint64_t ID;
        BigChungus Bugs;
    };
}
