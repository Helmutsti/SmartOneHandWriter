// Conversione UTF-8 -> std::wstring, portabile (niente Win32).
// Sostituisce MultiByteToWideChar: su Windows (wchar_t a 16 bit) emette UTF-16
// con coppie surrogate, su Linux/macOS (wchar_t a 32 bit) emette code point.
#pragma once

#include <string>

namespace onehand {
std::wstring utf8ToW(const std::string& s);
}
