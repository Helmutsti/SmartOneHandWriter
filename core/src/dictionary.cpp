#include "dictionary.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

namespace onehand {

void Dictionary::load(std::istream& f, const std::wstring& available, bool wildcardAny) {
    words_.clear();
    wildSet_.clear();
    wildAny_ = wildcardAny;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }

    const long total = static_cast<long>(lines.size());
    long rank = 0;
    for (auto& ln : lines) {
        // separatore: tab oppure spazio
        std::size_t sep = ln.find('\t');
        if (sep == std::string::npos) sep = ln.find(' ');
        std::string ws = (sep == std::string::npos) ? ln : ln.substr(0, sep);
        std::string fs = (sep == std::string::npos) ? "" : ln.substr(sep + 1);

        std::wstring w = utf8ToW(ws);
        for (auto& c : w) c = static_cast<wchar_t>(towlower(c));
        if (w.empty()) { ++rank; continue; }

        double freq;
        if (!fs.empty()) {
            freq = atof(fs.c_str());
            if (freq == 0.0) freq = static_cast<double>(total - rank);
        } else {
            freq = static_cast<double>(total - rank);
        }
        words_.push_back({w, freq});
        ++rank;
    }

    // insieme dei caratteri che un jolly puo' rappresentare
    if (!wildAny_ && !available.empty()) {
        std::set<wchar_t> avail(available.begin(), available.end());
        std::set<wchar_t> alpha;
        for (auto& e : words_) for (wchar_t c : e.w) alpha.insert(c);
        for (wchar_t c : alpha) if (!avail.count(c)) wildSet_.insert(c);
    } else {
        wildAny_ = true;
    }
}

std::vector<std::wstring> Dictionary::computeCandidates(const std::wstring& pat, int maxCand) const {
    std::vector<std::wstring> out;
    if (pat.empty()) return out;
    if (pat.find(L'?') == std::wstring::npos) { out.push_back(pat); return out; }

    const std::size_t n = pat.size();
    std::vector<std::pair<std::wstring, double>> m;
    for (auto& e : words_) {
        if (e.w.size() != n) continue;
        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            wchar_t pc = pat[i], wc = e.w[i];
            if (pc == L'?') {
                if (!wildAny_ && !wildSet_.count(wc)) { ok = false; break; }
            } else if (pc != wc) { ok = false; break; }
        }
        if (ok) m.push_back({e.w, e.f});
    }
    std::sort(m.begin(), m.end(),
              [](const std::pair<std::wstring, double>& a,
                 const std::pair<std::wstring, double>& b) { return a.second > b.second; });

    std::set<std::wstring> seen;
    for (auto& p : m) {
        if (seen.count(p.first)) continue;
        seen.insert(p.first);
        out.push_back(p.first);
        if (static_cast<int>(out.size()) >= maxCand) break;
    }
    if (out.empty()) out.push_back(pat);
    return out;
}

} // namespace onehand
