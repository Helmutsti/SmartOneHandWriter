#include "sohw/candidate_provider.hpp"
#include "utf8.hpp"            // onehand::utf8ToW / wToUtf8

#include <cwctype>

namespace sohw {

static std::vector<std::string> toUtf8List(const std::vector<std::wstring>& v) {
    std::vector<std::string> out;
    out.reserve(v.size());
    for (auto& w : v) out.push_back(onehand::wToUtf8(w));
    return out;
}

static std::wstring lowerW(std::wstring w) {
    for (auto& c : w) c = static_cast<wchar_t>(towlower(c));
    return w;
}

std::vector<std::string> T9CandidateProvider::candidates(
    const std::string& input, int maxCand) const {
    std::wstring keys = onehand::utf8ToW(input);
    if (keys.empty()) return {};

    // Ogni simbolo -> gruppo di lettere del keymap (tasto non mappato = se stesso).
    std::vector<std::vector<wchar_t>> groups;
    groups.reserve(keys.size());
    for (wchar_t k : keys) groups.push_back(keymap_.groupOf(k));

    return toUtf8List(dict_.computeCandidatesPrefix(groups, maxCand));
}

std::vector<std::string> LiteralCandidateProvider::candidates(
    const std::string& input, int maxCand) const {
    std::wstring prefix = lowerW(onehand::utf8ToW(input));
    if (prefix.empty()) return {};

    if (!completion_) {
        // Pass-through: la parola digitata cosi' com'e' (nessuna ricerca).
        return { onehand::wToUtf8(prefix) };
    }
    return toUtf8List(dict_.completionsOf(prefix, maxCand));
}

} // namespace sohw
