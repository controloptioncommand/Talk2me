#include "Script.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace t2m
{
CtlFrame CtlFrame::lerp (const CtlFrame& a, const CtlFrame& b, float t)
{
    CtlFrame r;
    const float* pa = &a.f1;
    const float* pb = &b.f1;
    float* pr = &r.f1;
    constexpr int n = (int) (sizeof (CtlFrame) / sizeof (float));
    for (int i = 0; i < n; ++i) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
    return r;
}

CtlFrame Script::at (float t) const
{
    if (frames.empty()) return {};
    const float x = std::max (0.0f, t / kHop);
    const size_t i = (size_t) x;
    if (i + 1 >= frames.size()) return frames.back();
    return CtlFrame::lerp (frames[i], frames[i + 1], x - (float) i);
}

namespace
{
// ---------------------------------------------------------------------------
// Разбор текста
struct Item
{
    bool pause = false;
    float pauseDur = 0;
    int phraseEnd = 0;   // 0 — нет, 1 — повествование, 2 — вопрос
    Pron pron;
    int textStart = 0, textEnd = 0;
};

void addWord (std::vector<Item>& items, const std::string& w, int s, int e)
{
    Item it;
    if (! Lexicon::get().pronounce (w, it.pron) || it.pron.empty()) return;
    it.textStart = s;
    it.textEnd = e;
    items.push_back (std::move (it));
}

void addPause (std::vector<Item>& items, float dur, int phraseEnd)
{
    if (items.empty()) return;   // пауза в начале не нужна
    if (items.back().pause)
    {
        items.back().pauseDur = std::max (items.back().pauseDur, dur);
        items.back().phraseEnd = std::max (items.back().phraseEnd, phraseEnd);
        return;
    }
    Item it;
    it.pause = true;
    it.pauseDur = dur;
    it.phraseEnd = phraseEnd;
    items.push_back (it);
}

std::vector<Item> tokenize (const std::string& text)
{
    std::vector<Item> items;
    const int n = (int) text.size();
    int i = 0;
    while (i < n)
    {
        const unsigned char c = (unsigned char) text[(size_t) i];
        if (c == '{')
        {
            const int s = i;
            int e = i + 1;
            while (e < n && text[(size_t) e] != '}') ++e;
            Item it;
            Lexicon::parsePhonemes (text.substr ((size_t) s + 1, (size_t) (e - s - 1)), it.pron);
            it.textStart = s;
            it.textEnd = std::min (n, e + 1);
            if (! it.pron.empty()) items.push_back (std::move (it));
            i = e + 1;
        }
        else if (std::isalpha (c))
        {
            const int s = i;
            while (i < n && (std::isalpha ((unsigned char) text[(size_t) i])
                             || (text[(size_t) i] == '\'' && i + 1 < n && std::isalpha ((unsigned char) text[(size_t) i + 1]))))
                ++i;
            addWord (items, text.substr ((size_t) s, (size_t) (i - s)), s, i);
        }
        else if (std::isdigit (c))
        {
            const int s = i;
            while (i < n && std::isdigit ((unsigned char) text[(size_t) i])) ++i;
            const auto words = Lexicon::numberToWords (text.substr ((size_t) s, (size_t) (i - s)));
            size_t p = 0;
            while (p < words.size())
            {
                size_t q = words.find (' ', p);
                if (q == std::string::npos) q = words.size();
                addWord (items, words.substr (p, q - p), s, i);
                p = q + 1;
            }
        }
        else
        {
            if (c == ',' || c == ';' || c == ':' || c == '\n' || c == '(' || c == ')') addPause (items, 0.16f, 0);
            else if (c == '.' || c == '!') addPause (items, 0.32f, 1);
            else if (c == '?') addPause (items, 0.32f, 2);
            else if (c == '-' && (i == 0 || text[(size_t) i - 1] == ' ')) addPause (items, 0.16f, 0);   // « - » — пауза, «a-b» — нет
            else if (c >= 0x80) { /* не ASCII — пропускаем */ }
            ++i;
        }
    }
    while (! items.empty() && items.back().pause) items.pop_back();
    return items;
}

// ---------------------------------------------------------------------------
// Слоги: ядро — гласная, согласные между ядрами — по принципу максимального приступа
bool legalOnset (const std::vector<Ph>& c)
{
    if (c.empty()) return true;
    if (c.size() == 1) return c[0] != NG;
    auto is = [] (Ph p, std::initializer_list<Ph> s) { for (auto x : s) if (x == p) return true; return false; };
    if (c.size() == 2)
    {
        const Ph a = c[0], b = c[1];
        if (is (b, { R, L }) && is (a, { P, B, F, K, G })) return ! (a == F && b == L && false);
        if (b == R && is (a, { T, D, TH, SH })) return true;
        if (a == S && is (b, { P, T, K, M, N, L, W, F })) return true;
        if (b == W && is (a, { K, G, T, D, S, TH })) return true;
        if (b == Y && is (a, { P, B, K, G, M, F, V, HH })) return true;
        return false;
    }
    if (c.size() == 3 && c[0] == S)
    {
        const Ph b = c[1], d = c[2];
        return (b == P && is (d, { R, L, Y })) || (b == T && d == R) || (b == K && is (d, { R, W, Y }));
    }
    return false;
}

struct SylRange { int first, nucleus, last; };   // индексы фонем в слове

std::vector<SylRange> syllabify (const Pron& p)
{
    std::vector<int> nuclei;
    for (int i = 0; i < (int) p.size(); ++i) if (isVowel (p[(size_t) i].ph)) nuclei.push_back (i);
    std::vector<SylRange> out;
    if (nuclei.empty())
    {
        out.push_back ({ 0, 0, (int) p.size() - 1 });   // слово без гласных — один «слог»
        return out;
    }
    int start = 0;
    for (size_t k = 0; k < nuclei.size(); ++k)
    {
        const int nuc = nuclei[k];
        int last;
        if (k + 1 < nuclei.size())
        {
            const int next = nuclei[k + 1];
            // согласные между ядрами: самый длинный допустимый приступ уходит в следующий слог
            int split = nuc + 1;
            for (int s = nuc + 1; s <= next; ++s)
            {
                std::vector<Ph> onset;
                for (int j = s; j < next; ++j) onset.push_back (p[(size_t) j].ph);
                if (legalOnset (onset)) { split = s; break; }
            }
            last = split - 1;
        }
        else
            last = (int) p.size() - 1;
        out.push_back ({ start, nuc, last });
        start = last + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
int rank (Kind k)
{
    switch (k)
    {
        case Kind::Stop: case Kind::Affricate: return 4;
        case Kind::Fricative: case Kind::Nasal: return 3;
        case Kind::Liquid: case Kind::Glide: return 2;
        case Kind::Vowel: case Kind::Diphthong: return 1;
        default: return 0;
    }
}

float blendK (Kind k)
{
    switch (k)
    {
        case Kind::Stop: return 0.5f;
        case Kind::Affricate: case Kind::Fricative: case Kind::Liquid: return 0.4f;
        case Kind::Nasal: case Kind::Glide: return 0.3f;
        default: return 0.5f;
    }
}

float transTime (Kind self, Kind other, float dur)
{
    float t = 0;
    if (other == Kind::Pause || other == Kind::Aspirate || self == Kind::Aspirate || self == Kind::Pause) return 0;
    switch (self)
    {
        case Kind::Vowel: case Kind::Diphthong:
            switch (other)
            {
                case Kind::Stop: case Kind::Affricate: t = 0.045f; break;
                case Kind::Fricative: case Kind::Nasal: t = 0.040f; break;
                case Kind::Liquid: t = 0.055f; break;
                case Kind::Glide: t = 0.065f; break;
                default: t = 0.050f; break;
            }
            break;
        case Kind::Stop: t = dur * 0.5f; break;
        case Kind::Liquid: t = 0.03f; break;
        case Kind::Glide: t = 0.04f; break;
        default: t = 0.02f; break;
    }
    return std::min (t, dur * 0.5f);
}

struct Targets { float a[6], b[6]; };   // F1 F2 F3 B1 B2 B3: начало и конец

struct PSeg
{
    Ph ph;
    int8_t stress;
    float dur;
    int syl;
    bool wordInitial, wordFinal, coda, phraseFinalSyl, afterS;
    Targets tg;
};

bool voicelessStop (Ph p) { return p == P || p == T || p == K; }
} // namespace

// ===========================================================================
Script buildScript (const std::string& text)
{
    Script sc;
    sc.text = text;
    const auto items = tokenize (text);

    // ---- слоги, слова и сегменты
    std::vector<PSeg> segs;
    struct PhraseInfo { int firstSyl, lastSyl, type; };
    std::vector<PhraseInfo> phrases;
    int phraseFirstSyl = 0;

    for (size_t ii = 0; ii < items.size(); ++ii)
    {
        const auto& it = items[ii];
        if (it.pause)
        {
            PSeg s {};
            s.ph = PAU;
            s.stress = -1;
            s.dur = it.pauseDur;
            s.syl = -1;
            segs.push_back (s);
            if (it.phraseEnd > 0 && (int) sc.syls.size() > phraseFirstSyl)
            {
                phrases.push_back ({ phraseFirstSyl, (int) sc.syls.size() - 1, it.phraseEnd });
                phraseFirstSyl = (int) sc.syls.size();
            }
            continue;
        }
        const bool beforePause = ii + 1 >= items.size() || items[ii + 1].pause;
        const auto ranges = syllabify (it.pron);
        Word w {};
        w.textStart = it.textStart;
        w.textEnd = it.textEnd;
        w.firstSyl = (int) sc.syls.size();
        w.numSyl = (int) ranges.size();
        const int wordIdx = (int) sc.words.size();

        std::string phStr;
        for (auto& ps : it.pron)
        {
            if (! phStr.empty()) phStr += ' ';
            const Ph shown = ps.ph == AX ? AH : (ps.ph == AXR ? ER : ps.ph);
            phStr += phon (shown).name;
            if (ps.stress >= 0) phStr += (char) ('0' + ps.stress);
        }
        sc.wordPhonemes.push_back (phStr);

        // текст слова делится между слогами пропорционально числу фонем
        const int tlen = it.textEnd - it.textStart;
        const int nph = (int) it.pron.size();
        for (size_t r = 0; r < ranges.size(); ++r)
        {
            const auto& rg = ranges[r];
            Syllable sy {};
            sy.word = wordIdx;
            sy.stress = it.pron[(size_t) rg.nucleus].stress;
            if (sy.stress < 0) sy.stress = 0;
            sy.textStart = it.textStart + (int) std::lround ((double) tlen * rg.first / std::max (1, nph));
            sy.textEnd = r + 1 == ranges.size() ? it.textEnd : it.textStart + (int) std::lround ((double) tlen * (rg.last + 1) / std::max (1, nph));
            const int sylIdx = (int) sc.syls.size();
            sc.syls.push_back (sy);
            for (int k = rg.first; k <= rg.last; ++k)
            {
                PSeg s {};
                s.ph = it.pron[(size_t) k].ph;
                s.stress = it.pron[(size_t) k].stress;
                s.syl = sylIdx;
                s.wordInitial = k == 0;
                s.wordFinal = k == nph - 1;
                s.coda = k > rg.nucleus;
                s.phraseFinalSyl = beforePause && r + 1 == ranges.size();
                s.afterS = k > 0 && it.pron[(size_t) k - 1].ph == S;
                segs.push_back (s);
            }
        }
        sc.words.push_back (w);
    }
    if ((int) sc.syls.size() > phraseFirstSyl) phrases.push_back ({ phraseFirstSyl, (int) sc.syls.size() - 1, 1 });
    if (segs.empty()) return sc;

    // ---- длительности (упрощённые правила Клатта)
    for (size_t i = 0; i < segs.size(); ++i)
    {
        auto& s = segs[i];
        if (s.ph == PAU) continue;
        const auto& d = phon (s.ph);
        float pct = 1.0f, mn = d.minDur;
        const bool prevC = i > 0 && isConsonant (segs[i - 1].ph) && ! segs[i].wordInitial;
        const bool nextC = i + 1 < segs.size() && isConsonant (segs[i + 1].ph) && ! segs[i].wordFinal;
        if (isVowel (s.ph))
        {
            if (s.stress == 0) { pct *= 0.6f; mn *= 0.6f; }
            if (s.phraseFinalSyl) pct *= 1.4f;
            else pct *= 0.85f;
            if (i + 1 < segs.size() && segs[i + 1].syl == s.syl && isConsonant (segs[i + 1].ph) && ! phon (segs[i + 1].ph).voiced)
                pct *= 0.8f;
            const auto& w = sc.words[(size_t) sc.syls[(size_t) s.syl].word];
            if (w.numSyl > 1) pct *= 0.85f;
        }
        else
        {
            if (prevC || nextC) pct *= 0.6f;
            if (s.phraseFinalSyl && s.coda) pct *= 1.3f;
        }
        s.dur = (mn + (d.dur - mn) * pct) * 0.001f;
        // придыхание после глухих смычных съедает начало гласной — гласную чуть удлиняем
        if (isVowel (s.ph) && i > 0 && voicelessStop (segs[i - 1].ph) && ! segs[i - 1].afterS) s.dur += 0.025f;
    }

    // ---- цели формант
    auto baseTargets = [] (Ph p) -> Targets
    {
        const auto& d = phon (p);
        Targets t {};
        const float a[6] = { d.f1, d.f2, d.f3, d.b1, d.b2, d.b3 };
        for (int k = 0; k < 6; ++k) t.a[k] = t.b[k] = a[k];
        if (d.kind == Kind::Diphthong) { t.b[0] = d.f1b; t.b[1] = d.f2b; t.b[2] = d.f3b; }
        return t;
    };
    for (size_t i = 0; i < segs.size(); ++i)
    {
        auto& s = segs[i];
        if (s.ph == PAU || s.ph == HH) continue;
        s.tg = baseTargets (s.ph);
        // тёмное L в конце слога
        if (s.ph == L && s.coda) { s.tg.a[0] = s.tg.b[0] = 450; s.tg.a[1] = s.tg.b[1] = 850; }
        // безударная IH — к центру
        if (s.ph == IH && s.stress == 0) { s.tg.a[1] = s.tg.b[1] = 1700; s.tg.a[0] = s.tg.b[0] = 430; }
        // UW после Y и переднеязычных — передняя (you, two, new)
        if (s.ph == UW && i > 0)
        {
            const Ph pp = segs[i - 1].ph;
            if (pp == Y || pp == T || pp == D || pp == S || pp == Z || pp == N || pp == L || pp == CH || pp == JH)
                s.tg.a[1] = s.tg.b[1] = 1400;
        }
        // K/G перед задними гласными — задний локус
        if (s.ph == K || s.ph == G || s.ph == NG)
        {
            const PSeg* v = nullptr;
            if (i + 1 < segs.size() && isVowel (segs[i + 1].ph)) v = &segs[i + 1];
            else if (i > 0 && isVowel (segs[i - 1].ph)) v = &segs[i - 1];
            if (v != nullptr && phon (v->ph).f2 < 1300) { s.tg.a[1] = s.tg.b[1] = 1450; s.tg.a[2] = s.tg.b[2] = 2450; }
        }
    }
    // HH берёт форманты следующего звука, паузы — соседнего
    for (int i = (int) segs.size() - 1; i >= 0; --i)
        if (segs[(size_t) i].ph == HH)
        {
            Targets t = (size_t) i + 1 < segs.size() && segs[(size_t) i + 1].ph != PAU ? segs[(size_t) i + 1].tg : baseTargets (AX);
            for (int k = 0; k < 6; ++k) t.b[k] = t.a[k];
            t.a[3] = t.b[3] = 300;
            segs[(size_t) i].tg = t;
        }
    for (size_t i = 0; i < segs.size(); ++i)
        if (segs[i].ph == PAU)
        {
            Targets t {};
            if (i > 0) { for (int k = 0; k < 6; ++k) t.a[k] = t.b[k] = segs[i - 1].tg.b[k]; }
            else if (i + 1 < segs.size()) { for (int k = 0; k < 6; ++k) t.a[k] = t.b[k] = segs[i + 1].tg.a[k]; }
            else t = baseTargets (AX);
            segs[i].tg = t;
        }

    // ---- время
    std::vector<float> start (segs.size());
    float t = 0.02f;   // 20 мс тишины в начале — мягкое начало
    for (size_t i = 0; i < segs.size(); ++i) { start[i] = t; t += segs[i].dur; }
    sc.duration = t + 0.03f;

    for (size_t i = 0; i < segs.size(); ++i)
        sc.segs.push_back ({ segs[i].ph, segs[i].stress, start[i], segs[i].dur, segs[i].syl });

    // слоги и слова: границы и точки удержания
    for (auto& sy : sc.syls) { sy.start = 1e9f; sy.end = 0; sy.hold = -1; }
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].syl < 0) continue;
        auto& sy = sc.syls[(size_t) segs[i].syl];
        sy.start = std::min (sy.start, start[i]);
        sy.end = std::max (sy.end, start[i] + segs[i].dur);
        if (isVowel (segs[i].ph))
            sy.hold = start[i] + segs[i].dur * (phon (segs[i].ph).kind == Kind::Diphthong ? 0.3f : 0.5f);
    }
    for (auto& sy : sc.syls) if (sy.hold < 0) sy.hold = (sy.start + sy.end) * 0.5f;
    for (auto& w : sc.words)
    {
        w.start = sc.syls[(size_t) w.firstSyl].start;
        w.end = sc.syls[(size_t) (w.firstSyl + w.numSyl - 1)].end;
    }

    // ---- форманты: опорные точки и линейная интерполяция
    struct Knot { float t, v; };
    std::vector<Knot> knots[6];
    auto boundary = [&] (size_t a, size_t b, int k)
    {
        const auto& A = segs[a];
        const auto& B = segs[b];
        const float va = A.tg.b[k], vb = B.tg.a[k];
        const Kind ka = phon (A.ph).kind, kb = phon (B.ph).kind;
        if (ka == Kind::Pause) return vb;
        if (kb == Kind::Pause) return va;
        if (ka == Kind::Aspirate) return va;
        if (kb == Kind::Aspirate) return vb;
        if (k >= 3) return (va + vb) * 0.5f;   // полосы — середина
        const int ra = rank (ka), rb = rank (kb);
        if (ra > rb) return va + blendK (ka) * (vb - va);
        if (rb > ra) return vb + blendK (kb) * (va - vb);
        return (va + vb) * 0.5f;
    };
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const auto& s = segs[i];
        const Kind kind = phon (s.ph).kind;
        const float s0 = start[i], e0 = s0 + s.dur;
        const float tin = i > 0 ? transTime (kind, phon (segs[i - 1].ph).kind, s.dur) : 0.0f;
        const float tout = i + 1 < segs.size() ? transTime (kind, phon (segs[i + 1].ph).kind, s.dur) : 0.0f;
        for (int k = 0; k < 6; ++k)
        {
            auto& kn = knots[k];
            const float vL = i > 0 ? boundary (i - 1, i, k) : s.tg.a[k];
            const float vR = i + 1 < segs.size() ? boundary (i, i + 1, k) : s.tg.b[k];
            if (i == 0) kn.push_back ({ s0, vL });
            if (kind == Kind::Diphthong)
            {
                const float a1 = s0 + tin, a2 = std::max (a1, s0 + s.dur * 0.35f), b1 = std::max (a2, e0 - tout);
                kn.push_back ({ a1, s.tg.a[k] });
                kn.push_back ({ a2, s.tg.a[k] });
                kn.push_back ({ b1, s.tg.b[k] });
            }
            else
            {
                kn.push_back ({ s0 + tin, s.tg.a[k] });
                kn.push_back ({ std::max (s0 + tin, e0 - tout), s.tg.b[k] });
            }
            kn.push_back ({ e0, vR });
        }
    }

    const size_t nf = (size_t) std::ceil (sc.duration / Script::kHop) + 1;
    sc.frames.resize (nf);
    for (int k = 0; k < 6; ++k)
    {
        const auto& kn = knots[k];
        size_t j = 0;
        for (size_t f = 0; f < nf; ++f)
        {
            const float tf = (float) f * Script::kHop;
            while (j + 1 < kn.size() && kn[j + 1].t <= tf) ++j;
            float v;
            if (tf <= kn.front().t) v = kn.front().v;
            else if (j + 1 >= kn.size()) v = kn.back().v;
            else
            {
                const float dt = kn[j + 1].t - kn[j].t;
                v = dt > 1e-6f ? kn[j].v + (kn[j + 1].v - kn[j].v) * (tf - kn[j].t) / dt : kn[j + 1].v;
            }
            float* dst = &sc.frames[f].f1;
            dst[k] = v;
        }
    }

    // ---- источники
    auto dbToLin = [] (float db) { return db <= 0 ? 0.0f : std::pow (10.0f, (db - 60.0f) / 20.0f); };
    size_t si = 0;
    for (size_t f = 0; f < nf; ++f)
    {
        const float tf = (float) f * Script::kHop + Script::kHop * 0.5f;
        while (si + 1 < segs.size() && start[si + 1] <= tf) ++si;
        auto& fr = sc.frames[f];
        if (tf < start[0] || tf >= start.back() + segs.back().dur) continue;   // тишина по краям
        const auto& s = segs[si];
        const auto& d = phon (s.ph);
        const float u = tf - start[si], dur = s.dur;
        auto setFric = [&] (float amount)
        {
            fr.af = d.af * amount;
            fr.a2 = dbToLin (d.a2); fr.a3 = dbToLin (d.a3); fr.a4 = dbToLin (d.a4);
            fr.a5 = dbToLin (d.a5); fr.a6 = dbToLin (d.a6); fr.ab = dbToLin (d.ab);
        };
        switch (d.kind)
        {
            case Kind::Vowel: case Kind::Diphthong:
                fr.av = s.stress == 1 ? 1.0f : (s.stress == 2 ? 0.92f : 0.8f);
                break;
            case Kind::Liquid: case Kind::Glide: case Kind::Nasal:
                fr.av = d.av;
                fr.fnz = d.fnz;
                break;
            case Kind::Fricative:
            {
                fr.av = d.av;
                const float ramp = std::clamp (std::min (u, dur - u) / 0.015f, 0.3f, 1.0f);
                setFric (ramp);
                break;
            }
            case Kind::Stop:
            {
                // длина взрыва: у K/G «шуршащий» длиннее, у губных короче
                const float burst = (s.ph == K || s.ph == G) ? (d.voiced ? 0.015f : 0.025f)
                                  : (s.ph == T || s.ph == D) ? (d.voiced ? 0.010f : 0.018f) : (d.voiced ? 0.008f : 0.012f);
                if (u < dur - burst) fr.av = d.voiced ? 0.12f : 0.0f;
                else setFric (s.wordFinal && (si + 1 >= segs.size() || segs[si + 1].ph == PAU) ? 0.6f : 1.0f);
                break;
            }
            case Kind::Affricate:
            {
                if (u < dur * 0.4f) fr.av = d.voiced ? 0.12f : 0.0f;
                else { fr.av = d.av; setFric (std::clamp ((dur - u) / 0.02f, 0.4f, 1.0f)); }
                break;
            }
            case Kind::Aspirate:
                fr.ah = 0.55f;
                break;
            case Kind::Pause:
                break;
        }
        // придыхание после глухой смычной (кроме sp, st, sk)
        if ((isVowel (s.ph) || d.kind == Kind::Liquid || d.kind == Kind::Glide) && si > 0
            && voicelessStop (segs[si - 1].ph) && ! segs[si - 1].afterS)
        {
            const float vot = segs[si - 1].ph == P ? 0.045f : (segs[si - 1].ph == T ? 0.055f : 0.06f);
            if (u < vot)
            {
                const float k = std::clamp ((u - (vot - 0.012f)) / 0.012f, 0.0f, 1.0f);
                fr.ah = 0.5f * (1.0f - k);
                fr.av *= k;
            }
        }
        // мягкое начало голоса после тишины и конец перед ней
        if (fr.av > 0)
        {
            if (si == 0 || segs[si - 1].ph == PAU) fr.av *= std::clamp (u / 0.012f, 0.0f, 1.0f);
            if (si + 1 >= segs.size() || segs[si + 1].ph == PAU) fr.av *= std::clamp ((dur - u) / 0.03f, 0.0f, 1.0f);
        }
    }

    // ---- интонация: понижение к концу фразы, акценты на ударных слогах, финальный спад или подъём
    auto bump = [] (float x) { return x <= 0 || x >= 1 ? 0.0f : 0.5f - 0.5f * std::cos (x * 6.2831853f); };
    for (const auto& ph : phrases)
    {
        const float ps = sc.syls[(size_t) ph.firstSyl].start, pe = sc.syls[(size_t) ph.lastSyl].end;
        int lastStressed = ph.lastSyl;
        for (int k = ph.lastSyl; k >= ph.firstSyl; --k) if (sc.syls[(size_t) k].stress == 1) { lastStressed = k; break; }
        const float fallStart = sc.syls[(size_t) lastStressed].hold;
        for (size_t f = (size_t) std::max (0.0f, ps / Script::kHop); f < nf && (float) f * Script::kHop <= pe; ++f)
        {
            const float tf = (float) f * Script::kHop;
            float p = 1.5f - 3.0f * (tf - ps) / std::max (0.05f, pe - ps);
            for (int k = ph.firstSyl; k <= ph.lastSyl; ++k)
            {
                const auto& sy = sc.syls[(size_t) k];
                const float h = sy.stress == 1 ? 2.5f : (sy.stress == 2 ? 1.0f : 0.0f);
                if (h > 0) p += h * bump ((tf - sy.start) / std::max (0.05f, sy.end - sy.start));
            }
            if (tf > fallStart)
            {
                const float x = std::min (1.0f, (tf - fallStart) / std::max (0.05f, pe - fallStart));
                p += ph.type == 2 ? 5.0f * x * x : -3.0f * x;
            }
            sc.frames[f].pitch = p;
        }
    }
    return sc;
}
} // namespace t2m
