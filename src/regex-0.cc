#include <string>
#include <iostream>
#include <stdint.h>

namespace regex {


bool MatchCharacter(const std::string& pattern, size_t pos, char c) {
    if (c == '\0') return false;
    if (pattern[pos] == '.') return true; 
    if (pattern[pos] == '\\' && pos + 1 < pattern.length()) {
        switch (pattern[pos + 1]) {
            case 'a': return std::isalpha(c);
            case 'd': return std::isdigit(c);
            case 'l': return std::islower(c);
            case 'p': return std::ispunct(c);
            case 's': return std::isspace(c);
            case 'u': return std::isupper(c);
            case 'w': return std::isalnum(c);
            case 'A': return !std::isalpha(c);
            case 'D': return !std::isdigit(c);
            case 'L': return !std::islower(c);
            case 'P': return !std::ispunct(c);
            case 'S': return !std::isspace(c);
            case 'U': return !std::isupper(c);
            case 'W': return !std::isalnum(c);
            default: return pattern[pos + 1] == c;
        }
    }
    return pattern[pos] == c;
}

bool MatchPattern(const std::string& re, const std::string& str, uint32_t& n) {
    if (re.empty()) return true;
    if (re[0] == '$' && re.length() == 1) return str.empty();

    size_t cn = (re[0] == '\\') ? 2 : 1;
    std::string next = re.substr(cn);

    if (!next.empty() && next[0] == '*') {
        next = next.substr(1);
        size_t pos = 0;
        do {
            uint32_t save = n;
            if (MatchPattern(next, str.substr(pos), n)) return true;
            n = save + 1;
            pos++;
        } while (pos <= str.length() && MatchCharacter(re, 0, str[pos-1]));
        return false;
    }

    if (!next.empty() && next[0] == '?') {
        next = next.substr(1);
        if (!str.empty() && MatchCharacter(re, 0, str[0])) {
            ++n;
            if (MatchPattern(next, str.substr(1), n)) return true;
            --n;
        }
        return MatchPattern(next, str, n);
    }

    ++n;
    return !str.empty() && MatchCharacter(re, 0, str[0]) &&
           MatchPattern(next, str.substr(1), n);
}

int32_t MatchRegex(const std::string re, const std::string& str, uint32_t& n) {
    if (re[0] == '^') {
        n = 0;
        if (MatchPattern(re.substr(1), str, n)) return 0;
        return -1;
    }    

    for (size_t pos = 0; pos <= str.length(); ++pos) {
        n = 0;
        if (MatchPattern(re, str.substr(pos), n)) return pos;
    }
    return -1;
}

} // namespace regex

void test(const std::string& re, const std::string& str, bool expect) {
    uint32_t n = 0;
    bool match = regex::MatchRegex(re, str, n) != -1;
    std::cout << re << " | " << str << " = " << (match ? "✓" : "✗") 
              << (match == expect ? "":" FAIL") << std::endl;
}

int main() {
    // Character
    test("a", "a", true);
    test("b", "a", false);
    test(".", "x", true);
    test("\\d", "5", true);
    test("\\d", "c", false);
    test("\\a", "x", true);
    test("\\a", "7", false);

    // Pattern
    test("a*", "aaa", true);
    test("a?", "a", true);
    test("abc", "abc", true);

    // Regex
    test("a*b", "aaab", true);

    return 0;
}
