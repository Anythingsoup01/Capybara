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
        Base m_Base;
        uint64_t ID;

        float m_FloatOne;
        float m_FloatTwo;
        float m_FloatThree;

        bool m_BoolOne;
        bool m_BoolTwo;
        bool m_BoolThree;
    };
}
