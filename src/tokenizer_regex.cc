// tokenizer_regex.cc — Regex engine for GPT-4 style pre-tokenization.
// Parser → NFA (Thompson) → Lazy DFA (on-demand subset construction) → FindAll
//
// Unicode properties: \p{A}=alpha, \p{H}=Han, \p{N}=digit, \p{L}=letter

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// ─── UTF-8 ──────────────────────────────────────────────────────────────────

static size_t UTF8Len(const char* p) {
    return "\1\1\1\1\1\1\1\1\1\1\1\1\2\2\3\4"[(*p & 0xFF) >> 4];
}

static uint32_t DecodeUTF8(const char* p, size_t len, size_t* out) {
    size_t n = UTF8Len(p);
    if (n > len) { *out = 1; return 0xFFFD; }
    *out = n;
    auto b = reinterpret_cast<const uint8_t*>(p);
    switch (n) {
        case 1: return b[0];
        case 2: return ((b[0]&0x1F)<<6)|(b[1]&0x3F);
        case 3: return ((b[0]&0x0F)<<12)|((b[1]&0x3F)<<6)|(b[2]&0x3F);
        case 4: return ((b[0]&0x07)<<18)|((b[1]&0x3F)<<12)|((b[2]&0x3F)<<6)|(b[3]&0x3F);
        default: return 0xFFFD;
    }
}

// ─── Unicode predicates ─────────────────────────────────────────────────────

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

// ─── NFA ────────────────────────────────────────────────────────────────────

using CharPred = std::function<bool(uint32_t)>;

struct NFAState {
    struct Edge { CharPred pred; int to; };
    std::vector<Edge> edges;
    std::vector<int> eps;
    bool accept = false;
};

struct NFA {
    std::vector<NFAState> states;
    int start = 0;
    int NewState() { states.emplace_back(); return (int)states.size()-1; }
    void Edge(int f, CharPred p, int t) { states[f].edges.push_back({std::move(p),t}); }
    void Eps(int f, int t) { states[f].eps.push_back(t); }
};

// ─── Parser → NFA ───────────────────────────────────────────────────────────

class Compiler {
    std::string_view p_; size_t i_=0; NFA n_;
    struct F { int s,e; };

    bool End() { return i_>=p_.size(); }
    bool Eat(char c) { if(!End()&&p_[i_]==c){++i_;return true;} return false; }
    uint32_t Next() { size_t b; auto c=DecodeUTF8(p_.data()+i_,p_.size()-i_,&b); i_+=b; return c; }

    F P(CharPred p) { int s=n_.NewState(),e=n_.NewState(); n_.states[e].accept=true; n_.Edge(s,std::move(p),e); return{s,e}; }
    F Cat(F a,F b) { n_.states[a.e].accept=false; n_.Eps(a.e,b.s); return{a.s,b.e}; }
    F Alt(F a,F b) { int s=n_.NewState(),e=n_.NewState(); n_.states[e].accept=true;
        n_.states[a.e].accept=n_.states[b.e].accept=false;
        n_.Eps(s,a.s);n_.Eps(s,b.s);n_.Eps(a.e,e);n_.Eps(b.e,e); return{s,e}; }
    F Opt(F f) { int s=n_.NewState(),e=n_.NewState(); n_.states[e].accept=true;n_.states[f.e].accept=false;
        n_.Eps(s,f.s);n_.Eps(s,e);n_.Eps(f.e,e); return{s,e}; }
    F Star(F f) { int s=n_.NewState(),e=n_.NewState(); n_.states[e].accept=true;n_.states[f.e].accept=false;
        n_.Eps(s,f.s);n_.Eps(s,e);n_.Eps(f.e,f.s);n_.Eps(f.e,e); return{s,e}; }
    F Plus(F f) { int s=n_.NewState(),e=n_.NewState(); n_.states[e].accept=true;n_.states[f.e].accept=false;
        n_.Eps(s,f.s);n_.Eps(f.e,f.s);n_.Eps(f.e,e); return{s,e}; }
    F Empty() { int s=n_.NewState(); n_.states[s].accept=true; return{s,s}; }

    F Clone(F f) {
        std::map<int,int> m; std::set<int> vis; std::vector<int> q={f.s};
        while(!q.empty()) { int x=q.back();q.pop_back(); if(vis.count(x))continue; vis.insert(x);
            m[x]=n_.NewState();
            for(auto&e:n_.states[x].edges)q.push_back(e.to);
            for(int e:n_.states[x].eps)q.push_back(e); }
        for(auto&[o,nw]:m) { n_.states[nw].accept=n_.states[o].accept;
            for(auto&e:n_.states[o].edges) n_.states[nw].edges.push_back({e.pred,m[e.to]});
            for(int e:n_.states[o].eps) n_.states[nw].eps.push_back(m[e]); }
        return{m[f.s],m[f.e]};
    }

