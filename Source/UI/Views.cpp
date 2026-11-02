#include "Views.h"
#include "../PluginProcessor.h"

namespace t2m
{
using LnF = T2MLookAndFeel;

// ===========================================================================
// Мини-спектрограмма
MiniSpectrogram::MiniSpectrogram()
{
    setTitle ("Mini spectrogram");
    setDescription ("A small decorative spectrogram of the output.");
    setInterceptsMouseClicks (false, false);
}

void MiniSpectrogram::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i) ring[(size_t) ((written + i) & 2047)] = x[i];
    written += n;
}

void MiniSpectrogram::tick()
{
    std::array<int8_t, kRows> col;
    col.fill (-1);
    if (written - lastColumnAt >= 256)
    {
        lastColumnAt = written;
        constexpr int N = 512;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (N - 1));
            work[(size_t) i] = ring[(size_t) ((written - N + i) & 2047)] * w;
        }
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());
        // полосы по логарифму: 120 Гц … 7 кГц, снизу вверх
        for (int r = 0; r < kRows; ++r)
        {
            const double f0 = 120.0 * std::pow (7000.0 / 120.0, (double) r / kRows);
            const double f1 = 120.0 * std::pow (7000.0 / 120.0, (double) (r + 1) / kRows);
            const int b0 = juce::jlimit (1, N / 2 - 1, (int) (f0 / sampleRate * N));
            const int b1 = juce::jlimit (b0 + 1, N / 2, (int) (f1 / sampleRate * N) + 1);
            float m = 0;
            for (int b = b0; b < b1; ++b) m = juce::jmax (m, work[(size_t) b]);
            const float db = juce::Decibels::gainToDecibels (m / (float) (N / 4), -120.0f) + 12.0f;
            col[(size_t) r] = db < -34.0f ? (int8_t) -1 : (int8_t) rampBucket (db);
        }
    }
    cols.push_back (col);
    const int maxCols = juce::jmax (1, (getWidth() - 8) / kCell);
    while ((int) cols.size() > maxCols) cols.pop_front();
    repaint();
}

void MiniSpectrogram::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    LnF::drawPlate (g, r, 8.0f, Colours::well);
    const int h = kRows * kCell;
    const int y0 = (getHeight() - h) / 2;
    const int maxCols = juce::jmax (1, (getWidth() - 8) / kCell);
    int x = 4 + (maxCols - (int) cols.size()) * kCell;
    juce::RectangleList<int> buckets[10];
    for (auto& c : cols)
    {
        for (int row = 0; row < kRows; ++row)
            if (c[(size_t) row] >= 0)
                buckets[c[(size_t) row]].addWithoutMerging ({ x, y0 + (kRows - 1 - row) * kCell, kCell, kCell });
        x += kCell;
    }
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }
}

// ===========================================================================
// Фраза
PhraseView::PhraseView (Talk2MeProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Phrase");
    setDescription ("The phrase the voice speaks. Press Return or click to edit.");
    editor.setMultiLine (true, true);
    editor.setReturnKeyStartsNewLine (false);
    editor.setFont (LnF::mono (LnF::Type::title1));
    editor.setIndents (10, 8);
    editor.setTitle ("Edit phrase");
    editor.onReturnKey = [this] { stopEditing (true); };
    editor.onEscapeKey = [this] { stopEditing (false); };
    editor.onFocusLost = [this] { if (editor.isVisible()) stopEditing (true); };
    editor.onTextChange = [this] { proc.setText (editor.getText()); };   // звучит сразу, пока печатаете
    addChildComponent (editor);
}

void PhraseView::setBundle (std::shared_ptr<const Bundle> b)
{
    bundle = std::move (b);
    hiSyl.clear();
    hiWords.clear();
    repaint();
}

void PhraseView::setActive (const std::vector<float>& times)
{
    std::vector<std::pair<int, int>> syl;
    std::vector<int> words;
    if (bundle != nullptr)
        for (float t : times)
            for (auto& s : bundle->script.syls)
                if (t >= s.start && t < s.end + 0.02f)
                {
                    syl.push_back ({ s.textStart, s.textEnd });
                    words.push_back (s.word);
                    break;
                }
    if (syl != hiSyl || words != hiWords)
    {
        hiSyl = std::move (syl);
        hiWords = std::move (words);
        repaint();
    }
}

