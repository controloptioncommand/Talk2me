#pragma once
#include <cstdint>
#include <string>

// Фонемы английского (ARPAbet, как в CMUdict) и их цели для формантного синтеза.
// Значения формант и амплитуд — по таблицам Клатта (1980) и правилам длительностей Аллена — Ханникатта — Клатта (1987),
// подогнаны на слух и по распознаванию.
namespace t2m
{
enum class Kind : uint8_t { Vowel, Diphthong, Liquid, Glide, Nasal, Fricative, Stop, Affricate, Aspirate, Pause };

enum Ph : uint8_t
{
    AA, AE, AH, AO, AW, AY, EH, ER, EY, IH, IY, OW, OY, UH, UW, AX, AXR,
    B, CH, D, DH, F, G, HH, JH, K, L, M, N, NG, P, R, S, SH, T, TH, V, W, Y, Z, ZH,
    PAU, NumPh
};

struct PhonDef
{
    const char* name;
    Kind kind;
    bool voiced;
    float dur, minDur;              // мс: собственная и минимальная длительность
    float f1, f2, f3, b1, b2, b3;   // цели формант (Гц); у смычных — локус
    float f1b, f2b, f3b;            // вторая цель у дифтонгов (0 — нет)
    float av;                       // голос 0…1
    float af;                       // шум (фрикация) 0…1
    float a2, a3, a4, a5, a6, ab;   // параллельные полосы шума, дБ (0 — выкл.)
    float fnz;                      // нуль носового резонатора (Гц), 270 — компенсирован
};

const PhonDef& phon (Ph p);
bool isVowel (Ph p);
bool isConsonant (Ph p);
Ph phFromName (const std::string& arpabetWithoutStress);   // NumPh — не найдено
} // namespace t2m
