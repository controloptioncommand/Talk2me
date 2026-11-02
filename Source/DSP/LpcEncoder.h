#pragma once
#include "Formant.h"
#include "LpcChip.h"
#include <vector>

namespace t2m
{
// Кодирование фразы в кадры чипа, как это делали в TI для ПЗУ Speak & Spell: речь (здесь — формантный рендер
// на 8 кГц) → предыскажение → LPC-анализ 10-го порядка по кадрам 25 мс → квантование по таблицам чипа.
struct LpcEncoder
{
    static constexpr float kChipRate = 8000.0f;
    static constexpr int kFrameSamples = 200;

    static std::vector<ChipFrame> encode (const Script& sc, const VoiceShape& shape, int chip);

    // Огибающая спектра кадра (для экрана): bins значений в дБ от 0 до 4 кГц
    static void envelope (const ChipFrame& f, int chip, float* outDb, int bins);
};
} // namespace t2m