void PhraseView::startEditing()
{
    if (editor.isVisible()) return;
    before = proc.getText();
    editor.setText (before, false);
    editor.applyColourToAllText (Colours::ink, true);
    editor.setColour (juce::TextEditor::backgroundColourId, Colours::well);
    editor.setColour (juce::TextEditor::outlineColourId, Colours::outline);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, Colours::signal);
    editor.setColour (juce::TextEditor::highlightColourId, Colours::signal.withAlpha (0.3f));
    editor.setColour (juce::CaretComponent::caretColourId, Colours::ink);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.moveCaretToEnd();
    repaint();
}

void PhraseView::stopEditing (bool commit)
{
    if (! editor.isVisible()) return;
    proc.setText (commit ? editor.getText() : before);
    editor.setVisible (false);
    repaint();
}

void PhraseView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked()) startEditing();
}

bool PhraseView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) { startEditing(); return true; }
    return false;
}

void PhraseView::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    headArea = r.removeFromTop (20);
    phonArea = r.removeFromBottom (18);
    r.removeFromBottom (6);
    textArea = r;
    editor.setBounds (textArea.expanded (8, 4));
}

void PhraseView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::surface);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    // заголовок
    {
        auto h = headArea;
        g.setFont (LnF::font (LnF::Type::headline, true));
        g.setColour (Colours::ink);
        g.drawText ("Phrase", h.removeFromLeft (80), juce::Justification::centredLeft);
        g.setFont (LnF::font (LnF::Type::footnote));
        g.setColour (Colours::ink2);
        g.drawText (editor.isVisible() ? juce::String ("Return to apply, Esc to cancel")
                                       : juce::String ("Click to edit") + midDot() + "{HH AH0 L OW1} for phonemes" + midDot() + "S P E L L for letters",
                    h, juce::Justification::centredRight);
    }

    if (! editor.isVisible())
    {
        const auto text = proc.getText();
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::topLeft);
        as.setLineSpacing (4.0f);
        const auto font = LnF::mono (LnF::Type::title1);
        if (text.trim().isEmpty())
            as.append ("Type a phrase", font, Colours::ink2);
        else
        {
            // посимвольно: подсвеченные слоги — сигнальным
            const int n = text.length();
            int i = 0;
            while (i < n)
            {
                bool hi = false;
                for (auto& s : hiSyl) if (i >= s.first && i < s.second) hi = true;
                int j = i + 1;
                while (j < n)
                {
                    bool hj = false;
                    for (auto& s : hiSyl) if (j >= s.first && j < s.second) hj = true;
                    if (hj != hi) break;
                    ++j;
                }
                as.append (text.substring (i, j), font, hi ? Colours::signal : Colours::ink);
                i = j;
            }
        }
        as.draw (g, textArea.toFloat());
    }

    // фонемы по словам; звучащее слово — основным цветом
    if (bundle != nullptr)
    {
        juce::AttributedString ps;
        ps.setJustification (juce::Justification::centredLeft);
        const auto f = LnF::mono (LnF::Type::footnote);
        for (size_t w = 0; w < bundle->script.wordPhonemes.size(); ++w)
        {
            if (w > 0) ps.append (midDot(), f, Colours::ink2);
            const bool active = std::find (hiWords.begin(), hiWords.end(), (int) w) != hiWords.end();
            ps.append (juce::String (bundle->script.wordPhonemes[w]), f, active ? Colours::ink : Colours::ink2);
        }
        ps.setWordWrap (juce::AttributedString::none);
        g.saveState();
        g.reduceClipRegion (phonArea);
        ps.draw (g, phonArea.toFloat());
        g.restoreState();
    }
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, 12.0f);
}

// ===========================================================================
// Главный вид
static const char* kTabNames[] = { "Script", "Scope" };

ScriptView::ScriptView()
{
    setWantsKeyboardFocus (true);
    setTitle ("Main view");
    setDescription ("Script: the spectral envelope of the phrase over time, with words and playheads. Scope: the output wave.");
}

juce::Colour ScriptView::background() const { return currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface; }

void ScriptView::setMode (Mode m)
{
    mode = m;
    setDescription (juce::String ("Main view, ") + kTabNames[(int) m] + " selected.");
    repaint();
}

