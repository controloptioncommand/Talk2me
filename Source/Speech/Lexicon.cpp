#include "Lexicon.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

namespace t2m
{
namespace
{
std::function<std::string()>& source()
{
    static std::function<std::string()> s;
    return s;
}

std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

// слово строки словаря: до пробела
inline const char* lineWordEnd (const char* p)
{
    while (*p != ' ' && *p != '\n' && *p != 0) ++p;
    return p;
}

int compareWord (const char* line, const std::string& w)
{
    const char* e = lineWordEnd (line);
    const size_t n = (size_t) (e - line);
    const int c = std::memcmp (line, w.data(), std::min (n, w.size()));
    if (c != 0) return c;
    return n < w.size() ? -1 : (n > w.size() ? 1 : 0);
}
} // namespace

void Lexicon::setSource (std::function<std::string()> loader) { source() = std::move (loader); }

const Lexicon& Lexicon::get()
{
    static const Lexicon lex;
    return lex;
}

Lexicon::Lexicon()
{
    if (source()) text = source()();
    // индекс: только основные варианты (без «(2)») и без комментариев
    size_t i = 0;
    while (i < text.size())
    {
        const size_t start = i;
        size_t e = text.find ('\n', i);
        if (e == std::string::npos) e = text.size();
        // обрезаем комментарий « # …»
        for (size_t k = start + 1; k < e; ++k)
            if (text[k] == '#' && text[k - 1] == ' ') { text[k - 1] = '\n'; break; }
        size_t sp = start;
        while (sp < e && text[sp] != ' ') ++sp;
        if (sp < e && sp > start && text[start] != ';' && text[sp - 1] != ')')   // «word(2)» — вариант, пропускаем
            index.push_back ((uint32_t) start);
        i = e + 1;
    }
    const char* base = text.c_str();
    std::sort (index.begin(), index.end(), [base] (uint32_t a, uint32_t b)
    {
        const char* ea = lineWordEnd (base + a);
        const char* eb = lineWordEnd (base + b);
        const size_t na = (size_t) (ea - (base + a)), nb = (size_t) (eb - (base + b));
        const int c = std::memcmp (base + a, base + b, std::min (na, nb));
        return c != 0 ? c < 0 : na < nb;
    });
}

bool Lexicon::parsePhonemes (const std::string& s, Pron& out)
{
    size_t i = 0;
    bool any = false;
    while (i < s.size())
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '-')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '-' && s[j] != '\n') ++j;
        if (j == i) break;
        std::string tok = s.substr (i, j - i);
        for (auto& c : tok) c = (char) std::toupper ((unsigned char) c);
        int8_t stress = -1;
        if (! tok.empty() && std::isdigit ((unsigned char) tok.back()))
        {
            stress = (int8_t) (tok.back() - '0');
            tok.pop_back();
        }
        Ph p = phFromName (tok);
        if (p == AH && stress == 0) p = AX;
        if (p == ER && stress == 0) p = AXR;
        if (p == NumPh) { i = j; continue; }
        if (isVowel (p) && stress < 0) stress = (p == AX || p == AXR) ? 0 : 1;
        if (! isVowel (p)) stress = -1;
        out.push_back ({ p, stress });
        any = true;
        i = j;
    }
    return any;
}

bool Lexicon::lookup (const std::string& w, Pron& out) const
{
    if (w.empty() || index.empty()) return false;
    const char* base = text.c_str();
    auto it = std::lower_bound (index.begin(), index.end(), w, [base] (uint32_t off, const std::string& key)
    {
        return compareWord (base + off, key) < 0;
    });
    if (it == index.end() || compareWord (base + *it, w) != 0) return false;
    const char* p = lineWordEnd (base + *it);
    const char* e = std::strchr (p, '\n');
    const std::string phones = e != nullptr ? std::string (p, (size_t) (e - p)) : std::string (p);
    return parsePhonemes (phones, out);
}

