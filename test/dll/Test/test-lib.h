#pragma once
#include "base-class.h"

#include <cstdint>

namespace DLL
{
    class Test : public Base // 12 bytes (0)
    {
    public:
        void PrintBaseInt();

        void Print(const char* msg);
        void PrintAgain();

        void CustomAdd(int a, int b);

        void UpdateCheck(int16_t test);

        private:
        Base m_Base; // 12 bytes (12)
        uint64_t ID; // 8 bytes (24)

        float m_FloatOne; // 4 bytes (32)
        float m_FloatTwo; // 4 bytes (36)
        float m_FloatThree; // 4 bytes (40)

        bool m_BoolOne; // 1 bytes (44)
        bool m_BoolTwo; // 1 bytes (45)
        bool m_BoolThree; // 1 bytes (46)
                          // total size 47
    };
}
