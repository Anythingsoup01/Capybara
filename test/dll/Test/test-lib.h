#pragma once
#include "base-class.h"

#include <cstdint>

namespace DLL
{
    class Test : public Base
    {
    public:
        void Print(const char* msg) override;
        void PrintAgain() override;

        void CustomAdd(int a, int b);

        void UpdateCheck(int16_t test);
    private:
        const char* m_PreviousMessage;
    };
}