Pron Lexicon::letterName (char c)
{
    static const char* names[26] = {
        "EY1", "B IY1", "S IY1", "D IY1", "IY1", "EH1 F", "JH IY1", "EY1 CH", "AY1", "JH EY1", "K EY1", "EH1 L", "EH1 M",
        "EH1 N", "OW1", "P IY1", "K Y UW1", "AA1 R", "EH1 S", "T IY1", "Y UW1", "V IY1", "D AH1 B AX L Y UW0", "EH1 K S",
        "W AY1", "Z IY1" };
    Pron p;
    c = (char) std::toupper ((unsigned char) c);
    if (c >= 'A' && c <= 'Z') parsePhonemes (names[c - 'A'], p);
    return p;
}

std::string Lexicon::numberToWords (const std::string& digits)
{
    static const char* ones[20] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                    "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
    static const char* tens[10] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
    // длинные числа и числа с ведущим нулём — по цифрам
    if (digits.empty()) return {};
    if (digits.size() > 9 || (digits.size() > 1 && digits[0] == '0'))
    {
        std::string r;
        for (char c : digits) { if (! r.empty()) r += ' '; r += ones[c - '0']; }
        return r;
    }
    long n = std::stol (digits);
    if (n == 0) return "zero";
    auto below1000 = [&] (int v)
    {
        std::string r;
        if (v >= 100) { r += ones[v / 100]; r += " hundred"; v %= 100; if (v) r += ' '; }
        if (v >= 20) { r += tens[v / 10]; v %= 10; if (v) { r += ' '; r += ones[v]; } }
        else if (v > 0) r += ones[v];
        return r;
    };
    // годы вида 1996 → nineteen ninety six
    if (digits.size() == 4 && n >= 1100 && n <= 2099 && n % 1000 >= 10 && (n / 100) % 10 != 0)
    {
        std::string r = below1000 ((int) (n / 100));
        const int lo = (int) (n % 100);
        r += ' ';
        r += lo < 10 ? std::string ("oh ") + ones[lo] : below1000 (lo);
        return r;
    }
    std::string r;
    if (n >= 1000000) { r += below1000 ((int) (n / 1000000)) + " million"; n %= 1000000; if (n) r += ' '; }
    if (n >= 1000) { r += below1000 ((int) (n / 1000)) + " thousand"; n %= 1000; if (n) r += ' '; }
    if (n > 0) r += below1000 ((int) n);
    return r;
}

