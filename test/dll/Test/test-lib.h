#pragma once
#include "base-class.h"

#include <cstdint>

namespace DLL
{
    class Test : public Base // 4 bytes (0)
    {
    public:
        void PrintBaseInt();

        void CustomAdd(int a, int b);


    private:
        Base m_Base; // 4 bytes (4)
        uint64_t ID; // 8 bytes (8)

        float m_FloatOne; // 4 bytes (16)
        float m_FloatTwo; // 4 bytes (20)
        float m_FloatThree; // 4 bytes (24)

        bool m_BoolOne; // 1 bytes (28)
        bool m_BoolTwo; // 1 bytes (29)
        bool m_BoolThree; // 1 bytes (30)
                          // total size 31
                          // minus base 04
                          // derived size 27
    };
}