void ScriptView::setBundle (std::shared_ptr<const Bundle> b, int eng)
{
    if (b == bundle && eng == engine) return;
    bundle = std::move (b);
    engine = eng;
    image = {};
    repaint();
}

void ScriptView::setPlayheads (const std::vector<float>& t)
{
    if (t == heads) return;
    heads = t;
    if (mode == Script) repaint();
}

void ScriptView::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i) ring[(size_t) ((written + i) & ((1 << 15) - 1))] = x[i];
    written += n;
}

void ScriptView::tick()
{
    if (mode != Scope) { scopeRead = written; return; }
    const int perCol = 96;
    if (written - scopeRead > (1 << 14)) scopeRead = written - (1 << 14);
    while (scopeRead + perCol <= written)
    {
        Col c;
        float pk = 0;
        for (int i = 0; i < perCol; ++i)
        {
            const float v = ring[(size_t) ((scopeRead + i) & ((1 << 15) - 1))];
            c.mn = juce::jmin (c.mn, v);
            c.mx = juce::jmax (c.mx, v);
            pk = juce::jmax (pk, std::abs (v));
        }
        c.db = juce::Decibels::gainToDecibels (pk, -100.0f);
        scopeRead += perCol;
        scopeCols.push_back (c);
    }
    while ((int) scopeCols.size() > juce::jmax (1, getWidth() - 2)) scopeCols.pop_front();
    repaint();
}

void ScriptView::resized()
{
    const float h = 28.0f;
    const float tabW = 2 * 72.0f;
    tabsArea = { (float) getWidth() - 12.0f - tabW, (float) getHeight() - 12.0f - h, tabW, h };
    legendArea = { (float) getWidth() - 12.0f - 236.0f, 12.0f, 236.0f, h };
    image = {};
}

void ScriptView::buildImage (juce::Rectangle<int> area)
{
    imageArea = area;
    image = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
    if (bundle == nullptr || bundle->chipFrames.empty() || bundle->script.duration <= 0) return;
    const auto& frames = bundle->chipFrames;
    const int rows = juce::jmax (8, area.getHeight() / 4);
    std::vector<float> env ((size_t) rows);
    std::vector<std::vector<float>> all;
    float maxDb = -200.0f;
    // кадры закодированы с предыскажением TI (верх поднят) — на экране его снимаем, чтобы были видны форманты речи
    std::vector<float> deemph ((size_t) rows);
    for (int r = 0; r < rows; ++r)
    {
        const float w = juce::MathConstants<float>::pi * ((float) r + 0.5f) / (float) rows;
        deemph[(size_t) r] = -20.0f * std::log10 (std::hypot (1.0f - 0.9373f * std::cos (w), 0.9373f * std::sin (w)));
    }
    for (auto& f : frames)
    {
        LpcEncoder::envelope (f, bundle->chip, env.data(), rows);
        for (int r = 0; r < rows; ++r) env[(size_t) r] += deemph[(size_t) r];
        for (float v : env) maxDb = juce::jmax (maxDb, v);
        all.push_back (env);
    }
    juce::Graphics g (image);
    const float W = (float) area.getWidth(), H = (float) area.getHeight();
    const float frameW = W * 0.025f / juce::jmax (0.05f, bundle->script.duration);
    const float rowH = H / (float) rows;
    juce::RectangleList<float> buckets[10];
    for (size_t i = 0; i < all.size(); ++i)
    {
        const float x0 = (float) i * frameW;
        if (x0 >= W) break;
        for (int r = 0; r < rows; ++r)
        {
            const float rel = all[i][(size_t) r] - maxDb;
            if (rel < -36.0f) continue;
            const int b = rampBucket (rel * 24.0f / 36.0f);
            // клетки на целых пикселях: пиксельная «партитура»
            const float y = H - (float) (r + 1) * rowH;
            buckets[b].addWithoutMerging ({ std::floor (x0), std::floor (y), juce::jmax (1.0f, std::ceil (frameW) - 1.0f), juce::jmax (1.0f, std::ceil (rowH) - 1.0f) });
        }
    }
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }

    // Talk 96: поверх — дорожки формант F1–F3
    if (engine == 0)
    {
        const auto& sc = bundle->script;
        g.setColour (Colours::ink.withAlpha (0.55f));
        for (size_t i = 0; i < sc.frames.size(); i += 2)
        {
            const auto& fr = sc.frames[i];
            if (fr.av < 0.1f) continue;
            const float x = W * (float) i * Script::kHop / sc.duration;
            for (float f : { fr.f1, fr.f2, fr.f3 })
            {
                if (f >= 4000.0f) continue;
                const float y = H - H * f / 4000.0f;
                g.fillRect (std::floor (x), std::floor (y), 2.0f, 2.0f);
            }
        }
    }
}

