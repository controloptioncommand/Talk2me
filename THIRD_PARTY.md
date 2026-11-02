# Чужой код и данные

## MAME — таблицы и логика чипов TI TMS51xx / TMS52xx
`Source/DSP/ChipTables.h` (таблицы энергии, высоты, коэффициентов K1–K10 и chirp-импульса) и логика синтеза в `Source/DSP/LpcChip.cpp` взяты из MAME: `src/devices/sound/tms5110r.hxx`, `tms5110.cpp`.

```
license:BSD-3-Clause
copyright-holders: Frank Palazzolo, Jarek Burczynski, Aaron Giles, Jonathan Gevaryahu, Couriersud

Redistribution and use in source and binary forms, with or without modification, are permitted provided
that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
   promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

Плагин не содержит ПЗУ со словами Speak & Spell и записей оригинальных устройств: фраза каждый раз кодируется заново.

## CMUdict — словарь произношений
`Resources/Lexicon/cmudict.dict.gz` — The CMU Pronouncing Dictionary, Carnegie Mellon University. Лицензия BSD, полный текст — `Resources/Lexicon/LICENSE-cmudict.txt`.

## Шрифты Medeo
`Resources/Fonts/` — Medeo Sans, Mono, Pixel. SIL Open Font License, `Resources/Fonts/OFL.txt`.

## Методы
- Формантный синтез — по D. H. Klatt, «Software for a cascade/parallel formant synthesizer» (1980), источник KLGLOTT88.
- Длительности фонем — по правилам J. Allen, M. S. Hunnicutt, D. Klatt, «From Text to Speech: The MITalk System» (1987).
- Правила «буква → звук» — по мотивам H. S. Elovitz et al., NRL Report 7948 (1976).

## JUCE
[JUCE 8](https://juce.com) — фреймворк плагина, скачивается при сборке.
