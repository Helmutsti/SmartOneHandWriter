// SmartOneHandWriter - formato del modello bigrammi compatto (binario).
//
// Prodotto offline da tools/build_bigrams e letto a runtime da BigramModel.
// Little-endian. Struttura CSR (Compressed Sparse Row) per lookup rapido dei
// successori di una parola.
//
// Layout:
//   char     magic[4]      = {'S','H','W','B'}
//   uint32   version       = SHWB_VERSION
//   uint32   topK          // top-K successori tenuti per riga (info/diagnostica)
//   uint32   minCount      // soglia di conteggio applicata (info/diagnostica)
//   uint32   V             // numero di token nel vocabolario
//   -- tabella vocabolario, in ordine di id (0..V-1):
//   { uint16 len; char utf8[len]; } * V
//   -- indice CSR:
//   uint32   offsets[V+1]  // riga id -> [offsets[id], offsets[id+1]) in pairs
//   uint32   P             // numero totale di coppie (successori)
//   { uint32 w2_id; uint32 count; } * P   // ogni riga ordinata per count desc
#pragma once

#include <cstdint>

namespace sohw {

constexpr char     kBigramMagic[4] = {'S', 'H', 'W', 'B'};
constexpr uint32_t kBigramVersion  = 1u;

} // namespace sohw
