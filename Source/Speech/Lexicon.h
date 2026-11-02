#pragma once
#include "Phonemes.h"
#include <functional>
#include <string>
#include <vector>

namespace t2m
{
// Фонема с ударением: stress −1 у согласных, 0/1/2 у гласных (как в CMUdict)
struct PS
{
    Ph ph;
    int8_t stress;
};
using Pron = std::vector<PS>;

// Произношение слов: словарь CMUdict (≈135 тыс. слов), для незнакомых — отделение окончаний
// и правила «буква → звук» в духе NRL (Elovitz et al., 1976).
class Lexicon
{
public:
    // Источник текста словаря (формат cmudict.dict). Плагин отдаёт распакованный BinaryData.
    static void setSource (std::function<std::string()> loader);
    static const Lexicon& get();

    // слово (буквы и апостроф, любой регистр) → произношение; false — пусто
    bool pronounce (const std::string& word, Pron& out) const;
    bool lookup (const std::string& lowerWord, Pron& out) const;   // только словарь
    static Pron letterName (char c);                              // «A» → EY1
    static std::string numberToWords (const std::string& digits);
    static bool parsePhonemes (const std::string& arpabet, Pron& out);   // «HH AH0 L OW1»
    static Pron rules (const std::string& lowerWord);                    // буква → звук
    size_t size() const { return index.size(); }

private:
    Lexicon();
    std::string text;
    std::vector<uint32_t> index;   // начала строк, отсортированы по слову
};
} // namespace t2m