// ===========================================================================
// Правила «буква → звук» (по мотивам NRL). Контекст:
//  # — одна или больше гласных, : — ноль или больше согласных, ^ — одна согласная,
//  . — звонкая согласная (B D V G J L M N R W Z), + — E I Y, % — суффикс (ER E ES ED ING ELY),
//  & — шипящая (S C G Z X J CH SH), @ — T S R D L Z N J TH CH SH, пробел — граница слова.
namespace
{
struct Rule { const char* left; const char* match; const char* right; const char* out; };

const Rule rulesTable[] = {
    // A
    { " ", "A", " ", "AH0" }, { " ", "ARE", " ", "AA1 R" }, { " ", "AR", "O", "AX R" }, { "", "AR", "#", "EH1 R" },
    { "^", "AS", "#", "EY1 S" }, { "", "A", "WA", "AX" }, { "", "AW", "", "AO1" }, { " :", "ANY", "", "EH1 N IY0" },
    { "", "A", "^+#", "EY1" }, { "#:", "ALLY", "", "AX L IY0" }, { " ", "AL", "#", "AX L" }, { "", "AGAIN", "", "AX G EH1 N" },
    { "#:", "AG", "E", "IH0 JH" }, { "", "A", "^+:#", "AE1" }, { " :", "A", "^+ ", "EY1" }, { "", "A", "^%", "EY1" },
    { " ", "ARR", "", "AX R" }, { "", "ARR", "", "AE1 R" }, { " :", "AR", " ", "AA1 R" }, { "", "AR", " ", "ER0" },
    { "", "AR", "", "AA1 R" }, { "", "AIR", "", "EH1 R" }, { "", "AI", "", "EY1" }, { "", "AY", "", "EY1" },
    { "", "AU", "", "AO1" }, { "#:", "AL", " ", "AX L" }, { "#:", "ALS", " ", "AX L Z" }, { "", "ALK", "", "AO1 K" },
    { "", "AL", "^", "AO1 L" }, { " :", "ABLE", "", "EY1 B AX L" }, { "", "ABLE", "", "AX B AX L" }, { "", "ANG", "+", "EY1 N JH" },
    { "", "A", "", "AE1" },
    // B
    { " ", "BE", "^#", "B IH0" }, { "", "BEING", "", "B IY1 IH0 NG" }, { " ", "BOTH", " ", "B OW1 TH" },
    { " ", "BUS", "#", "B IH1 Z" }, { "", "BUIL", "", "B IH1 L" }, { "", "BB", "", "B" }, { "", "B", "", "B" },
    // C
    { " ", "CH", "^", "K" }, { "^E", "CH", "", "K" }, { "", "CH", "", "CH" }, { " S", "CI", "#", "S AY1" },
    { "", "CI", "A", "SH" }, { "", "CI", "O", "SH" }, { "", "CI", "EN", "SH" }, { "", "C", "+", "S" }, { "", "CK", "", "K" },
    { "", "COM", "%", "K AH1 M" }, { "", "CC", "+", "K S" }, { "", "CC", "", "K" }, { "", "C", "", "K" },
    // D
    { "#:", "DED", " ", "D IH0 D" }, { ".E", "D", " ", "D" }, { "#^:E", "D", " ", "T" }, { " ", "DE", "^#", "D IH0" },
    { " ", "DO", " ", "D UW1" }, { " ", "DOES", "", "D AH1 Z" }, { " ", "DOING", "", "D UW1 IH0 NG" }, { " ", "DOW", "", "D AW1" },
    { "", "DU", "A", "JH UW1" }, { "", "DD", "", "D" }, { "", "D", "", "D" },
    // E
    { "#:", "E", " ", "" }, { "'^:", "E", " ", "" }, { " :", "E", " ", "IY1" }, { "#", "ED", " ", "D" }, { "#:", "E", "D ", "" },
    { "", "EV", "ER", "EH1 V" }, { "", "E", "^%", "IY1" }, { "", "ERI", "#", "IY1 R IY0" }, { "", "ERI", "", "EH1 R IH0" },
    { "#:", "ER", "#", "ER0" }, { "", "ER", "#", "EH1 R" }, { "", "ER", "", "ER1" }, { " ", "EVEN", "", "IY1 V AX N" },
    { "#:", "E", "W", "" }, { "@", "EW", "", "UW1" }, { "", "EW", "", "Y UW1" }, { "", "E", "O", "IY0" },
    { "#:&", "ES", " ", "IH0 Z" }, { "#:", "E", "S ", "" }, { "#:", "ELY", " ", "L IY0" }, { "#:", "EMENT", "", "M AX N T" },
    { "", "EFUL", "", "F UH0 L" }, { "", "EE", "", "IY1" }, { "", "EARN", "", "ER1 N" }, { " ", "EAR", "^", "ER1" },
    { "", "EAD", "", "EH1 D" }, { "#:", "EA", " ", "IY0 AX" }, { "", "EA", "SU", "EH1" }, { "", "EA", "", "IY1" },
    { "", "EIGH", "", "EY1" }, { "", "EI", "", "IY1" }, { " ", "EYE", "", "AY1" }, { "", "EY", "", "IY0" }, { "", "EU", "", "Y UW1" },
    { "", "E", "", "EH1" },
    // F
    { "", "FUL", "", "F UH0 L" }, { "", "FF", "", "F" }, { "", "F", "", "F" },
    // G
    { "", "GIV", "", "G IH1 V" }, { " ", "G", "I^", "G" }, { "", "GE", "T", "G EH1" }, { "SU", "GGES", "", "G JH EH1 S" },
    { "", "GG", "", "G" }, { " B#", "G", "", "G" }, { "", "G", "+", "JH" }, { "", "GREAT", "", "G R EY1 T" }, { "#", "GH", "", "" },
    { "", "G", "", "G" },
    // H
    { " ", "HAV", "", "HH AE1 V" }, { " ", "HERE", "", "HH IY1 R" }, { " ", "HOUR", "", "AW1 AXR" }, { "", "HOW", "", "HH AW1" },
    { "", "H", "#", "HH" }, { "", "H", "", "" },
    // I
    { " ", "IN", "", "IH1 N" }, { " ", "I", " ", "AY1" }, { "", "IN", "D", "AY1 N" }, { "", "IER", "", "IY0 AXR" },
    { "#:R", "IED", " ", "IY0 D" }, { "", "IED", " ", "AY1 D" }, { "", "IEN", "", "IY0 AX N" }, { "", "IE", "T", "AY1 AX" },
    { " :", "I", "%", "AY1" }, { "", "I", "%", "IY1" }, { "", "IE", "", "IY1" }, { "", "I", "^+:#", "IH1" }, { "", "IR", "#", "AY1 R" },
    { "", "IZ", "%", "AY1 Z" }, { "", "IS", "%", "AY1 Z" }, { "", "I", "D%", "AY1" }, { "+^", "I", "^+", "IH1" },
    { "", "I", "T%", "AY1" }, { "#^:", "I", "^+", "IH1" }, { "", "I", "^+", "AY1" }, { "", "IR", "", "ER1" }, { "", "IGH", "", "AY1" },
    { "", "ILD", "", "AY1 L D" }, { "", "IGN", " ", "AY1 N" }, { "", "IGN", "^", "AY1 N" }, { "", "IGN", "%", "AY1 N" },
    { "", "IQUE", "", "IY1 K" }, { "", "I", "", "IH1" },
    // J K L M
    { "", "J", "", "JH" }, { " ", "K", "N", "" }, { "", "KK", "", "K" }, { "", "K", "", "K" },
    { "", "LO", "C#", "L OW1" }, { "L", "L", "", "" }, { "#^:", "L", "%", "AX L" }, { "", "LEAD", "", "L IY1 D" }, { "", "L", "", "L" },
    { "", "MOV", "", "M UW1 V" }, { "", "MM", "", "M" }, { "", "M", "", "M" },
    // N
    { "E", "NG", "+", "N JH" }, { "", "NG", "R", "NG G" }, { "", "NG", "#", "NG G" }, { "", "NGL", "%", "NG G AX L" },
    { "", "NG", "", "NG" }, { "", "NK", "", "NG K" }, { " ", "NOW", " ", "N AW1" }, { "", "NN", "", "N" }, { "", "N", "", "N" },
    // O
    { "", "OF", " ", "AX V" }, { "", "OROUGH", "", "ER1 OW0" }, { "#:", "OR", " ", "AXR" }, { "#:", "ORS", " ", "AXR Z" },
    { "", "OR", "", "AO1 R" }, { " ", "ONE", "", "W AH1 N" }, { "", "OW", "", "OW1" }, { " ", "OVER", "", "OW1 V AXR" },
    { "", "OV", "", "AH1 V" }, { "", "O", "^%", "OW1" }, { "", "O", "^EN", "OW1" }, { "", "O", "^I#", "OW1" }, { "", "OL", "D", "OW1 L" },
    { "", "OUGHT", "", "AO1 T" }, { "", "OUGH", "", "AH1 F" }, { " ", "OU", "", "AW1" }, { "H", "OU", "S#", "AW1" },
    { "", "OUS", "", "AX S" }, { "", "OUR", "", "AO1 R" }, { "", "OULD", "", "UH1 D" }, { "^", "OU", "^L", "AH1" },
    { "", "OUP", "", "UW1 P" }, { "", "OU", "", "AW1" }, { "", "OY", "", "OY1" }, { "", "OING", "", "OW1 IH0 NG" },
    { "", "OI", "", "OY1" }, { "", "OOR", "", "AO1 R" }, { "", "OOK", "", "UH1 K" }, { "", "OOD", "", "UH1 D" }, { "", "OO", "", "UW1" },
    { "", "O", "E", "OW1" }, { "", "O", " ", "OW1" }, { "", "OA", "", "OW1" }, { " ", "ONLY", "", "OW1 N L IY0" },
    { " ", "ONCE", "", "W AH1 N S" }, { "", "ON'T", "", "OW1 N T" }, { "C", "O", "N", "AA1" }, { "", "O", "NG", "AO1" },
    { " :^", "O", "N", "AH1" }, { "I", "ON", "", "AX N" }, { "#:", "ON", " ", "AX N" }, { "#^", "ON", "", "AX N" },
    { "", "O", "ST ", "OW1" }, { "", "OF", "^", "AO1 F" }, { "", "OTHER", "", "AH1 DH AXR" }, { "", "OSS", " ", "AO1 S" },
    { "#^:", "OM", "", "AH1 M" }, { "", "O", "", "AA1" },
    // P Q R
    { "", "PH", "", "F" }, { "", "PEOP", "", "P IY1 P" }, { "", "POW", "", "P AW1" }, { "", "PUT", " ", "P UH1 T" },
    { "", "PP", "", "P" }, { "", "P", "", "P" },
    { "", "QUAR", "", "K W AO1 R" }, { "", "QU", "", "K W" }, { "", "Q", "", "K" },
    { " ", "RE", "^#", "R IY0" }, { "", "RR", "", "R" }, { "", "R", "", "R" },
    // S
    { "", "SH", "", "SH" }, { "#", "SION", "", "ZH AX N" }, { "", "SOME", "", "S AH1 M" }, { "#", "SUR", "#", "ZH AXR" },
    { "", "SUR", "#", "SH AXR" }, { "#", "SU", "#", "ZH UW0" }, { "#", "SSU", "#", "SH UW0" }, { "#", "SED", " ", "Z D" },
    { "#", "S", "#", "Z" }, { "", "SAID", "", "S EH1 D" }, { "^", "SION", "", "SH AX N" }, { "", "S", "S", "" },
    { ".", "S", " ", "Z" }, { "#:.E", "S", " ", "Z" }, { "#^:##", "S", " ", "Z" }, { "#^:#", "S", " ", "S" }, { "U", "S", " ", "S" },
    { " :#", "S", " ", "Z" }, { " ", "SCH", "", "S K" }, { "", "S", "C+", "" }, { "#", "SM", "", "Z AX M" }, { "", "S", "", "S" },
    // T
    { " ", "THE", " ", "DH AX" }, { "", "TO", " ", "T UW1" }, { "", "THAT", " ", "DH AE1 T" }, { " ", "THIS", " ", "DH IH1 S" },
    { " ", "THEY", "", "DH EY1" }, { " ", "THERE", "", "DH EH1 R" }, { "", "THER", "", "DH AXR" }, { "", "THEIR", "", "DH EH1 R" },
    { " ", "THAN", " ", "DH AE1 N" }, { " ", "THEM", " ", "DH EH1 M" }, { "", "THESE", " ", "DH IY1 Z" }, { " ", "THEN", "", "DH EH1 N" },
    { "", "THROUGH", "", "TH R UW1" }, { "", "THOSE", "", "DH OW1 Z" }, { "", "THOUGH", " ", "DH OW1" }, { " ", "THUS", "", "DH AH1 S" },
    { "", "TH", "", "TH" }, { "#:", "TED", " ", "T IH0 D" }, { "S", "TI", "#N", "CH" }, { "", "TI", "O", "SH" }, { "", "TI", "A", "SH" },
    { "", "TIEN", "", "SH AX N" }, { "", "TUR", "#", "CH AXR" }, { "", "TU", "A", "CH UW1" }, { " ", "TWO", "", "T UW1" },
    { "", "TT", "", "T" }, { "", "T", "", "T" },
    // U
    { " ", "UN", "I", "Y UW1 N" }, { " ", "UN", "", "AH1 N" }, { " ", "UPON", "", "AX P AO1 N" }, { "@", "UR", "#", "UH1 R" },
    { "", "UR", "#", "Y UH1 R" }, { "", "UR", "", "ER1" }, { "", "U", "^ ", "AH1" }, { "", "U", "^^", "AH1" }, { "", "UY", "", "AY1" },
    { " G", "U", "#", "" }, { "G", "U", "%", "" }, { "G", "U", "#", "W" }, { "#N", "U", "", "Y UW1" }, { "@", "U", "", "UW1" },
    { "", "U", "", "Y UW1" },
    // V W X Y Z
    { "", "VIEW", "", "V Y UW1" }, { "", "V", "", "V" },
    { " ", "WERE", "", "W ER1" }, { "", "WA", "S", "W AA1" }, { "", "WA", "T", "W AA1" }, { "", "WHERE", "", "W EH1 R" },
    { "", "WHAT", "", "W AA1 T" }, { "", "WHOL", "", "HH OW1 L" }, { "", "WHO", "", "HH UW1" }, { "", "WH", "", "W" },
    { "", "WAR", "", "W AO1 R" }, { "", "WOR", "^", "W ER1" }, { "", "WR", "", "R" }, { "", "W", "", "W" },
    { "", "X", "", "K S" },
    { "", "YOUNG", "", "Y AH1 NG" }, { " ", "YOU", "", "Y UW1" }, { " ", "YES", "", "Y EH1 S" }, { " ", "Y", "", "Y" },
    { "#^:", "Y", " ", "IY0" }, { "#^:", "Y", "I", "IY0" }, { " :", "Y", " ", "AY1" }, { " :", "Y", "#", "AY1" },
    { " :", "Y", "^+:#", "IH1" }, { " :", "Y", "^#", "AY1" }, { "", "Y", "", "IH1" },
    { "", "ZZ", "", "Z" }, { "", "Z", "", "Z" },
    // апостроф и прочее
    { "", "'", "", "" },
};

bool isV (char c) { return std::strchr ("AEIOUY", c) != nullptr && c != 0; }
bool isC (char c) { return c != 0 && std::isalpha ((unsigned char) c) && std::strchr ("AEIOU", c) == nullptr; }   // Y — согласная
bool isBound (char c) { return ! std::isalpha ((unsigned char) c) && c != '\''; }

bool matchRight (const std::string& w, size_t p, const char* pat)
{
    for (; *pat; ++pat)
    {
        const char c = *pat;
        const char x = p < w.size() ? w[p] : ' ';
        switch (c)
        {
            case ' ': if (! isBound (x)) return false; ++p; break;
            case '#': if (! isV (x)) return false; while (p < w.size() && isV (w[p])) ++p; break;
            case ':': while (p < w.size() && isC (w[p])) ++p; break;
            case '^': if (! isC (x)) return false; ++p; break;
            case '.': if (std::strchr ("BDVGJLMNRWZ", x) == nullptr || x == 0) return false; ++p; break;
            case '+': if (std::strchr ("EIY", x) == nullptr || x == 0) return false; ++p; break;
            case '%':
            {
                static const char* suf[] = { "ING", "ELY", "ER", "ES", "ED", "E" };
                bool ok = false;
                for (auto* s : suf)
                {
                    const size_t n = std::strlen (s);
                    if (w.compare (p, n, s) == 0) { p += n; ok = true; break; }
                }
                if (! ok) return false;
                break;
            }
            default: if (x != c) return false; ++p; break;
        }
    }
    return true;
}

bool matchLeft (const std::string& w, long p, const char* pat)
{
    const long n = (long) std::strlen (pat);
    for (long k = n - 1; k >= 0; --k)
    {
        const char c = pat[k];
        const char x = p >= 0 ? w[(size_t) p] : ' ';
        switch (c)
        {
            case ' ': if (! isBound (x)) return false; --p; break;
            case '#': if (! isV (x)) return false; while (p >= 0 && isV (w[(size_t) p])) --p; break;
            case ':': while (p >= 0 && isC (w[(size_t) p])) --p; break;
            case '^': if (! isC (x)) return false; --p; break;
            case '.': if (std::strchr ("BDVGJLMNRWZ", x) == nullptr || x == 0) return false; --p; break;
            case '+': if (std::strchr ("EIY", x) == nullptr || x == 0) return false; --p; break;
            case '&':
                if (x == 'H' && p > 0 && std::strchr ("CS", w[(size_t) p - 1]) != nullptr) p -= 2;
                else if (x != 0 && std::strchr ("SCGZXJ", x) != nullptr) --p;
                else return false;
                break;
            case '@':
                if (x == 'H' && p > 0 && std::strchr ("TCS", w[(size_t) p - 1]) != nullptr) p -= 2;
                else if (x != 0 && std::strchr ("TSRDLZNJ", x) != nullptr) --p;
                else return false;
                break;
            default: if (x != c) return false; --p; break;
        }
    }
    return true;
}
} // namespace

