#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace base32
{
    inline std::string base32_encode(const uint8_t* data, size_t len)
    {
        static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string out;
        out.reserve(((len + 4) / 5) * 8);

        size_t i = 0;
        while (i < len)
        {
            uint64_t buffer = 0;
            size_t bufferBits = 0;
            size_t chunkLen = 0;

            for (; chunkLen < 5 && i < len; ++chunkLen, ++i)
            {
                buffer = (buffer << 8) | data[i];
                bufferBits += 8;
            }

            buffer <<= (5 - chunkLen) * 8;
            bufferBits += (5 - chunkLen) * 8;

            while (bufferBits >= 5)
            {
                const uint32_t index = static_cast<uint32_t>((buffer >> (bufferBits - 5)) & 0x1fu);
                out.push_back(alphabet[index]);
                bufferBits -= 5;
            }
        }

        return out;
    }
}
