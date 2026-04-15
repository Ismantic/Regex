// new_regex.cc — Extended regex engine for GPT-4 style pre-tokenization.
// Based on regex.cc architecture: Parser → AST → NFA (Thompson) → DFA → Match
// Extended with: [char class], \p{unicode}, \s \r \n, {m,n}, (?:), FindAll
//
// Unicode properties: \p{A}=alpha, \p{H}=Han, \p{N}=digit, \p{L}=letter

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <map>
#include <queue>
#include <stack>
#include <algorithm>
#include <stdexcept>
#include <stdint.h>

namespace regex {

// ═══════════════════════════════════════════════════════════════════════════
// UTF-8
// ═══════════════════════════════════════════════════════════════════════════

size_t BytesOneUTF8(const char* src) {
    return "\1\1\1\1\1\1\1\1\1\1\1\1\2\2\3\4"[(*src & 0xFF) >> 4];
}

uint32_t DecodeUTF8At(const std::string& str, size_t pos, size_t* bytes) {
    if (pos >= str.size()) { *bytes = 0; return 0; }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(str.data() + pos);
    *bytes = BytesOneUTF8(str.data() + pos);
    if (pos + *bytes > str.size()) { *bytes = 0; return 0; }
    switch (*bytes) {
        case 1: return data[0];
        case 2: return ((data[0]&0x1F)<<6)|(data[1]&0x3F);
        case 3: return ((data[0]&0x0F)<<12)|((data[1]&0x3F)<<6)|(data[2]&0x3F);
        case 4: return ((data[0]&0x07)<<18)|((data[1]&0x3F)<<12)|((data[2]&0x3F)<<6)|(data[3]&0x3F);
        default: *bytes = 0; return 0;
    }
}

std::string EncodeUTF8(uint32_t c) {
    if (c <= 0x7F) return std::string(1, (char)c);
    if (c <= 0x7FF) { std::string r(2,0); r[0]=0xC0|(c>>6); r[1]=0x80|(c&0x3F); return r; }
    if (c <= 0xFFFF) { std::string r(3,0); r[0]=0xE0|(c>>12); r[1]=0x80|((c>>6)&0x3F); r[2]=0x80|(c&0x3F); return r; }
    if (c <= 0x10FFFF) { std::string r(4,0); r[0]=0xF0|(c>>18); r[1]=0x80|((c>>12)&0x3F); r[2]=0x80|((c>>6)&0x3F); r[3]=0x80|(c&0x3F); return r; }
    return "?";
}

// ═══════════════════════════════════════════════════════════════════════════
// Unicode predicates
// ═══════════════════════════════════════════════════════════════════════════

static bool IsHan(uint32_t c) {
    return (c>=0x3400&&c<=0x4DBF)||(c>=0x4E00&&c<=0x9FFF)||
           (c>=0xF900&&c<=0xFAFF)||(c>=0x20000&&c<=0x323AF);
}
static bool IsDigit(uint32_t c) {
    return (c>='0'&&c<='9')||(c>=0xFF10&&c<=0xFF19);
}
static bool IsWordChar(uint32_t c) {
    if (c>='0'&&c<='9') return true;
    if (c>='A'&&c<='Z') return true;
    if (c>='a'&&c<='z') return true;
    if (c>=0x00C0&&c<=0x00FF&&c!=0x00D7&&c!=0x00F7) return true;
    if (c>=0x0100&&c<=0x036F) return true;
    if (c>=0x0370&&c<=0x07FF) return true;
    if (c>=0x0800&&c<=0x085F) return true;
    if (c>=0x0900&&c<=0x1FFF) return true;
    if (c>=0x2E80&&c<=0x2FFF) return true;
    if (c>=0x3040&&c<=0x33FF) return true;
    if (c>=0x3400&&c<=0x9FFF) return true;
    if (c>=0xA000&&c<=0xD7FF) return true;
    if (c>=0xF900&&c<=0xFAFF) return true;
    if (c>=0xFB00&&c<=0xFDFF) return true;
    if (c>=0xFE70&&c<=0xFEFF) return true;
    if (c>=0xFF10&&c<=0xFF19) return true;
    if ((c>=0xFF21&&c<=0xFF3A)||(c>=0xFF41&&c<=0xFF5A)) return true;
    if (c>=0xFF66&&c<=0xFFDC) return true;
    if (c>=0x10000&&c<=0x10FFF) return true;
    if (c>=0x20000&&c<=0x323AF) return true;
    return false;
}
static bool IsAlpha(uint32_t c) { return IsWordChar(c)&&!IsDigit(c)&&!IsHan(c); }
static bool IsLetter(uint32_t c) { return IsWordChar(c)&&!IsDigit(c); }
static bool IsWhitespace(uint32_t c) {
    if (c>=0x09&&c<=0x0D) return true;
    if (c==0x20||c==0x85||c==0xA0) return true;
    if (c==0x1680||(c>=0x2000&&c<=0x200A)) return true;
    if (c==0x2028||c==0x2029||c==0x202F||c==0x205F||c==0x3000) return true;
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// CharPred — predicate-based character matching
// ═══════════════════════════════════════════════════════════════════════════

using CharPred = std::function<bool(uint32_t)>;

// ═══════════════════════════════════════════════════════════════════════════
// AST — same hierarchy as regex.cc, with CharClassAst and RepeatAst added
// ═══════════════════════════════════════════════════════════════════════════

class EmptyAst;
class LiteralAst;
class DotAst;
class CharClassAst;       // NEW
class SequenceAst;
class AlternativeAst;
class StarAst;
class PlusAst;
class OptionalAst;
class RepeatAst;          // NEW

class AstVisitor {
public:
    virtual ~AstVisitor() = default;
    virtual void Visit(const EmptyAst*) = 0;
    virtual void Visit(const LiteralAst*) = 0;
    virtual void Visit(const DotAst*) = 0;
    virtual void Visit(const CharClassAst*) = 0;    // NEW
    virtual void Visit(const SequenceAst*) = 0;
    virtual void Visit(const AlternativeAst*) = 0;
    virtual void Visit(const StarAst*) = 0;
    virtual void Visit(const PlusAst*) = 0;
    virtual void Visit(const OptionalAst*) = 0;
    virtual void Visit(const RepeatAst*) = 0;        // NEW
};

enum class AstType {
    Empty, Literal, Dot, CharClass, Sequence, Alternative, Star, Plus, Optional, Repeat
};

class Ast {
public:
    virtual ~Ast() = default;
    virtual AstType GetType() const = 0;
    virtual void Accept(AstVisitor* v) const = 0;
    virtual std::unique_ptr<Ast> Clone() const = 0;  // needed for {m,n}
};

// --- EmptyAst ---
class EmptyAst : public Ast {
public:
    AstType GetType() const override { return AstType::Empty; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<EmptyAst>(); }
};

// --- LiteralAst --- matches a single Unicode codepoint
class LiteralAst : public Ast {
    uint32_t point_;
public:
    explicit LiteralAst(uint32_t p) : point_(p) {}
    AstType GetType() const override { return AstType::Literal; }
    uint32_t GetPoint() const { return point_; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<LiteralAst>(point_); }
};

// --- DotAst --- matches any char except \r \n
class DotAst : public Ast {
public:
    AstType GetType() const override { return AstType::Dot; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<DotAst>(); }
};

// --- CharClassAst --- NEW: [abc], [^abc], \p{A}, \s, etc.
class CharClassAst : public Ast {
    CharPred pred_;
    std::string repr_;
public:
    CharClassAst(CharPred p, std::string repr) : pred_(std::move(p)), repr_(std::move(repr)) {}
    AstType GetType() const override { return AstType::CharClass; }
    const CharPred& GetPred() const { return pred_; }
    const std::string& GetRepr() const { return repr_; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<CharClassAst>(pred_, repr_); }
};

// --- SequenceAst ---
class SequenceAst : public Ast {
    std::vector<std::unique_ptr<Ast>> elements_;
public:
    void Add(std::unique_ptr<Ast> e) { elements_.push_back(std::move(e)); }
    const std::vector<std::unique_ptr<Ast>>& GetElements() const { return elements_; }
    AstType GetType() const override { return AstType::Sequence; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override {
        auto n = std::make_unique<SequenceAst>();
        for (auto& e : elements_) n->Add(e->Clone());
        return n;
    }
};

// --- AlternativeAst ---
class AlternativeAst : public Ast {
    std::vector<std::unique_ptr<Ast>> branches_;
public:
    void Add(std::unique_ptr<Ast> b) { branches_.push_back(std::move(b)); }
    const std::vector<std::unique_ptr<Ast>>& GetBranches() const { return branches_; }
    AstType GetType() const override { return AstType::Alternative; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override {
        auto n = std::make_unique<AlternativeAst>();
        for (auto& b : branches_) n->Add(b->Clone());
        return n;
    }
};

// --- StarAst, PlusAst, OptionalAst ---
class StarAst : public Ast {
    std::unique_ptr<Ast> element_;
public:
    explicit StarAst(std::unique_ptr<Ast> e) : element_(std::move(e)) {}
    const Ast* GetElement() const { return element_.get(); }
    AstType GetType() const override { return AstType::Star; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<StarAst>(element_->Clone()); }
};

class PlusAst : public Ast {
    std::unique_ptr<Ast> element_;
public:
    explicit PlusAst(std::unique_ptr<Ast> e) : element_(std::move(e)) {}
    const Ast* GetElement() const { return element_.get(); }
    AstType GetType() const override { return AstType::Plus; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<PlusAst>(element_->Clone()); }
};

class OptionalAst : public Ast {
    std::unique_ptr<Ast> element_;
public:
    explicit OptionalAst(std::unique_ptr<Ast> e) : element_(std::move(e)) {}
    const Ast* GetElement() const { return element_.get(); }
    AstType GetType() const override { return AstType::Optional; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override { return std::make_unique<OptionalAst>(element_->Clone()); }
};

// --- RepeatAst --- NEW: {min,max}
class RepeatAst : public Ast {
    std::unique_ptr<Ast> element_;
    int min_, max_;  // max_ == -1 means unlimited
public:
    RepeatAst(std::unique_ptr<Ast> e, int lo, int hi)
        : element_(std::move(e)), min_(lo), max_(hi) {}
    const Ast* GetElement() const { return element_.get(); }
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }
    AstType GetType() const override { return AstType::Repeat; }
    void Accept(AstVisitor* v) const override { v->Visit(this); }
    std::unique_ptr<Ast> Clone() const override {
        return std::make_unique<RepeatAst>(element_->Clone(), min_, max_);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// AST Printer — same as regex.cc, extended with CharClass and Repeat
// ═══════════════════════════════════════════════════════════════════════════

class AstPrinter : public AstVisitor {
    int indent_ = 0;
    void Pad() { for (int i = 0; i < indent_; i++) std::cout << "  "; }
public:
    void Visit(const EmptyAst*) override { Pad(); std::cout << "Empty\n"; }
    void Visit(const LiteralAst* n) override {
        Pad(); uint32_t p = n->GetPoint();
        if (p >= 32 && p < 127) std::cout << "Literal('" << (char)p << "')\n";
        else std::cout << "Literal(U+" << std::hex << p << std::dec << ")\n";
    }
    void Visit(const DotAst*) override { Pad(); std::cout << "Dot\n"; }
    void Visit(const CharClassAst* n) override { Pad(); std::cout << "CharClass(" << n->GetRepr() << ")\n"; }
    void Visit(const SequenceAst* n) override {
        Pad(); std::cout << "Sequence\n"; indent_++;
        for (auto& e : n->GetElements()) e->Accept(this); indent_--;
    }
    void Visit(const AlternativeAst* n) override {
        Pad(); std::cout << "Alternative\n"; indent_++;
        for (auto& b : n->GetBranches()) b->Accept(this); indent_--;
    }
    void Visit(const StarAst* n) override { Pad(); std::cout << "Star\n"; indent_++; n->GetElement()->Accept(this); indent_--; }
    void Visit(const PlusAst* n) override { Pad(); std::cout << "Plus\n"; indent_++; n->GetElement()->Accept(this); indent_--; }
    void Visit(const OptionalAst* n) override { Pad(); std::cout << "Optional\n"; indent_++; n->GetElement()->Accept(this); indent_--; }
    void Visit(const RepeatAst* n) override {
        Pad(); std::cout << "Repeat{" << n->GetMin() << ","
            << (n->GetMax()==-1 ? "" : std::to_string(n->GetMax())) << "}\n";
        indent_++; n->GetElement()->Accept(this); indent_--;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Parser — recursive descent, regex string → AST
// Extended from regex.cc with: [...], \p{}, \s, \r, \n, {m,n}, (?:...)
// ═══════════════════════════════════════════════════════════════════════════

class RegexParser {
    std::string pattern_;
    size_t pos_ = 0;

    bool AtEnd() { return pos_ >= pattern_.size(); }
    bool Match(char c) { if (!AtEnd() && pattern_[pos_] == c) { ++pos_; return true; } return false; }

    uint32_t NextChar() {
        size_t bytes;
        uint32_t c = DecodeUTF8At(pattern_, pos_, &bytes);
        if (bytes == 0) throw std::runtime_error("invalid UTF-8");
        pos_ += bytes;
        return c;
    }

    // Parse \p{Name} or \P{Name}
    CharPred ParseUnicodeProperty(bool negated) {
        if (!Match('{')) throw std::runtime_error("expected {");
        std::string name;
        while (!AtEnd() && pattern_[pos_] != '}') name += pattern_[pos_++];
        if (!Match('}')) throw std::runtime_error("expected }");

        CharPred pred;
        if (name == "A") pred = IsAlpha;
        else if (name == "H" || name == "Han") pred = IsHan;
        else if (name == "N") pred = IsDigit;
        else if (name == "L") pred = IsLetter;
        else throw std::runtime_error("unknown property: " + name);
        if (negated) return [pred](uint32_t c) { return !pred(c); };
        return pred;
    }

    // Parse \r, \n, \s, \d, \p{...}, \P{...}, or escaped literal
    std::pair<CharPred, std::string> ParseEscape() {
        if (AtEnd()) throw std::runtime_error("unexpected end after \\");
        char c = pattern_[pos_++];
        switch (c) {
            case 'r': return {[](uint32_t x){return x=='\r';}, "\\r"};
            case 'n': return {[](uint32_t x){return x=='\n';}, "\\n"};
            case 't': return {[](uint32_t x){return x=='\t';}, "\\t"};
            case 's': return {IsWhitespace, "\\s"};
            case 'S': return {[](uint32_t x){return !IsWhitespace(x);}, "\\S"};
            case 'd': return {IsDigit, "\\d"};
            case 'p': return {ParseUnicodeProperty(false), "\\p{...}"};
            case 'P': return {ParseUnicodeProperty(true), "\\P{...}"};
            default: {
                uint32_t cc = c;
                return {[cc](uint32_t x){return x==cc;}, std::string("\\")+(char)c};
            }
        }
    }

    // Parse [...] or [^...]
    std::unique_ptr<Ast> ParseCharClass() {
        bool negated = Match('^');
        std::string repr = negated ? "[^" : "[";
        CharPred pred = [](uint32_t) { return false; };

        while (!AtEnd() && pattern_[pos_] != ']') {
            CharPred cp; std::string cr;
            if (pattern_[pos_] == '\\') {
                ++pos_;
                auto [p, r] = ParseEscape();
                cp = std::move(p); cr = r;
            } else {
                uint32_t c = NextChar();
                cr = EncodeUTF8(c);
                if (!AtEnd() && pattern_[pos_] == '-' &&
                    pos_+1 < pattern_.size() && pattern_[pos_+1] != ']') {
                    ++pos_; uint32_t c2 = NextChar();
                    cr += "-" + EncodeUTF8(c2);
                    cp = [c,c2](uint32_t x) { return x>=c && x<=c2; };
                } else {
                    cp = [c](uint32_t x) { return x==c; };
                }
            }
            repr += cr;
            pred = [a=std::move(pred),b=std::move(cp)](uint32_t x) { return a(x)||b(x); };
        }
        if (!Match(']')) throw std::runtime_error("expected ]");
        repr += "]";
        if (negated) pred = [p=std::move(pred)](uint32_t x) { return !p(x); };
        return std::make_unique<CharClassAst>(std::move(pred), std::move(repr));
    }

    // Parse atom: literal | . | (...) | [...] | \escape
    std::unique_ptr<Ast> ParseAtom() {
        if (AtEnd()) throw std::runtime_error("unexpected end");

        // Group: (...) or (?:...)
        if (Match('(')) {
            if (!AtEnd() && pattern_[pos_] == '?' &&
                pos_+1 < pattern_.size() && pattern_[pos_+1] == ':') {
                pos_ += 2;
            }
            auto ast = ParseAlternation();
            if (!Match(')')) throw std::runtime_error("expected )");
            return ast;
        }

        if (Match('[')) return ParseCharClass();
        if (Match('.')) return std::make_unique<DotAst>();

        if (Match('\\')) {
            auto [pred, repr] = ParseEscape();
            return std::make_unique<CharClassAst>(std::move(pred), std::move(repr));
        }

        return std::make_unique<LiteralAst>(NextChar());
    }

    // Parse quantifier: atom followed by *, +, ?, {m,n}
    // Possessive quantifiers (*+, ++, ?+, {m,n}+) are parsed but treated
    // as greedy — DFA matching is inherently non-backtracking.
    std::unique_ptr<Ast> ParseQuantified() {
        auto atom = ParseAtom();
        if (AtEnd()) return atom;

        if (Match('*')) { Match('+'); return std::make_unique<StarAst>(std::move(atom)); }
        if (Match('+')) { Match('+'); return std::make_unique<PlusAst>(std::move(atom)); }
        if (Match('?')) { Match('+'); return std::make_unique<OptionalAst>(std::move(atom)); }

        if (Match('{')) {
            int lo = 0;
            while (!AtEnd() && pattern_[pos_]>='0' && pattern_[pos_]<='9')
                lo = lo*10 + (pattern_[pos_++]-'0');
            int hi = lo;
            if (Match(',')) {
                if (!AtEnd() && pattern_[pos_]>='0' && pattern_[pos_]<='9') {
                    hi = 0;
                    while (!AtEnd() && pattern_[pos_]>='0' && pattern_[pos_]<='9')
                        hi = hi*10 + (pattern_[pos_++]-'0');
                } else hi = -1;
            }
            if (!Match('}')) throw std::runtime_error("expected }");
            Match('+');
            return std::make_unique<RepeatAst>(std::move(atom), lo, hi);
        }

        return atom;
    }

    std::unique_ptr<Ast> ParseSequence() {
        auto seq = std::make_unique<SequenceAst>();
        while (!AtEnd() && pattern_[pos_] != '|' && pattern_[pos_] != ')') {
            seq->Add(ParseQuantified());
        }
        if (seq->GetElements().empty()) return std::make_unique<EmptyAst>();
        if (seq->GetElements().size() == 1)
            return const_cast<std::vector<std::unique_ptr<Ast>>&>(seq->GetElements())[0]->Clone();
        return seq;
    }

    std::unique_ptr<Ast> ParseAlternation() {
        auto first = ParseSequence();
        if (AtEnd() || pattern_[pos_] != '|') return first;
        auto alt = std::make_unique<AlternativeAst>();
        alt->Add(std::move(first));
        while (Match('|')) alt->Add(ParseSequence());
        return alt;
    }

public:
    explicit RegexParser(std::string p) : pattern_(std::move(p)) {}

    std::unique_ptr<Ast> Parse() {
        pos_ = 0;
        auto ast = ParseAlternation();
        if (pos_ < pattern_.size())
            throw std::runtime_error("unexpected at pos " + std::to_string(pos_));
        return ast;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// NFA — Thompson construction via Visitor
// Changed from regex.cc: edges now use CharPred instead of uint32_t
// ═══════════════════════════════════════════════════════════════════════════

struct NFAState {
    int id;
    bool accept = false;

    struct Edge {
        CharPred pred;
        NFAState* to;
    };
    std::vector<Edge> edges;
    std::vector<NFAState*> epsilons;

    static int next_id;
    NFAState() : id(next_id++) {}
};
int NFAState::next_id = 0;

struct NFAFrag {
    NFAState* start;
    NFAState* end;
};

class NFABuilder : public AstVisitor {
    std::vector<std::unique_ptr<NFAState>> alloc_;
    std::stack<NFAFrag> stack_;

    NFAState* NewState() {
        auto s = std::make_unique<NFAState>();
        auto* p = s.get();
        alloc_.push_back(std::move(s));
        return p;
    }

    void PushPred(CharPred pred) {
        auto *s = NewState(), *e = NewState();
        e->accept = true;
        s->edges.push_back({std::move(pred), e});
        stack_.push({s, e});
    }

public:
    void Visit(const EmptyAst*) override {
        auto *s = NewState(), *e = NewState();
        e->accept = true;
        s->epsilons.push_back(e);
        stack_.push({s, e});
    }

    void Visit(const LiteralAst* n) override {
        uint32_t p = n->GetPoint();
        PushPred([p](uint32_t c) { return c == p; });
    }

    void Visit(const DotAst*) override {
        PushPred([](uint32_t c) { return c != '\n' && c != '\r'; });
    }

    void Visit(const CharClassAst* n) override {
        PushPred(n->GetPred());
    }

    void Visit(const SequenceAst* n) override {
        auto& elems = n->GetElements();
        if (elems.empty()) { Visit((const EmptyAst*)nullptr); return; }
        elems[0]->Accept(this);
        for (size_t i = 1; i < elems.size(); i++) {
            auto u = stack_.top(); stack_.pop();
            elems[i]->Accept(this);
            auto v = stack_.top(); stack_.pop();
            u.end->accept = false;
            u.end->epsilons.push_back(v.start);
            stack_.push({u.start, v.end});
        }
    }

    void Visit(const AlternativeAst* n) override {
        auto& branches = n->GetBranches();
        if (branches.empty()) { Visit((const EmptyAst*)nullptr); return; }
        branches[0]->Accept(this);
        for (size_t i = 1; i < branches.size(); i++) {
            auto u = stack_.top(); stack_.pop();
            branches[i]->Accept(this);
            auto v = stack_.top(); stack_.pop();
            auto *s = NewState(), *e = NewState();
            e->accept = true;
            s->epsilons.push_back(u.start);
            s->epsilons.push_back(v.start);
            u.end->accept = v.end->accept = false;
            u.end->epsilons.push_back(e);
            v.end->epsilons.push_back(e);
            stack_.push({s, e});
        }
    }

    void Visit(const StarAst* n) override {
        n->GetElement()->Accept(this);
        auto inner = stack_.top(); stack_.pop();
        auto *s = NewState(), *e = NewState();
        e->accept = true; inner.end->accept = false;
        s->epsilons.push_back(inner.start);
        s->epsilons.push_back(e);
        inner.end->epsilons.push_back(inner.start);
        inner.end->epsilons.push_back(e);
        stack_.push({s, e});
    }

    void Visit(const PlusAst* n) override {
        n->GetElement()->Accept(this);
        auto inner = stack_.top(); stack_.pop();
        auto *s = NewState(), *e = NewState();
        e->accept = true; inner.end->accept = false;
        s->epsilons.push_back(inner.start);
        inner.end->epsilons.push_back(inner.start);
        inner.end->epsilons.push_back(e);
        stack_.push({s, e});
    }

    void Visit(const OptionalAst* n) override {
        n->GetElement()->Accept(this);
        auto inner = stack_.top(); stack_.pop();
        auto *s = NewState(), *e = NewState();
        e->accept = true; inner.end->accept = false;
        s->epsilons.push_back(inner.start);
        s->epsilons.push_back(e);
        inner.end->epsilons.push_back(e);
        stack_.push({s, e});
    }

    // NEW: {min,max} — expand to min required + optional/star tail
    void Visit(const RepeatAst* n) override {
        int lo = n->GetMin(), hi = n->GetMax();
        NFAFrag result = {nullptr, nullptr};

        auto append = [&](NFAFrag f) {
            if (result.start == nullptr) result = f;
            else {
                result.end->accept = false;
                result.end->epsilons.push_back(f.start);
                result.end = f.end;
            }
        };

        // lo required copies
        for (int i = 0; i < lo; i++) {
            n->GetElement()->Accept(this);
            append(stack_.top()); stack_.pop();
        }

        if (hi == -1) {
            // {lo,} → lo copies + star
            n->GetElement()->Accept(this);
            auto inner = stack_.top(); stack_.pop();
            auto *s = NewState(), *e = NewState();
            e->accept = true; inner.end->accept = false;
            s->epsilons.push_back(inner.start);
            s->epsilons.push_back(e);
            inner.end->epsilons.push_back(inner.start);
            inner.end->epsilons.push_back(e);
            append({s, e});
        } else {
            // {lo,hi} → lo copies + (hi-lo) optional copies
            for (int i = lo; i < hi; i++) {
                n->GetElement()->Accept(this);
                auto inner = stack_.top(); stack_.pop();
                auto *s = NewState(), *e = NewState();
                e->accept = true; inner.end->accept = false;
                s->epsilons.push_back(inner.start);
                s->epsilons.push_back(e);
                inner.end->epsilons.push_back(e);
                append({s, e});
            }
        }

        if (result.start == nullptr) {
            auto *s = NewState(); s->accept = true; result = {s, s};
        }
        stack_.push(result);
    }

    NFAFrag GetResult() { return stack_.top(); }
    int NumStates() const { return alloc_.size(); }

    void PrintNFA(NFAFrag frag) {
        std::cout << "\n=== NFA: " << alloc_.size() << " states ===\n";
        std::set<NFAState*> visited;
        std::queue<NFAState*> queue;
        queue.push(frag.start);
        visited.insert(frag.start);
        while (!queue.empty()) {
            auto* st = queue.front(); queue.pop();
            std::cout << "State " << st->id;
            if (st == frag.start) std::cout << " (START)";
            if (st->accept) std::cout << " (END)";
            std::cout << ": " << st->edges.size() << " edges, "
                      << st->epsilons.size() << " eps\n";
            for (auto& e : st->edges)
                if (visited.insert(e.to).second) queue.push(e.to);
            for (auto* e : st->epsilons)
                if (visited.insert(e).second) queue.push(e);
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// DFA — Lazy subset construction with equivalence classes
// Changed from regex.cc: uses on-demand construction + CharPred equiv classes
// ═══════════════════════════════════════════════════════════════════════════

class LazyDFA {
    NFAFrag nfa_;
    using StateSet = std::set<NFAState*>;

    struct DState {
        bool accept;
        std::map<int, int> trans;  // equiv_class_id -> DFA state index
    };
    std::vector<DState> states_;
    std::map<StateSet, int> set_to_id_;

    // Equivalence classes: group codepoints by their predicate signature
    std::vector<CharPred*> all_preds_;
    std::map<std::vector<bool>, int> sig_map_;
    std::map<uint32_t, int> cp_cache_;
    int next_cls_ = 0;

    void EpsClosure(StateSet& s) {
        std::vector<NFAState*> stk(s.begin(), s.end());
        while (!stk.empty()) {
            auto* st = stk.back(); stk.pop_back();
            for (auto* e : st->epsilons)
                if (s.insert(e).second) stk.push_back(e);
        }
    }

    int GetOrCreate(StateSet& ss) {
        EpsClosure(ss);
        auto it = set_to_id_.find(ss);
        if (it != set_to_id_.end()) return it->second;
        int id = states_.size();
        bool acc = false;
        for (auto* s : ss) if (s->accept) { acc = true; break; }
        states_.push_back({acc, {}});
        set_to_id_[ss] = id;
        return id;
    }

    int ClassifyCP(uint32_t cp) {
        auto it = cp_cache_.find(cp);
        if (it != cp_cache_.end()) return it->second;
        std::vector<bool> sig;
        sig.reserve(all_preds_.size());
        for (auto* p : all_preds_) sig.push_back((*p)(cp));
        auto sit = sig_map_.find(sig);
        int cls;
        if (sit != sig_map_.end()) cls = sit->second;
        else { cls = next_cls_++; sig_map_[sig] = cls; }
        cp_cache_[cp] = cls;
        return cls;
    }

    int Step(int dfa_st, uint32_t cp) {
        int cls = ClassifyCP(cp);
        auto it = states_[dfa_st].trans.find(cls);
        if (it != states_[dfa_st].trans.end()) return it->second;

        // Find the NFA state set for this DFA state
        StateSet* cur = nullptr;
        for (auto& [ss, id] : set_to_id_)
            if (id == dfa_st) { cur = const_cast<StateSet*>(&ss); break; }

        StateSet next;
        for (auto* s : *cur)
            for (auto& e : s->edges)
                if (e.pred(cp)) next.insert(e.to);

        if (next.empty()) {
            states_[dfa_st].trans[cls] = -1;
            return -1;
        }
        int next_id = GetOrCreate(next);
        states_[dfa_st].trans[cls] = next_id;
        return next_id;
    }

public:
    void Build(NFAFrag nfa) {
        nfa_ = nfa;
        // Collect all predicates from reachable NFA states
        std::set<NFAState*> visited;
        std::queue<NFAState*> queue;
        queue.push(nfa.start);
        visited.insert(nfa.start);
        while (!queue.empty()) {
            auto* st = queue.front(); queue.pop();
            for (auto& e : st->edges) {
                all_preds_.push_back(&e.pred);
                if (visited.insert(e.to).second) queue.push(e.to);
            }
            for (auto* e : st->epsilons)
                if (visited.insert(e).second) queue.push(e);
        }

        StateSet start = {nfa.start};
        GetOrCreate(start);
    }

    // Full-string match (like regex.cc's Match)
    bool Match(const std::string& text) {
        int cur = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t bytes;
            uint32_t cp = DecodeUTF8At(text, pos, &bytes);
            if (bytes == 0) return false;
            int next = Step(cur, cp);
            if (next < 0) return false;
            cur = next;
            pos += bytes;
        }
        return states_[cur].accept;
    }

    // Find longest match starting at data[0..len).
    int MatchAt(const char* data, size_t len) {
        int cur = 0;
        int last = states_[0].accept ? 0 : -1;
        size_t pos = 0;
        while (pos < len) {
            size_t bytes;
            uint32_t cp = DecodeUTF8At(std::string(data+pos, len-pos), 0, &bytes);
            if (bytes == 0) break;
            int next = Step(cur, cp);
            if (next < 0) break;
            cur = next;
            pos += bytes;
            if (states_[cur].accept) last = pos;
        }
        return last;
    }

    // Find all non-overlapping matches (leftmost-longest).
    std::vector<std::string_view> FindAll(std::string_view text) {
        std::vector<std::string_view> result;
        const char* data = text.data();
        size_t len = text.size(), pos = 0;
        while (pos < len) {
            int n = MatchAt(data+pos, len-pos);
            if (n > 0) { result.emplace_back(data+pos, n); pos += n; }
            else pos += BytesOneUTF8(data+pos);
        }
        return result;
    }

    void PrintStats() {
        std::cout << "DFA: " << states_.size() << " states, "
                  << next_cls_ << " equiv classes\n";
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Regex — top-level API (same interface as regex.cc)
// ═══════════════════════════════════════════════════════════════════════════

class Regex {
    std::unique_ptr<Ast> ast_;
    NFABuilder nfa_builder_;
    LazyDFA dfa_;

public:
    explicit Regex(const std::string& pattern) {
        RegexParser parser(pattern);
        ast_ = parser.Parse();
        ast_->Accept(&nfa_builder_);
        auto nfa = nfa_builder_.GetResult();
        dfa_.Build(nfa);
    }

    void PrintAst() { AstPrinter p; ast_->Accept(&p); }
    void PrintNFA() { nfa_builder_.PrintNFA(nfa_builder_.GetResult()); }
    void PrintStats() { dfa_.PrintStats(); }

    bool Match(const std::string& text) { return dfa_.Match(text); }
    std::vector<std::string_view> FindAll(std::string_view text) { return dfa_.FindAll(text); }
};

} // namespace regex

// ═══════════════════════════════════════════════════════════════════════════
// main — tests from regex.cc + new features + GPT-4 tokenizer pattern
// ═══════════════════════════════════════════════════════════════════════════

void test_match(const std::string& pattern, const std::vector<std::pair<std::string,bool>>& cases) {
    regex::Regex r(pattern);
    std::cout << "Pattern: " << pattern << "\n";
    for (auto& [text, expected] : cases) {
        bool result = r.Match(text);
        std::cout << "  \"" << text << "\" -> " << (result ? "Match" : "No")
                  << (result == expected ? "" : " FAIL!") << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
    // --- Benchmark mode: ./new_regex <file> [max_lines] ---
    if (argc >= 2) {
        int max_lines = argc > 2 ? std::atoi(argv[2]) : 0;
        std::vector<std::string> lines;
        { std::ifstream fin(argv[1]); std::string l;
          while (std::getline(fin, l)) {
              if (!l.empty()) lines.push_back(l);
              if (max_lines > 0 && (int)lines.size() >= max_lines) break;
          }
        }
        std::cerr << "Loaded " << lines.size() << " lines\n";

        const char* pat =
            "'([sdmt]|ll|ve|re)"
            "|[^\\r\\n\\p{A}\\p{H}\\p{N}]?\\p{A}+"
            "|\\p{H}+"
            "|\\p{N}+"
            "| ?[^\\s\\p{A}\\p{H}\\p{N}]+[\\r\\n]*"
            "|\\s*[\\r\\n]"
            "|\\s";

        auto t0 = std::chrono::high_resolution_clock::now();
        regex::Regex tok(pat);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cerr << "Compile: "
            << std::chrono::duration<double,std::milli>(t1-t0).count() << " ms\n";
        tok.PrintStats();

        int64_t total_tokens = 0, total_bytes = 0;
        for (auto& l : lines) total_bytes += l.size();

        auto t2 = std::chrono::high_resolution_clock::now();
        for (auto& l : lines) total_tokens += tok.FindAll(l).size();
        auto t3 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double,std::milli>(t3-t2).count();

        std::cerr << "Lines:  " << lines.size() << "\n"
                  << "Bytes:  " << total_bytes << "\n"
                  << "Tokens: " << total_tokens << "\n"
                  << "Time:   " << ms << " ms\n"
                  << "Speed:  " << (total_bytes/1024.0/1024.0)/(ms/1000.0) << " MB/s\n";
        return 0;
    }

    // --- Original regex.cc tests ---
    std::cout << "=== Basic Tests (from regex.cc) ===\n\n";
    test_match("a", {{"a",true},{"b",false},{"",false}});
    test_match("ab", {{"ab",true},{"a",false},{"b",false}});
    test_match("\xe4\xbd\xa0\xe5\xa5\xbd", {{"\xe4\xbd\xa0\xe5\xa5\xbd",true},{"\xe4\xbd\xa0",false}});
    test_match("a|b", {{"a",true},{"b",true},{"c",false}});
    test_match("a*", {{"",true},{"a",true},{"aaa",true},{"b",false}});
    test_match("a+", {{"",false},{"a",true},{"aaa",true}});
    test_match("a?", {{"",true},{"a",true},{"aa",false}});
    test_match("(a|b)*", {{"",true},{"ab",true},{"ba",true},{"abba",true}});

    // --- New features ---
    std::cout << "=== New Feature Tests ===\n\n";
    test_match("[abc]+", {{"abcba",true},{"xyz",false}});
    test_match("[^abc]+", {{"xyz",true},{"abc",false}});
    test_match("[a-z]+", {{"hello",true},{"Hello",false}});
    test_match("\\p{N}+", {{"123",true},{"abc",false}});
    test_match("\\p{H}+", {{"\xe4\xbd\xa0\xe5\xa5\xbd",true},{"abc",false}});
    test_match("\\s+", {{" \t",true},{"abc",false}});
    test_match("a{2,4}", {{"a",false},{"aa",true},{"aaa",true},{"aaaa",true},{"aaaaa",false}});
    test_match("a{3}", {{"aa",false},{"aaa",true},{"aaaa",false}});
    test_match("a{2,}", {{"a",false},{"aa",true},{"aaaaaa",true}});

    // --- GPT-4 tokenizer ---
    std::cout << "=== GPT-4 Tokenizer ===\n\n";

    const char* pattern =
        "[^\\r\\n\\p{A}\\p{H}\\p{N}]?\\p{A}+"
        "|\\p{H}+"
        "|\\p{N}+"
        "| ?[^\\s\\p{A}\\p{H}\\p{N}]+[\\r\\n]*"
        "|\\s*[\\r\\n]"
        "|\\s";

    regex::Regex tokenizer(pattern);
    tokenizer.PrintStats();
    std::cout << "\n";

    struct TC { const char* in; };
    TC cases[] = {
        {"Hello, World!"},
        {"don't"},
        {"they'll"},
        {"I've"},
        {"she's"},
        {"$100"},
        {"100%"},
        {"24h"},
        {"hello123world"},
        {"hello,world"},
        {"hello,,world"},
        {"hello, world"},
        {" $100"},
        {" 24h"},
        {"$hello"},
        {"a1b2c3"},
        {" hello"},
        {"  hello"},
        {"   hello"},
        {"    hello"},
        {"hello  world"},
        {"hello   world"},
        {"  hello  world  "},
        {"#hashtag"},
        {"C++"},
        {"hello---world"},
        {"3.14"},
        {"U.S.A."},
        {"hello!world"},
        {"(test)"},
        {"hello'world"},
        {"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"},
        {" \xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"},
        {"hello\xe4\xbd\xa0\xe5\xa5\xbd""world"},
        {"\xe4\xbd\xa0\xe5\xa5\xbd\xef\xbc\x8c\xe4\xb8\x96\xe7\x95\x8c\xef\xbc\x81"},
        {"Hello, \xe4\xbd\xa0\xe5\xa5\xbd! 123abc"},
    };

    for (auto& tc : cases) {
        auto tokens = tokenizer.FindAll(tc.in);
        std::cout << "'" << tc.in << "'\n  -> [";
        for (size_t i = 0; i < tokens.size(); i++) {
            if (i) std::cout << ", ";
            std::cout << "'" << tokens[i] << "'";
        }
        std::cout << "]\n";
    }

    return 0;
}