Pron Lexicon::rules (const std::string& lw)
{
    std::string w = " ";
    for (char c : lw) w += (char) std::toupper ((unsigned char) c);
    w += " ";
    Pron out;
    size_t i = 1;
    while (i + 1 < w.size())
    {
        bool matched = false;
        for (const auto& r : rulesTable)
        {
            const size_t n = std::strlen (r.match);
            if (w.compare (i, n, r.match) != 0) continue;
            if (! matchRight (w, i + n, r.right)) continue;
            if (! matchLeft (w, (long) i - 1, r.left)) continue;
            parsePhonemes (r.out, out);
            i += n;
            matched = true;
            break;
        }
        if (! matched) ++i;
    }
    // одно главное ударение: первое «1» остаётся, остальные — вторичные
    bool primary = false;
    for (auto& ps : out)
        if (ps.stress == 1) { if (primary) ps.stress = 2; primary = true; }
    if (! primary)
        for (auto& ps : out) if (isVowel (ps.ph) && ps.ph != AX && ps.ph != AXR) { ps.stress = 1; break; }
    return out;
}

bool Lexicon::pronounce (const std::string& word, Pron& out) const
{
    std::string w;
    for (char c : word) if (std::isalpha ((unsigned char) c) || c == '\'') w += c;
    while (! w.empty() && w.front() == '\'') w.erase (w.begin());
    while (! w.empty() && w.back() == '\'') w.pop_back();
    if (w.empty()) return false;

    // одиночная заглавная буква (кроме «I» и «A» в начале фразы) — название буквы: «S P E L L»
    if (w.size() == 1 && std::isupper ((unsigned char) w[0]) && w[0] != 'I')
    {
        auto p = letterName (w[0]);
        out.insert (out.end(), p.begin(), p.end());
        return true;
    }
    const std::string lw = lower (w);
    if (lookup (lw, out)) return true;

    // окончания: стем из словаря + суффикс
    auto lastVoiced = [] (const Pron& p) { return ! p.empty() && (isVowel (p.back().ph) || phon (p.back().ph).voiced); };
    auto lastIs = [] (const Pron& p, std::initializer_list<Ph> set) { if (p.empty()) return false; for (auto s : set) if (p.back().ph == s) return true; return false; };
    auto tryStem = [&] (const std::string& stem, Pron& p) { return stem.size() >= 2 && (lookup (stem, p) || lookup (stem + "e", p)); };
    struct Suffix { const char* s; int kind; };   // 0 s, 1 ed, 2 ing, 3 er, 4 ly, 5 's
    static const Suffix sfx[] = { { "'s", 5 }, { "ing", 2 }, { "es", 0 }, { "ed", 1 }, { "er", 3 }, { "ly", 4 }, { "s", 0 } };
    for (auto& sf : sfx)
    {
        const size_t n = std::strlen (sf.s);
        if (lw.size() <= n + 1 || lw.compare (lw.size() - n, n, sf.s) != 0) continue;
        std::string stem = lw.substr (0, lw.size() - n);
        Pron p;
        bool ok = tryStem (stem, p);
        if (! ok && stem.size() > 2 && stem[stem.size() - 1] == stem[stem.size() - 2]) ok = tryStem (stem.substr (0, stem.size() - 1), p);   // running → run
        if (! ok && (sf.kind == 0 || sf.kind == 1) && stem.back() == 'i') ok = tryStem (stem.substr (0, stem.size() - 1) + "y", p);   // cities → city
        if (! ok) continue;
        switch (sf.kind)
        {
            case 0: case 5:
                if (lastIs (p, { S, Z, SH, ZH, CH, JH })) { p.push_back ({ IH, 0 }); p.push_back ({ Z, -1 }); }
                else p.push_back ({ lastVoiced (p) ? Z : S, -1 });
                break;
            case 1:
                if (lastIs (p, { T, D })) { p.push_back ({ IH, 0 }); p.push_back ({ D, -1 }); }
                else p.push_back ({ lastVoiced (p) ? D : T, -1 });
                break;
            case 2: p.push_back ({ IH, 0 }); p.push_back ({ NG, -1 }); break;
            case 3: p.push_back ({ AXR, 0 }); break;
            case 4: p.push_back ({ L, -1 }); p.push_back ({ IY, 0 }); break;
            default: break;
        }
        out.insert (out.end(), p.begin(), p.end());
        return true;
    }
    auto r = rules (lw);
    if (r.empty()) return false;
    out.insert (out.end(), r.begin(), r.end());
    return true;
}
} // namespace t2m