void ScriptView::paintScript (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto heat = area.withTrimmedBottom (22.0f).toNearestInt();
    if (! image.isValid() || imageArea != heat) buildImage (heat);
    g.drawImageAt (image, heat.getX(), heat.getY());

    // подписи частот
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    for (float f : { 1000.0f, 2000.0f, 3000.0f })
    {
        const float y = (float) heat.getBottom() - (float) heat.getHeight() * f / 4000.0f;
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (Colours::ink2.withAlpha (0.35f));
        g.drawDashedLine ({ (float) heat.getX(), y, (float) heat.getRight(), y }, dashes, 2, 1.0f);
        g.setColour (Colours::ink2);
        g.drawText (juce::String ((int) (f / 1000.0f)) + "k", juce::Rectangle<float> ((float) heat.getX() + 4.0f, y - 14.0f, 30.0f, 13.0f), juce::Justification::centredLeft);
    }

    if (bundle == nullptr || bundle->script.empty())
    {
        g.setFont (LnF::font (LnF::Type::body));
        g.setColour (Colours::ink2);
        g.drawText ("Type a phrase above", heat, juce::Justification::centred);
        return;
    }
    const auto& sc = bundle->script;
    const float W = (float) heat.getWidth();
    auto xOf = [&] (float t) { return (float) heat.getX() + W * t / sc.duration; };

    // слова под «партитурой»
    std::vector<int> activeWords;
    for (float t : heads)
        for (size_t w = 0; w < sc.words.size(); ++w)
            if (t >= sc.words[w].start && t < sc.words[w].end + 0.02f) activeWords.push_back ((int) w);
    const float wy = (float) heat.getBottom() + 4.0f;
    float lastRight = -1e9f;
    for (size_t w = 0; w < sc.words.size(); ++w)
    {
        const auto& wd = sc.words[w];
        const float x = xOf (wd.start);
        g.setColour (Colours::ink2.withAlpha (0.5f));
        g.fillRect (x, (float) heat.getY(), 1.0f, (float) heat.getHeight() + 4.0f);
        const bool active = std::find (activeWords.begin(), activeWords.end(), (int) w) != activeWords.end();
        const auto txt = juce::String (sc.text.substr ((size_t) wd.textStart, (size_t) (wd.textEnd - wd.textStart)));
        const auto f = LnF::font (LnF::Type::subheadline, active);
        const float tw = (float) juce::GlyphArrangement::getStringWidth (f, txt);
        if (x < lastRight + 4.0f && ! active) continue;   // не налезать на соседнее слово
        g.setFont (f);
        g.setColour (active ? Colours::ink : Colours::ink2);
        g.drawText (txt, juce::Rectangle<float> (x + 3.0f, wy, tw + 4.0f, 16.0f), juce::Justification::centredLeft);
        lastRight = x + 3.0f + tw;
    }

    // курсоры звучащих нот
    g.setColour (Colours::signal);
    for (float t : heads)
    {
        const float x = xOf (juce::jlimit (0.0f, sc.duration, t));
        g.fillRect (x - 1.0f, (float) heat.getY(), 2.0f, (float) heat.getHeight());
    }
}

