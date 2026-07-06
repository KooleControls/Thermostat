#pragma once
#include <cstdint>
#include <cmath>

// 32-bit OpenTherm frame helpers: [31]=even parity  [30:28]=msg type
// [27:24]=spare  [23:16]=data-ID  [15:0]=data value. f8.8 = signed
// fixed-point temperature encoding.
namespace OtFrame
{
    enum MsgType : uint8_t
    {
        ReadData      = 0,
        WriteData     = 1,
        InvalidData   = 2,
        ReadAck       = 4,
        WriteAck      = 5,
        DataInvalid   = 6,
        UnknownDataId = 7,
    };

    inline uint32_t Build(MsgType type, uint8_t id, uint16_t value)
    {
        uint32_t f = ((uint32_t)(type & 0x7) << 28) | ((uint32_t)id << 16) | value;
        uint32_t v = f;
        int ones = 0;
        while (v) { ones += v & 1; v >>= 1; }
        if (ones & 1) f |= 0x80000000u;   // even parity over all 32 bits
        return f;
    }

    inline MsgType  Type(uint32_t f)  { return (MsgType)((f >> 28) & 0x7); }
    inline uint8_t  Id(uint32_t f)    { return (f >> 16) & 0xFF; }
    inline uint16_t Value(uint32_t f) { return f & 0xFFFF; }

    inline uint16_t F88(float v)      { return (uint16_t)(int16_t)lroundf(v * 256.0f); }
    inline float    FromF88(uint16_t v) { return (int16_t)v / 256.0f; }
}
