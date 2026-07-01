#include "utf8.hpp"

namespace onehand {

std::wstring utf8ToW(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int len;
        if (c < 0x80)            { cp = c;          len = 1; }
        else if ((c >> 5) == 0x6){ cp = c & 0x1F;   len = 2; }
        else if ((c >> 4) == 0xE){ cp = c & 0x0F;   len = 3; }
        else if ((c >> 3) == 0x1E){ cp = c & 0x07;  len = 4; }
        else                     { cp = 0xFFFD;     len = 1; }  // byte di testa non valido

        if (i + static_cast<std::size_t>(len) > n) {
            cp = 0xFFFD; len = 1;
        } else {
            for (int k = 1; k < len; ++k) {
                unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; len = 1; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        i += static_cast<std::size_t>(len);

        if (sizeof(wchar_t) >= 4) {
            out.push_back(static_cast<wchar_t>(cp));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

} // namespace onehand