void ScriptView::paintScope (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float mid = area.getCentreY(), hh = area.getHeight() * 0.5f;
    const float dashes[] = { 3.0f, 4.0f };
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (float db : { -6.0f, -12.0f })
    {
        const float a = juce::Decibels::decibelsToGain (db) * hh;
        for (float y : { mid - a, mid + a })
        {
            g.setColour (Colours::ink2.withAlpha (0.45f));
            g.drawDashedLine ({ area.getX() + 12.0f, y, area.getRight() - 48.0f, y }, dashes, 2, 1.0f);
        }
        g.setColour (Colours::ink2);
        g.drawText (minus() + juce::String ((int) -db), juce::Rectangle<float> (area.getRight() - 44.0f, mid - a - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
    }
    juce::RectangleList<float> buckets[10];
    float x = area.getRight() - (float) scopeCols.size();
    for (auto& c : scopeCols)
    {
        if (x >= area.getX() && c.mx >= c.mn)
        {
            const float y0 = mid - juce::jlimit (-1.0f, 1.0f, c.mx) * hh;
            const float y1 = mid - juce::jlimit (-1.0f, 1.0f, c.mn) * hh;
            buckets[rampBucket (c.db)].addWithoutMerging ({ x, y0, 1.0f, juce::jmax (1.0f, y1 - y0) });
        }
        x += 1.0f;
    }
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }
}

void ScriptView::paintOverlay (juce::Graphics& g)
{
    const auto plateFill = currentTheme() == ThemeId::Rubber ? Colours::surface : Colours::well;
    // сведения о фразе — слева сверху
    if (bundle != nullptr && ! bundle->script.empty())
    {
        const auto& sc = bundle->script;
        juce::String info = juce::String (sc.duration, 1) + " s" + midDot() + juce::String ((int) sc.words.size()) + " words" + midDot()
                          + juce::String ((int) sc.syls.size()) + " syllables";
        if (engine == 1) info += midDot() + juce::String ((int) bundle->chipFrames.size()) + " LPC frames";
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.setColour (Colours::ink2);
        g.drawText (info, juce::Rectangle<float> (16.0f, 12.0f, legendArea.getX() - 24.0f, 28.0f), juce::Justification::centredLeft);
    }

    LnF::drawPlate (g, legendArea, 8.0f, plateFill);
    {
        auto r = legendArea.reduced (12.0f, 0.0f);
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.setColour (Colours::ink);
        g.drawText (minus() + "24 dB", r.removeFromLeft (46.0f), juce::Justification::centredLeft);
        g.drawText ("0 dB", r.removeFromRight (30.0f), juce::Justification::centredRight);
        auto bar = r.reduced (6.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 12.0f, 6.0f);
        const float seg = bar.getWidth() / 10.0f;
        for (int b = 0; b < 10; ++b)
        {
            g.setColour (rampBucketColour (Colours::carrierRamp, b));
            g.fillRect (juce::Rectangle<float> (bar.getX() + seg * (float) b + 0.5f, bar.getY(), seg - 1.0f, bar.getHeight()));
        }
    }

    LnF::drawPlate (g, tabsArea, tabsArea.getHeight() * 0.5f, plateFill);
    const float w = (tabsArea.getWidth() - 4.0f) / 2.0f;
    for (int i = 0; i < 2; ++i)
    {
        auto cell = juce::Rectangle<float> (tabsArea.getX() + 2.0f + w * (float) i, tabsArea.getY() + 2.0f, w, tabsArea.getHeight() - 4.0f);
        const bool sel = (int) mode == i;
        if (sel)
        {
            g.setColour (currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface);
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (cell.reduced (0.5f), cell.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (sel ? Colours::ink : Colours::ink2);
        g.setFont (LnF::font (LnF::Type::subheadline, sel));
        g.drawText (kTabNames[i], cell, juce::Justification::centred);
    }
    if (hasKeyboardFocus (false))
        LnF::drawFocusRing (g, tabsArea.expanded (2.0f), tabsArea.getHeight() * 0.5f + 2.0f);
}

void ScriptView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float radius = 12.0f;
    g.setColour (background());
    g.fillRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
        g.reduceClipRegion (clip);
        if (mode == Script) paintScript (g, r.reduced (16.0f, 0.0f).withTrimmedTop (48.0f).withTrimmedBottom (48.0f));
        else paintScope (g, r.reduced (1.0f, 46.0f));
        paintOverlay (g);
    }
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void ScriptView::mouseDown (const juce::MouseEvent& e)
{
    if (tabsArea.contains (e.position))
        setMode ((Mode) juce::jlimit (0, 1, (int) ((e.position.x - tabsArea.getX()) / tabsArea.getWidth() * 2.0f)));
}

bool ScriptView::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)  { setMode (Script); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { setMode (Scope); return true; }
    return false;
}
} // namespace t2m