    F Rep(F f,int lo,int hi) {
        F r={-1,-1};
        for(int i=0;i<lo;i++){F c=Clone(f);r=(r.s==-1)?c:Cat(r,c);}
        if(hi==-1){F st=Star(Clone(f));r=(r.s==-1)?st:Cat(r,st);}
        else for(int i=lo;i<hi;i++){F o=Opt(Clone(f));r=(r.s==-1)?o:Cat(r,o);}
        return(r.s==-1)?Empty():r;
    }

    CharPred ParseProp() {
        Eat('{'); std::string nm; while(!End()&&p_[i_]!='}')nm+=p_[i_++]; Eat('}');
        if(nm=="A")return IsAlpha; if(nm=="H"||nm=="Han")return IsHan;
        if(nm=="N")return IsDigit; if(nm=="L")return IsLetter;
        throw std::runtime_error("unknown \\p{"+nm+"}");
    }
    CharPred ParseEsc() {
        char c=p_[i_++];
        if(c=='r')return[](uint32_t x){return x=='\r';}; if(c=='n')return[](uint32_t x){return x=='\n';};
        if(c=='t')return[](uint32_t x){return x=='\t';}; if(c=='s')return IsWhitespace;
        if(c=='S')return[](uint32_t x){return!IsWhitespace(x);};
        if(c=='d')return IsDigit; if(c=='p')return ParseProp();
        if(c=='P'){auto p=ParseProp();return[p](uint32_t x){return!p(x);};}
        uint32_t cc=c; return[cc](uint32_t x){return x==cc;};
    }
    CharPred ParseCC() {
        bool neg=Eat('^');
        CharPred pred=[](uint32_t){return false;};
        while(!End()&&p_[i_]!=']') {
            CharPred cp;
            if(p_[i_]=='\\'){++i_;cp=ParseEsc();}
            else { uint32_t c=Next();
                if(!End()&&p_[i_]=='-'&&i_+1<p_.size()&&p_[i_+1]!=']'){++i_;uint32_t c2=Next();cp=[c,c2](uint32_t x){return x>=c&&x<=c2;};}
                else cp=[c](uint32_t x){return x==c;}; }
            pred=[a=std::move(pred),b=std::move(cp)](uint32_t x){return a(x)||b(x);};
        }
        Eat(']');
        return neg?[p=std::move(pred)](uint32_t x){return!p(x);}:pred;
    }

    F ParseAtom() {
        if(Eat('(')){if(i_+1<p_.size()&&p_[i_]=='?'&&p_[i_+1]==':')i_+=2; F f=ParseAlt();Eat(')');return f;}
        if(Eat('['))return P(ParseCC());
        if(Eat('.'))return P([](uint32_t c){return c!='\n'&&c!='\r';});
        if(Eat('\\'))return P(ParseEsc());
        return P([c=Next()](uint32_t x){return x==c;});
    }
    F ParseQ() {
        F f=ParseAtom(); if(End())return f;
        if(Eat('*')){Eat('+');return Star(f);} if(Eat('+')){Eat('+');return Plus(f);}
        if(Eat('?')){Eat('+');return Opt(f);}
        if(Eat('{')){ int lo=0,hi;
            while(!End()&&p_[i_]>='0'&&p_[i_]<='9')lo=lo*10+(p_[i_++]-'0'); hi=lo;
            if(Eat(',')){if(!End()&&p_[i_]>='0'&&p_[i_]<='9'){hi=0;while(!End()&&p_[i_]>='0'&&p_[i_]<='9')hi=hi*10+(p_[i_++]-'0');}else hi=-1;}
            Eat('}');Eat('+'); return Rep(f,lo,hi); }
        return f;
    }
    F ParseSeq() { F r={-1,-1}; while(!End()&&p_[i_]!='|'&&p_[i_]!=')'){F f=ParseQ();r=(r.s==-1)?f:Cat(r,f);} return(r.s==-1)?Empty():r; }
    F ParseAlt() { F f=ParseSeq(); while(Eat('|'))f=Alt(f,ParseSeq()); return f; }

public:
    NFA Compile(std::string_view pat) { p_=pat;i_=0;n_=NFA(); F f=ParseAlt(); n_.start=f.s; return std::move(n_); }
};

// ─── Lazy DFA (on-demand subset construction with caching) ──────────────────

class LazyDFA {
    const NFA* nfa_;
    using StateSet = std::set<int>;

