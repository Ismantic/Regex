#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <queue>
#include <stack>
#include <algorithm>
#include <stdexcept>

#include <stdint.h>

namespace regex {

size_t BytesOneUTF8(const char* src) {
    return "\1\1\1\1\1\1\1\1\1\1\1\1\2\2\3\4"[(*src & 0xFF) >> 4];
}

uint32_t DecodeUTF8At(const std::string& str, size_t pos, size_t* bytes) {
    if (pos >= str.size()) {
        *bytes = 0;
        return 0;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(str.data() + pos);
    *bytes = BytesOneUTF8(str.data() + pos);

    if (pos + *bytes > str.size()) {
        *bytes = 0;
        return 0;
    }

    switch (*bytes) {
        case 1:
            return data[0];
        case 2:
            if ((data[1] & 0xC0) != 0x80) { 
                *bytes = 0; 
                return 0; 
            }
            return ((data[0] & 0x1F) << 6) | 
                   (data[1] & 0x3F);
        case 3:
            if ((data[1] & 0xC0) != 0x80 || 
                (data[2] & 0xC0) != 0x80) { 
                *bytes = 0; 
                return 0; 
            }
            return ((data[0] & 0x0F) << 12) | 
                   ((data[1] & 0x3F) << 6) | 
                   (data[2] & 0x3F);
        case 4:
            if ((data[1] & 0xC0) != 0x80 || 
                (data[2] & 0xC0) != 0x80 || 
                (data[3] & 0xC0) != 0x80) { 
                *bytes = 0; 
                return 0; 
            }
            return ((data[0] & 0x07) << 18) | 
                   ((data[1] & 0x3F) << 12) | 
                   ((data[2] & 0x3F) << 6) | 
                   (data[3] & 0x3F); 
        default:
            *bytes = 0;
            return 0;
    }
}

std::string EncodeUTF8(uint32_t c) {
    if (c <= 0x7F) {
        return std::string(1, static_cast<char>(c));
    } else if (c <= 0x7FF) {
        std::string result(2, 0);
        result[0] = static_cast<char>(0xC0 | (c >> 6));
        result[1] = static_cast<char>(0x80 | (c & 0x3F));
        return result;
    } else if (c <= 0xFFFF) {
        std::string result(3, 0);
        result[0] = static_cast<char>(0xE0 | (c >> 12));
        result[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        result[2] = static_cast<char>(0x80 | (c & 0x3F));
        return result;
    } else if (c <= 0x10FFFF) {
        std::string result(4, 0);
        result[0] = static_cast<char>(0xF0 | (c >> 18));
        result[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        result[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        result[3] = static_cast<char>(0x80 | (c & 0x3F));
        return result;
    }
    return "?";
}

class EmptyAst;
class LiteralAst;
class DotAst;
class SequenceAst;
class AlternativeAst;
class StarAst;
class PlusAst;
class OptionalAst;

class AstVisitor {
public:
    virtual ~AstVisitor() = default;
    virtual void Visit(const EmptyAst* node) = 0;
    virtual void Visit(const LiteralAst* node) = 0;
    virtual void Visit(const DotAst* node) = 0;
    virtual void Visit(const SequenceAst* node) = 0;
    virtual void Visit(const AlternativeAst* node) = 0;
    virtual void Visit(const StarAst* node) = 0;
    virtual void Visit(const PlusAst* node) = 0;
    virtual void Visit(const OptionalAst* node) = 0;
};

enum class AstType {
    Empty, Literal, Dot, Sequence, Alternative, Star, Plus, Optional
};

class Ast {
public:
    virtual ~Ast() = default;
    virtual AstType GetType() const = 0;
    virtual void Accept(AstVisitor* visitor) const = 0;
};

class EmptyAst : public Ast {
public:
    AstType GetType() const override { return AstType::Empty; }

    void Accept(AstVisitor* v) const override { v->Visit(this); }
};

class LiteralAst : public Ast {
private:
    uint32_t point;

public:
    explicit LiteralAst(uint32_t p) : point(p) {}

    AstType GetType() const override { return AstType::Literal; }

    uint32_t GetPoint() const { return point; }

    void Accept(AstVisitor* v) const override { v->Visit(this); }
};

class DotAst : public Ast {
public:
    AstType GetType() const override { return AstType::Dot; }

    void Accept(AstVisitor* v) const override { v->Visit(this); }
};

class SequenceAst : public Ast {
private:
    std::vector<std::unique_ptr<Ast>> elements;

public:
    void InsertElement(std::unique_ptr<Ast> e) {
        elements.push_back(std::move(e));
    }

    const std::vector<std::unique_ptr<Ast>>& GetElements() const {
        return elements;
    }

    AstType GetType() const override {
        return AstType::Sequence;
    }

    void Accept(AstVisitor* v) const override {
        v->Visit(this);
    }
};

class AlternativeAst : public Ast {
private:
    std::vector<std::unique_ptr<Ast>> brances;

public:
    void InsertBranch(std::unique_ptr<Ast> branch) {
        brances.push_back(std::move(branch));
    }

    const std::vector<std::unique_ptr<Ast>>& GetBranches() const {
        return brances;
    }

    AstType GetType() const override {
        return AstType::Alternative;
    }

    void Accept(AstVisitor* v) const override {
        v->Visit(this);
    }
};

class StarAst : public Ast {
private:
    std::unique_ptr<Ast> element;

public:
    explicit StarAst(std::unique_ptr<Ast> e) : element(std::move(e)) {}

    const Ast* GetElement() const { return element.get(); }

    AstType GetType() const override { return AstType::Star; }

    void Accept(AstVisitor* v) const override {
        v->Visit(this);
    }
};

class PlusAst : public Ast {
private:
    std::unique_ptr<Ast> element;

public:
    explicit PlusAst(std::unique_ptr<Ast> e) : element(std::move(e)) {}

    const Ast* GetElement() const { return element.get(); }

    AstType GetType() const override { return AstType::Plus; }

    void Accept(AstVisitor* v) const override {
        v->Visit(this);
    }
};

class OptionalAst : public Ast {
private:
    std::unique_ptr<Ast> element;

public:
    explicit OptionalAst(std::unique_ptr<Ast> e) : element(std::move(e)) {}

    const Ast* GetElement() const { return element.get(); }

    AstType GetType() const override { return AstType::Optional; }

    void Accept(AstVisitor* v) const override {
        v->Visit(this);
    }
};

class RegexParser {
private:
    std::string pattern;
    size_t pos = 0;
    int cnt = 0;

public:
    explicit RegexParser(std::string p) : pattern(std::move(p)) {}

    std::unique_ptr<Ast> Parse() {
        pos = 0;
        cnt = 0;
        auto ast = ParsePattern();

        if (pos < pattern.length()) {
            throw std::runtime_error("Parse Error " + std::to_string(pos));
        }

        return ast;
    };

private:
    void PrintEnter(const std::string& function, const std::string& e) {
        std::cout << std::string(cnt*2, ' ') << "Enter " << function 
                  << "(), Pos=" << pos << ", Expect=" << e;
        if (pos < pattern.size()) {
            size_t bytes;
            uint32_t c = DecodeUTF8At(pattern, pos, &bytes);
            if (bytes > 0) {
                std::string current_char = pattern.substr(pos, bytes);
                std::cout << ", Current=" << current_char 
                          << "'(U+" << std::hex << c 
                          << std::dec << ")";
            }
        }
        std::cout << "\n";
        cnt++;
    }

    void PrintExit(const std::string& function, const std::string& r) {
        cnt--;
        std::cout << std::string(cnt*2, ' ') << "Exit " << function
                  << "(), Result=" << r << ", Pos=" << pos << "\n";
    }

    std::unique_ptr<Ast> ParsePattern() {
        PrintEnter("ParsePattern", "<sequence> ('|' <sequence>)*");

        if (pos >= pattern.length()) {
            PrintExit("ParsePattern", "Empty");
            return std::make_unique<EmptyAst>();
        }

        auto n = std::make_unique<AlternativeAst>();
        n->InsertBranch(ParseSequence());

        while (pos < pattern.length() && pattern[pos] == '|') {
            std::cout << std::string(cnt*2, ' ') << "Got '|', Parse next branch\n";
            ++pos;
            n->InsertBranch(ParseSequence());
        }

        PrintExit("ParsePattern", "AlternativeNode");
        return n;
    }

    std::unique_ptr<Ast> ParseSequence() {
        PrintEnter("ParseSequence", "<element>*");

        auto s = std::make_unique<SequenceAst>();

        while (pos < pattern.length() && pattern[pos] != '|' && pattern[pos] != ')') {
            std::cout << std::string(cnt*2, ' ') << "Parse next element\n";
            s->InsertElement(ParseElement());
        }

        PrintExit("ParseSequence", "SequenceNode");
        return s;
    }

    std::unique_ptr<Ast> ParseElement() {
        PrintEnter("ParseElement", "<atom> <quantifier>?");

        if (pos >= pattern.length()) {
            PrintExit("ParseElement", "Empty");
            return std::make_unique<EmptyAst>();
        }

        auto atom = ParseAtom();

        if (pos < pattern.length()) {
            char quantifier = pattern[pos];
            switch (quantifier) {
                case '*':
                    std::cout << std::string(cnt * 2, ' ') << "Got Quantifier '*'\n";
                    ++pos;
                    PrintExit("ParseElement", "StarNode");
                    return std::make_unique<StarAst>(std::move(atom));
                case '+':
                    std::cout << std::string(cnt * 2, ' ') << "Got Quantifier '+'\n";
                    ++pos;
                    PrintExit("ParseElement", "PlusNode");
                    return std::make_unique<PlusAst>(std::move(atom));
                case '?':
                    std::cout << std::string(cnt * 2, ' ') << "Got Quantifier '?'\n";
                    ++pos;
                    PrintExit("ParseElement", "OptionalNode");
                    return std::make_unique<OptionalAst>(std::move(atom));
            }
        }

        PrintExit("ParseElement", "AtomNode");
        return atom;
    }

    std::unique_ptr<Ast> ParseAtom() {
        PrintEnter("ParseAtom", "<literal> | '.' | '(' <pattern> ')'");

        if (pos >= pattern.length()) {
            throw std::runtime_error("ParseAtom: Unexcept EOF");
        }

        char c = pattern[pos];
        std::unique_ptr<Ast> element;

        if (c == '(') {
            std::cout << std::string(cnt*2, ' ') << "Got '(', Parse down pattern\n";
            ++pos;
            element = ParsePattern();

            if (pos >= pattern.length() || pattern[pos] != ')') {
                throw std::runtime_error("ParseAtom: No Match ')'");
            }
            std::cout << std::string(cnt*2, ' ') << "Got ')', Parse down pattern done\n";
            ++pos;
            PrintExit("ParseAtom", "() Expression");
            return element;
        } else if (c == '.') {
            std::cout << std::string(cnt*2, ' ') << "Got '.'\n";
            element = std::make_unique<DotAst>();
            ++pos;
            PrintExit("ParseAtom", "DotNode");
            return element;
        } else if (c != '|' && c != ')' && c != '*' && c != '+' && c != '?') {
            size_t bytes;
            uint32_t c = DecodeUTF8At(pattern, pos, &bytes);

            if (bytes > 0) {
                std::string char_str = pattern.substr(pos, bytes);
                std::cout << std::string(cnt*2, ' ') << "Got UTF-8 '" << char_str 
                          << "' (U+" << std::hex << c << std::dec << ")\n";
                element = std::make_unique<LiteralAst>(c);
                pos += bytes;
                PrintExit("ParseAtom", "LiternalNode");
                return element;
            } else {
                throw std::runtime_error("ParseAtom: Unvalid UTF-8 " + std::to_string(pos));
            }
        } else {
            throw std::runtime_error("ParseAtom: Unexcept '" + std::string(1, c) + "' at pos " + std::to_string(pos));
        }
    }
};

class RegexPrinter : public AstVisitor {
private:
    int cnt = 0;

    void PrintIndent() {
        for (int i = 0; i < cnt; ++i) {
            std::cout << " ";
        }
    }

public:
    void Visit(const EmptyAst* node) override {
        PrintIndent();
        std::cout << "Empty\n";
    }

    void Visit(const LiteralAst* node) override {
        PrintIndent();
        uint32_t p = node->GetPoint();
        std::string c = EncodeUTF8(p);
        std::cout << "Literal('" << c << "', U+" 
                  << std::hex << p << std::dec << ")\n";
    }

    void Visit(const DotAst* node) override {
        PrintIndent();
        std::cout << "Dot(.)\n";
    }

    void Visit(const SequenceAst* node) override {
        PrintIndent();
        std::cout << "Sequence\n";
        cnt++;
        for (const auto& e : node->GetElements()) {
            e->Accept(this);
        }
        cnt--;
    }


    void Visit(const AlternativeAst* node) override {
        PrintIndent();
        std::cout << "Alternative\n";
        cnt++;
        for (const auto& branch : node->GetBranches()) {
            branch->Accept(this);
        }
        cnt--;
    }


    void Visit(const StarAst* node) override {
        PrintIndent();
        std::cout << "Star\n";
        cnt++;
        node->GetElement()->Accept(this);
        cnt--;
    }
    
    void Visit(const PlusAst* node) override {
        PrintIndent();
        std::cout << "Plus\n";
        cnt++;
        node->GetElement()->Accept(this);
        cnt--;
    }
    
    void Visit(const OptionalAst* node) override {
        PrintIndent();
        std::cout << "Optional\n";
        cnt++;
        node->GetElement()->Accept(this);
        cnt--;
    }
};

class NFAState {
public:
    int i;
    bool end = false;
    std::map<uint32_t, std::vector<NFAState*>> transitions;
    std::vector<NFAState*> e_transitions;

    static constexpr uint32_t DOT_CHAR = 0xFFFFFFFF;

    static int next;

    NFAState() : i(next++) {};

    void InsertTransition(uint32_t p, NFAState* t) {
        transitions[p].push_back(t);
    }

    void InsertEpsilonTransition(NFAState* t) {
        e_transitions.push_back(t);
    }

    void InsertDotTransition(NFAState* t) {
        transitions[DOT_CHAR].push_back(t);
    }
};

int NFAState::next = 0;

struct NFA {
    NFAState* start;
    NFAState* end;

    NFA(NFAState* s, NFAState* e) : start(s), end(e) {}
};

class NFABuilder : public AstVisitor {
private:
    std::vector<std::unique_ptr<NFAState>> states;
    std::stack<NFA> stack;

    NFAState* NewState() {
        auto state = std::make_unique<NFAState>();
        NFAState* p = state.get();
        states.push_back(std::move(state));
        return p;
    }

public:
    void Visit(const EmptyAst* node) override {
        std::cout << "Construct Empty NFA\n";
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        start->InsertEpsilonTransition(end);
        stack.push(NFA(start, end));
    }

    void Visit(const LiteralAst* node) override {
        uint32_t p = node->GetPoint();
        std::cout << "Construct Literal(U+" << std::hex << p
                  << std::dec << ") NFA\n";
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        start->InsertTransition(p, end);
        stack.push(NFA(start, end));
    }

    void Visit(const DotAst* node) override {
        std::cout << "Construct Dot NFA\n";
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        start->InsertDotTransition(end);
        stack.push(NFA(start, end));
    }

    void Visit(const SequenceAst* node) override {
        std::cout << "Construct Sequence NFA\n";

        const auto& elements = node->GetElements();
        if (elements.empty()) {
            Visit(static_cast<const EmptyAst*>(nullptr));
            return;
        }

        elements[0]->Accept(this);

        for (size_t i = 1; i < elements.size(); ++i) {
            auto u = stack.top(); stack.pop();
            elements[i]->Accept(this);
            auto v = stack.top(); stack.pop();

            u.end->end = false;
            u.end->InsertEpsilonTransition(v.start);
            stack.push(NFA(u.start, v.end));
        }
    }

    void Visit(const AlternativeAst* node) override {
        std::cout << "Construct Alternative NFA\n";

        const auto& branches = node->GetBranches();
        if (branches.empty()) {
            Visit(static_cast<const EmptyAst*>(nullptr));
            return;
        }

        branches[0]->Accept(this);

        for (size_t i = 1; i < branches.size(); ++i) {
            auto u = stack.top(); stack.pop();
            branches[i]->Accept(this);
            auto v = stack.top(); stack.pop();
            
            auto start = NewState();
            auto end = NewState();
            end->end = true;
            
            start->InsertEpsilonTransition(u.start);
            start->InsertEpsilonTransition(v.start);
            
            u.end->end = false;
            v.end->end = false;
            u.end->InsertEpsilonTransition(end);
            v.end->InsertEpsilonTransition(end);
            
            stack.push(NFA(start, end));
        }
    }

    void Visit(const StarAst* node) override {
        std::cout << "Construct Star NFA\n";
        
        node->GetElement()->Accept(this);
        auto inner = stack.top(); stack.pop();
        
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        
        start->InsertEpsilonTransition(inner.start);
        start->InsertEpsilonTransition(end);
        
        inner.end->end = false;
        inner.end->InsertEpsilonTransition(inner.start);
        inner.end->InsertEpsilonTransition(end);
        
        stack.push(NFA(start, end));
    }

    void Visit(const PlusAst* node) override {
        std::cout << "Construct Plus NFA\n";
        
        node->GetElement()->Accept(this);
        auto inner = stack.top(); stack.pop();
        
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        
        start->InsertEpsilonTransition(inner.start);
        
        inner.end->end = false;
        inner.end->InsertEpsilonTransition(inner.start);
        inner.end->InsertEpsilonTransition(end);
        
        stack.push(NFA(start, end));
    }

    void Visit(const OptionalAst* node) override {
        std::cout << "Construct Optional NFA\n";
        
        node->GetElement()->Accept(this);
        auto inner = stack.top(); stack.pop();
        
        auto start = NewState();
        auto end = NewState();
        end->end = true;
        
        start->InsertEpsilonTransition(inner.start);
        start->InsertEpsilonTransition(end);
        
        inner.end->end = false;
        inner.end->InsertEpsilonTransition(end);
        
        stack.push(NFA(start, end));
    }

    NFA GetNFA() {
        if (stack.empty()) {
            throw std::runtime_error("NFA Construct Error: Stack Empty");
        }

        auto r = stack.top(); stack.pop();
        return r;
    }

    void PrintNFA(const NFA& n) {
        std::cout << "\n=== NFA Struct ===\n";
        std::set<NFAState*> vs;
        std::queue<NFAState*> queue;

        queue.push(n.start);
        vs.insert(n.start);

        while (!queue.empty()) {
            auto state = queue.front();
            queue.pop();

            std::cout << "State " << state->i;
            if (state == n.start) std::cout << " (START)";
            if (state->end) std::cout << " (END)";
            std::cout << ":\n";

            for (const auto& [p, st] : state->transitions) {
                for (auto t : st) {
                    if (p == NFAState::DOT_CHAR) {
                        std::cout << "  --[.]-->";
                    } else if (p <= 127 && p >= 32) {
                        std::cout << "  --'" << static_cast<char>(p) << "'-->";
                    } else {
                        std::cout << "  --U+" << std::hex << p << std::dec << "-->";
                    }
                    std::cout << " State " << t->i << "\n";

                    if (vs.find(t) == vs.end()) {
                        vs.insert(t);
                        queue.push(t);
                    }
                }
            }

            for (auto t : state->e_transitions) {
                std::cout << "  --ε--> State" << t->i << "\n";
                if (vs.find(t) == vs.end()) {
                    vs.insert(t);
                    queue.push(t);
                }
            }
        }
    }
};

class DFAState {
public:
    int i;
    bool end = false;
    std::map<uint32_t, DFAState*> transitions;

    static int next;

    DFAState() : i(next++) {}
};

int DFAState::next = 0;

class DFABuilder {
private:
    std::vector<std::unique_ptr<DFAState>> states;
public:
    DFAState* NewState() {
        auto state = std::make_unique<DFAState>();
        DFAState* p = state.get();
        states.push_back(std::move(state));
        return p;
    }

    DFAState* Build(const NFA& n) {
        std::map<std::set<NFAState*>, DFAState*> state_map;
        std::queue<std::set<NFAState*>> queue;

        std::set<NFAState*> start_set = {n.start};
        EpsilonClosure(start_set);

        auto start_fa = NewState();
        if (ContainsEndState(start_set)) {
            start_fa->end = true;
        }

        state_map[start_set] = start_fa;
        queue.push(start_set);

        while (!queue.empty()) {
            auto current_set = queue.front();
            queue.pop();

            auto current_fa = state_map[current_set];

            std::set<uint32_t> alphabet;
            for (auto state : current_set) {
                for (const auto& [p, st] : state->transitions) {
                    alphabet.insert(p);
                }
            }

            for (uint32_t p : alphabet) {
                auto next_set = Move(current_set, p);
                if (next_set.empty()) continue;

                EpsilonClosure(next_set);

                DFAState* next_fa;
                if (state_map.find(next_set) == state_map.end()) {
                    next_fa = NewState();
                    if (ContainsEndState(next_set)) {
                        next_fa->end = true;
                    }
                    state_map[next_set] = next_fa;
                    queue.push(next_set);
                } else {
                    next_fa = state_map[next_set];
                }

                current_fa->transitions[p] = next_fa;
            }
        }

        return start_fa;
    }

    void PrintDFA(DFAState* start) {
        std::cout << "\n=== DFA Struct ===\n";
        std::set<DFAState*> vs;
        std::queue<DFAState*> queue;

        queue.push(start);
        vs.insert(start);
        
        while (!queue.empty()) {
            auto state = queue.front();
            queue.pop();
            
            std::cout << "State " << state->i;
            if (state == start) std::cout << " (START)";
            if (state->end) std::cout << " (END)";
            std::cout << ":\n";
            
            for (const auto& [p, t] : state->transitions) {
                std::cout << "  --";
                if (p == NFAState::DOT_CHAR) {
                    std::cout << "  --[.]-->";
                } else if (p <= 127 && p >= 32) {
                    std::cout << "'" << static_cast<char>(p) << "'";
                } else {
                    std::cout << "U+" << std::hex << p << std::dec;
                }
                std::cout << "--> State " << t->i << "\n";
                
                if (vs.find(t) == vs.end()) {
                    vs.insert(t);
                    queue.push(t);
                }
            }
        }
    }

    bool Match(DFAState* start, const std::string& text) {
        auto current = start;
        size_t pos = 0;

        while (pos < text.size()) {
            size_t bytes;
            uint32_t p = DecodeUTF8At(text, pos, &bytes);

            if (bytes == 0) {
                return false;
            }

            auto it = current->transitions.find(p);
            if (it == current->transitions.end()) {
                return false;
            }

            current = it->second;
            pos += bytes;
        }

        return current->end;
    }

private:
    void EpsilonClosure(std::set<NFAState*>& states) {
        std::stack<NFAState*> stack;

        for (auto state : states) {
            stack.push(state);
        }

        while (!stack.empty()) {
            auto current = stack.top();
            stack.pop();

            for (auto t : current->e_transitions) {
                if (states.find(t) == states.end()) {
                    states.insert(t);
                    stack.push(t);
                }
            }
        }
    }   
    
    std::set<NFAState*> Move(const std::set<NFAState*>& states, uint32_t p) {
        std::set<NFAState*> rs;

        for (auto state : states) {
            auto it = state->transitions.find(p);
            if (it != state->transitions.end()) {
                for (auto t : it->second) {
                    rs.insert(t);
                }
            }

            if (p != 10 && p != 13) {
                auto it = state->transitions.find(NFAState::DOT_CHAR);
                if (it != state->transitions.end()) {
                    for (auto t : it->second) {
                        rs.insert(t);
                    }
                }
            }
        }

        return rs;
    }

    bool ContainsEndState(const std::set<NFAState*>& states) {
        for (auto state : states) {
            if (state->end) return true;
        }
        return false;
    }
};

class Regex {
private:
    std::unique_ptr<Ast> ast;
    DFAState* fa = nullptr;
    DFABuilder e;

public:
    Regex(const std::string& pattern) {
        std::cout << "\n=== Compile Regex Pattern: \""
                  << pattern << "\" ===\n";
        
        RegexParser parser(pattern);
        ast = parser.Parse();
        std::cout << "\n✓ RecursiveDescentParse Done\n";

        std::cout << "\n=== Ast Struct ===\n";
        RegexPrinter printer;
        ast->Accept(&printer);

        std::cout << "\n=== Ast -> NFA Convert ===\n";
        NFABuilder er;
        ast->Accept(&er);
        auto n = er.GetNFA();
        std::cout << "✓ Ast -> NFA Convert Done\n";
        er.PrintNFA(n);

        fa = e.Build(n);
        e.PrintDFA(fa);
    }

    bool Match(const std::string& text) {
        return e.Match(fa, text);
    }

};

void test_regex(const std::string& pattern, const std::vector<std::string>& cases) {
    try {

        Regex regex(pattern);

        std::cout << "\n=== Test ===\n";
        for (const auto& text : cases) {
            bool r = regex.Match(text);
            std::cout << "\"" << text << "\" -> " 
                      << (r ? "Match" : "Not Match")
                      << "\n";
        }

        std::cout << "\n" << std::string(60, '=') << "\n";

    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n\n";
    }
}
} // namespace regex

int main() {
    // ASCII
    using regex::test_regex;

    test_regex("a", {"a", "b", ""});
    
    test_regex("ab", {"ab", "a", "b"});
    
    // Chinese
    test_regex("你", {"你", "好", "你好"});
    
    test_regex("你好", {"你好", "你", "好", "再见"});
    
    test_regex("你|好", {"你", "好", "你好", "再见"});
    
    test_regex("你*", {"", "你", "你你", "你你你", "好"});
    
    test_regex("你+", {"", "你", "你你", "好"});
    
    test_regex("你?", {"", "你", "你你", "好"});
    
    // English & Chinese
    test_regex("Hello世界", {"Hello世界", "Hello", "世界"});
    
    test_regex("a你b", {"a你b", "ab", "你"});
    
    test_regex("(你|好)*", {"", "你", "好", "你好", "好你", "你你好好"});
    
    // Emoji
    test_regex("🌟", {"🌟", "⭐", "星"});    

    return 0;
}