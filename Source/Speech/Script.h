#pragma once
#include "Lexicon.h"
#include <string>
#include <vector>

namespace t2m
{
// Управляющий кадр формантного синтеза (шаг 5 мс)
struct CtlFrame
{
    float f1 = 500, f2 = 1500, f3 = 2500;   // форманты, Гц
    float b1 = 80, b2 = 90, b3 = 150;       // полосы, Гц
    float av = 0;                           // голос
    float ah = 0;                           // придыхание
    float af = 0;                           // шум
    float a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, ab = 0;   // полосы шума, линейно
    float fnz = 270;                        // нуль носового резонатора
    float pitch = 0;                        // интонация, полутона относительно ноты

    static CtlFrame lerp (const CtlFrame& a, const CtlFrame& b, float t);
};

struct Segment
{
    Ph ph;
    int8_t stress;
    float start, dur;   // с
    int syl;            // индекс слога, −1 — пауза
};

struct Syllable
{
    float start, hold, end;    // с; hold — середина гласной (там звук держится, пока нажата клавиша)
    int word;
    int textStart, textEnd;    // символы исходного текста — для подсветки
    int8_t stress;
};

struct Word
{
    float start, end;
    int textStart, textEnd;
    int firstSyl, numSyl;
};

struct Script
{
    static constexpr float kHop = 0.005f;
    std::string text;
    std::vector<Segment> segs;
    std::vector<Syllable> syls;
    std::vector<Word> words;
    std::vector<CtlFrame> frames;
    std::vector<std::string> wordPhonemes;   // «HH AH0 L OW1» по словам
    float duration = 0;

    bool empty() const { return syls.empty(); }
    CtlFrame at (float t) const;
};

// Текст → сценарий. Поддерживает: числа, знаки препинания (паузы, «?» — вопросительная интонация),
// фонемы в фигурных скобках {HH AH0 L OW1}, одиночные заглавные буквы — названия букв.
Script buildScript (const std::string& text);
} // namespace t2m