    struct DState {
        bool accept;
        std::map<int, int> trans;  // equiv_class_id -> DFA state index
    };
    std::vector<DState> dfa_;
    std::map<StateSet, int> cache_;  // NFA state set -> DFA state index

    // Per-codepoint equiv class: computed from predicate signature
    std::vector<CharPred*> all_preds_;
    std::map<std::vector<bool>, int> sig_map_;
    std::map<uint32_t, int> cp_cache_;
    int next_cls_ = 0;

    void EpsClosure(StateSet& s) {
        std::vector<int> stk(s.begin(), s.end());
        while (!stk.empty()) {
            int x = stk.back(); stk.pop_back();
            for (int e : nfa_->states[x].eps)
                if (s.insert(e).second) stk.push_back(e);
        }
    }

    int GetOrCreate(StateSet& ss) {
        EpsClosure(ss);
        auto it = cache_.find(ss);
        if (it != cache_.end()) return it->second;
        int id = dfa_.size();
        bool acc = false;
        for (int s : ss) if (nfa_->states[s].accept) { acc = true; break; }
        dfa_.push_back({acc, {}});
        cache_[ss] = id;
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

    // Compute transition for DFA state + codepoint (via equiv class)
    int Step(int dfa_st, uint32_t cp) {
        int cls = ClassifyCP(cp);
        auto it = dfa_[dfa_st].trans.find(cls);
        if (it != dfa_[dfa_st].trans.end()) return it->second;

        // Compute NFA transition
        // Find which NFA state set corresponds to dfa_st
        StateSet* cur_nfa = nullptr;
        for (auto& [ss, id] : cache_)
            if (id == dfa_st) { cur_nfa = const_cast<StateSet*>(&ss); break; }

        StateSet next;
        for (int s : *cur_nfa)
            for (auto& e : nfa_->states[s].edges)
                if (e.pred(cp)) next.insert(e.to);

        if (next.empty()) {
            dfa_[dfa_st].trans[cls] = -1;
            return -1;
        }

        int next_id = GetOrCreate(next);
        dfa_[dfa_st].trans[cls] = next_id;
        return next_id;
    }

public:
    void Init(const NFA& nfa) {
        nfa_ = &nfa;
        // Collect all predicates for equiv class computation
        for (auto& st : nfa.states)
            for (auto& e : st.edges)
                all_preds_.push_back(const_cast<CharPred*>(&e.pred));

        StateSet start = {nfa.start};
        GetOrCreate(start);
    }

    int MatchAt(const char* data, size_t len) {
        int cur = 0;
        int last = dfa_[0].accept ? 0 : -1;
        size_t pos = 0;
        while (pos < len) {
            size_t bytes;
            uint32_t cp = DecodeUTF8(data+pos, len-pos, &bytes);
            int next = Step(cur, cp);
            if (next < 0) break;
            cur = next;
            pos += bytes;
            if (dfa_[cur].accept) last = pos;
        }
        return last;
    }

    std::vector<std::string_view> FindAll(std::string_view text) {
        std::vector<std::string_view> r;
        const char* d = text.data();
        size_t len = text.size(), pos = 0;
        while (pos < len) {
            int n = MatchAt(d+pos, len-pos);
            if (n > 0) { r.emplace_back(d+pos, n); pos += n; }
            else pos += UTF8Len(d+pos);
        }
        return r;
    }

    void PrintStats() {
        std::cout << "LazyDFA: " << dfa_.size() << " states, "
                  << next_cls_ << " equiv classes\n";
    }
};

// ─── main ───────────────────────────────────────────────────────────────────

int main() {
    const char* pattern =
        "'([sdmt]|ll|ve|re)"
        "|[^\\r\\n\\p{A}\\p{H}\\p{N}]?\\p{A}+"
        "|\\p{H}+"
        "|\\p{N}+"
        "| ?[^\\s\\p{A}\\p{H}\\p{N}]+[\\r\\n]*"
        "|\\s*[\\r\\n]"
        "|\\s+";

    std::cout << "Compiling...\n";
    Compiler compiler;
    NFA nfa = compiler.Compile(pattern);
    std::cout << "NFA: " << nfa.states.size() << " states\n";

    LazyDFA dfa;
    dfa.Init(nfa);

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
        {"  hello"},
        {"hello  world"},
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
        auto tokens = dfa.FindAll(tc.in);
        std::cout << "'" << tc.in << "'\n  -> [";
        for (size_t i = 0; i < tokens.size(); i++) {
            if (i) std::cout << ", ";
            std::cout << "'" << tokens[i] << "'";
        }
        std::cout << "]\n";
    }

    dfa.PrintStats();
    return 0;
}
