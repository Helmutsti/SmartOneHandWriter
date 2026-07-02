#include "sohw/bigram_model.hpp"
#include "sohw/bigram_format.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace sohw {

namespace {
// Lettura little-endian con controllo dei limiti.
struct Reader {
    const unsigned char* p;
    std::size_t n, i = 0;
    bool ok = true;
    bool need(std::size_t k) { if (i + k > n) { ok = false; return false; } return true; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = p[i] | (p[i+1] << 8); i += 2; return v; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(p[i]) | (static_cast<uint32_t>(p[i+1]) << 8) |
                     (static_cast<uint32_t>(p[i+2]) << 16) | (static_cast<uint32_t>(p[i+3]) << 24);
        i += 4; return v;
    }
};
} // namespace

bool BigramModel::load(const std::string& path) {
    vocab_.clear(); id_.clear(); uni_.clear(); uniTotal_ = 0;
    offsets_.clear(); w2_.clear(); cnt_.clear();

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 20) return false;

    Reader r{reinterpret_cast<const unsigned char*>(buf.data()), buf.size()};
    if (std::memcmp(buf.data(), kBigramMagic, 4) != 0) return false;
    r.i = 4;
    uint32_t version = r.u32();
    if (version != kBigramVersion) return false;
    r.u32();                 // topK (diagnostica)
    r.u32();                 // minCount (diagnostica)
    uint32_t V = r.u32();
    if (!r.ok) return false;

    vocab_.reserve(V);
    id_.reserve(V * 2);
    for (uint32_t idx = 0; idx < V; ++idx) {
        uint16_t len = r.u16();
        if (!r.need(len)) return false;
        std::string w(reinterpret_cast<const char*>(r.p + r.i), len);
        r.i += len;
        id_.emplace(w, idx);
        vocab_.push_back(std::move(w));
    }
    if (!r.ok) return false;

    uni_.resize(V);
    for (uint32_t idx = 0; idx < V; ++idx) { uni_[idx] = r.u32(); uniTotal_ += uni_[idx]; }
    if (!r.ok) return false;

    offsets_.resize(V + 1);
    for (uint32_t idx = 0; idx <= V; ++idx) offsets_[idx] = r.u32();
    if (!r.ok) return false;

    uint32_t P = r.u32();
    if (!r.ok) return false;
    if (offsets_[V] != P) return false;   // coerenza CSR
    w2_.resize(P);
    cnt_.resize(P);
    for (uint32_t k = 0; k < P; ++k) { w2_[k] = r.u32(); cnt_[k] = r.u32(); }
    if (!r.ok) { vocab_.clear(); return false; }
    return true;
}

int BigramModel::idOf(const std::string& word) const {
    auto it = id_.find(word);
    return it == id_.end() ? -1 : static_cast<int>(it->second);
}

uint32_t BigramModel::count(const std::string& w1, const std::string& w2) const {
    int a = idOf(w1), b = idOf(w2);
    if (a < 0 || b < 0) return 0;
    const uint32_t ub = static_cast<uint32_t>(b);
    for (uint32_t k = offsets_[a]; k < offsets_[a + 1]; ++k)
        if (w2_[k] == ub) return cnt_[k];
    return 0;
}

uint32_t BigramModel::unigramCount(const std::string& word) const {
    int a = idOf(word);
    return a < 0 ? 0u : uni_[a];
}

uint64_t BigramModel::rowTotal(const std::string& w1) const {
    int a = idOf(w1);
    if (a < 0) return 0;
    uint64_t s = 0;
    for (uint32_t k = offsets_[a]; k < offsets_[a + 1]; ++k) s += cnt_[k];
    return s;
}

std::vector<std::pair<std::string, uint32_t>> BigramModel::successors(
    const std::string& w1, int maxN) const {
    std::vector<std::pair<std::string, uint32_t>> out;
    int a = idOf(w1);
    if (a < 0) return out;
    // Le righe sono gia' ordinate per conteggio desc dal tool.
    for (uint32_t k = offsets_[a]; k < offsets_[a + 1]; ++k) {
        if (maxN > 0 && static_cast<int>(out.size()) >= maxN) break;
        out.push_back({vocab_[w2_[k]], cnt_[k]});
    }
    return out;
}

} // namespace sohw
