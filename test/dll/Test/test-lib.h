#pragma once
#include "base-class.h"

#include <cstdint>

namespace DLL
{
    class Test : public Base
    {
    public:
        void PrintBaseInt();

        void Print(const char* msg);
        void PrintAgain();

        void CustomAdd(int a, int b);

        void UpdateCheck(int16_t test);

    private:
        const char* m_PreviousMessage;
    };
}
