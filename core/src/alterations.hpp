// Alterazioni di una parola: maiuscolo/minuscolo e accenti (IT). Sono mostrate
// DENTRO la lista dei suggerimenti, appese DOPO le parole reali del dizionario.
// Dettaglio interno del core.
#pragma once

#include <string>
#include <vector>

namespace onehand {

// Varianti case/accento di 'word', in ordine di priorita' (piu' utili prima).
// Include la forma base cosi' com'e'; il chiamante deduplica contro i candidati
// gia' presenti. Per parole di piu' lettere: {base, Capitalizzata, MAIUSCOLA}.
// Per una sola lettera: {min, MAI, varianti accentate min/MAI}.
std::vector<std::wstring> alterationsOf(const std::wstring& word);

// Applica la maiuscola iniziale (usata al render quando capFirst e' attivo).
std::wstring capitalizeFirst(const std::wstring& w);

} // namespace onehand
