# Правила проекта Talk2Me

Talk2Me — вайбкод VST3/AU-синтезатор речи для macOS, C++ / JUCE 8. Брат Mist и Eido: те же темы, шрифты и страница About.
Репозиторий публичный: https://github.com/controloptioncommand/Talk2me

Перед любой работой с интерфейсом прочитай **[docs/DESIGN.md](docs/DESIGN.md)** — это обязательная дизайн-система.

## Общение
- Отвечать по-русски, коротко и по делу. Не начинать ответ с похвалы.
- Менять мнение только на аргументах, а не потому что пользователь не согласен.
- Если просьба неоднозначна (особенно по дизайну) — уточнить, а не придумывать от себя.

## Приватность (репозиторий открытый)
- Никаких личных данных в файлах, коммитах и патчноутах: почт, паролей, токенов, API-ключей, локальных путей, ссылок на переписку с ИИ.
- Коммиты: автор `nnn <controloptioncommand@users.noreply.github.com>`, коммиттер `Claude <noreply@anthropic.com>`. Строку `Claude-Session: …` в сообщения коммитов **не добавлять**.
  ```bash
  GIT_COMMITTER_NAME=Claude GIT_COMMITTER_EMAIL=noreply@anthropic.com \
    git -c user.name="nnn" -c user.email="controloptioncommand@users.noreply.github.com" commit -m "…"
  ```
- Перед каждым push: `bash scripts/privacy-check.sh` (CI тоже его запускает и остановит сборку при утечке).
- Не делать force push и не переписывать опубликованную историю без явной просьбы.
- Публично оставлены намеренно: ник «nnn» и ссылка на Telegram автора на странице About.

## Пасхалки — не раскрывать
- В README, руководстве (PDF), патчноутах и любой другой публичной документации **не упоминать** эффект «Туман» на логотипе About, сову, игру и скрытые ссылки на странице About (версия → GitHub, «made by nnn» → Telegram). Это пасхалки, их находят сами.
- Скриншоты игры и эффекта «Туман» в документацию не добавлять.

## Руководство (PDF)
- `docs/Talk2Me-manual.pdf` — руководство пользователя (рус.), кладётся в архив релиза. Исходник — `docs/manual/Talk2Me-manual.html` (стиль — как у PDF Mist и Eido).
- **Обновлять руководство** при любом изменении интерфейса, ручек, диапазонов или поведения: поправить текст в HTML и пересобрать `bash docs/manual/build.sh` (снимает свежие скриншоты тестом, `crop.py` нарезает, `print.js` печатает PDF через Chromium). Пересобранный PDF — посмотреть глазами: без пустых страниц и обрезанных картинок.
- Названия ручек и диапазоны сверять с кодом (`Source/Params.cpp`, `Source/UI/`), не писать по памяти.

## Версии и патчноуты
- Текущая версия — в файле `VERSION` (схема: v1, v1.01 … v2, v2.01 …). На странице About она же в `Source/UI/About.h` (`AboutInfo::version`, `AboutInfo::date`).
- **У каждой версии обязателен патчноут** `docs/releases/<версия>.md` — без него CI не соберёт релиз.
- Новая версия: поменять `VERSION` и `AboutInfo` → добавить патчноут → коммит в `main`. CI сам соберёт, проверит (pluginval, auval, тест) и опубликует релиз с архивом `Talk2Me-macOS.zip`.
- Пока `VERSION` не меняется, новые сборки перезаписывают текущий релиз.
- Ссылка на последнюю сборку: https://github.com/controloptioncommand/Talk2me/releases/latest/download/Talk2Me-macOS.zip
- После публикации дать пользователю эту ссылку и команду установки:
  ```bash
  cd ~/Downloads && rm -rf Talk2Me-macOS Talk2Me-macOS.zip && gh release download -R controloptioncommand/Talk2me -p Talk2Me-macOS.zip && unzip -q Talk2Me-macOS.zip -d Talk2Me-macOS && bash Talk2Me-macOS/install-mac.sh
  ```

## Сборка и проверка
```bash
cmake -B build -DT2M_BUILD_TESTS=ON && cmake --build build --target T2MTest
./build/T2MTest_artefacts/Release/T2MTest ./render   # на Linux — через xvfb-run -a
```
Тест: разбор текста (числа, буквы, фонемы, незнакомые слова), рендер нот во всех движках, чипах и режимах (не тишина, не NaN, ≤ 0 dBFS), крайние значения ручек, шаги по слогам, удержание гласной, аккорд, состояние, все пресеты, скорость (8 голосов), демо-WAV, скриншоты в обеих темах, About. Перед коммитом тест должен печатать `ALL OK`. После изменений интерфейса — посмотреть скриншоты.

## Что нельзя ломать
- Таблицы чипов (`Source/DSP/ChipTables.h`) — из MAME, сверены с декапами. Не править на глаз.
- Знак коэффициентов LPC: A(z) = 1 + Σ aᵢ z⁻ⁱ, у гласных K1 близко к −1 (как в решётке чипа).
- Фраза собирается в потоке сообщений (`Talk2MeProcessor::rebuildNow`) и передаётся в аудиопоток готовой; в аудиопотоке нет выделений памяти и разбора текста. Старые фразы держит `keepAlive`, чтобы аудиопоток не освобождал память.
- Смена ручек Voice / Formant / Chip перекодирует кадры, но не обрывает звучащие ноты; обрывает только смена текста (`Bundle::textSerial`).
- Выход защищён мягким лимитером (не выше 0 dBFS).
- Изменение диапазонов параметров ломает старые проекты и пресеты — предупреждать.

## Структура
- `Source/Speech/` — `Phonemes` (цели формант и длительности), `Lexicon` (CMUdict, окончания, правила чтения, числа), `Script` (слоги, длительности, переходы, интонация → кадры 5 мс)
- `Source/DSP/` — `Formant` (Talk 96), `LpcChip` + `ChipTables` (Spell 80), `LpcEncoder` (фраза → кадры чипа), `Engine` (голоса, режимы игры, пересчёт частоты)
- `Source/PluginProcessor`, `Params`, `Presets` — параметры, состояние, пресеты
- `Source/UI/` — `LookAndFeel` и `Widgets` (общие с Mist и Eido), `Views` (фраза, партитура, Scope, мини-спектрограмма), `About`
- `Tests/T2MTest.cpp` — офлайн-тест; `scripts/` — установка и проверка приватности; `docs/` — руководство (`Talk2Me-manual.pdf`, исходник в `docs/manual/`), дизайн, картинки README, патчноуты
- При изменении ручек и поведения — обновлять руководство (`docs/manual/Talk2Me-manual.html` → `bash docs/manual/build.sh`) и README (пасхалки там не упоминать).
