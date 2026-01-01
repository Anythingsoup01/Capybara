#pragma once
#include "base-class.h"

#include <cstdint>

namespace DLL
{
    class Test : public Base // 16 bytes (0)
    {
    public:
        void PrintBaseInt();

        void CustomAdd(int a, int b);


    private:
        Base m_Base; // 16 bytes (16)
        uint64_t ID; // 8 bytes (32)

        float m_FloatOne; // 4 bytes (40)
        float m_FloatTwo; // 4 bytes (44)
        float m_FloatThree; // 4 bytes (48)

        bool m_BoolOne; // 1 bytes (52)
        bool m_BoolTwo; // 1 bytes (53)
        bool m_BoolThree; // 1 bytes (54)
                          // total size 55
                          // minus base 16
                          // derived size 39
    };
}
