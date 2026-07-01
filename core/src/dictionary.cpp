#include "dictionary.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <set>

namespace onehand {

void Dictionary::load(std::istream& f) {
    words_.clear();

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
}

std::vector<std::wstring> Dictionary::computeCandidates(
    const std::vector<std::vector<wchar_t>>& groups, int maxCand) const {
    std::vector<std::wstring> out;
    if (groups.empty()) return out;

    const std::size_t n = groups.size();
    std::vector<std::pair<std::wstring, double>> m;
    for (auto& e : words_) {
        if (e.w.size() != n) continue;
        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            const std::vector<wchar_t>& g = groups[i];
            if (std::find(g.begin(), g.end(), e.w[i]) == g.end()) { ok = false; break; }
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
    return out;
}

} // namespace onehand
