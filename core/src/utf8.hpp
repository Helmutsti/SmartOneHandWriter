// Conversione UTF-8 -> std::wstring, portabile (niente Win32).
// Sostituisce MultiByteToWideChar: su Windows (wchar_t a 16 bit) emette UTF-16
// con coppie surrogate, su Linux/macOS (wchar_t a 32 bit) emette code point.
#pragma once

#include <string>

namespace onehand {
std::wstring utf8ToW(const std::string& s);

// Conversione inversa: std::wstring -> UTF-8. Su Windows (wchar_t 16 bit)
// ricompone le coppie surrogate; su Linux/macOS tratta ogni wchar_t come code
// point. Serve al confine UTF-8 del CORE "nuova concezione".
std::string wToUtf8(const std::wstring& w);
}
