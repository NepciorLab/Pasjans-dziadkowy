#pragma once
// Developed with AI assistance (Claude, Anthropic) — see README.md.
// ============================================================================
// solver.h — offline solver for one fixed deal ("Solver" toolbar button)
// ============================================================================
// Unlike the auto-play engine (ai_engine / rankCandidates in game.h), which
// plays like a human and is never allowed to peek at the reserve, the solver
// works in memory on a KNOWN deal: the whole reserve order is part of the
// position. It is a randomised-restart depth-first search with backtracking:
//
//   * every position (canonical hash: column order and the twin foundation
//     slots do not matter) is expanded at most once per attempt,
//   * moves are tried best-first by a cheap static heuristic; from the second
//     attempt on random noise is added to that heuristic, so attempts explore
//     different parts of the tree (restarts beat one endless dive: solver
//     run-times on this kind of game are heavy-tailed),
//   * an attempt has a node budget; each new attempt gets a bigger one (up to
//     a cap), so early attempts are quick and later ones dig deeper,
//   * attempts run in parallel on several threads; the first solution wins.
//
// Nothing here touches the UI or the global game state: it only reads the
// start position it is given, and reports through a Progress struct.
// ============================================================================
#include "game.h"
#include <cstdint>
#include <vector>
#include <atomic>
#include <algorithm>
#include <unordered_map>

namespace solver {

// One move of the solver's own move set. Column numbers are 0..9, foundation
// slots 0..7 (same numbering as GameState).
struct Mv {
   int8_t  type = 0;   // 0 col->col, 1 col->foundation, 2 foundation->col, 3 deal from reserve
   int8_t  a = 0;      // source column (type 0/1) or source foundation slot (type 2)
   int8_t  b = 0;      // target column (type 0/2) or target foundation slot (type 1)
   int16_t ci = 0;     // type 0: index in column `a` of the first moved card
   int     score = 0;  // ordering heuristic (higher = tried first)
};

// Live progress, written by the search threads, read by the UI thread.
struct Progress {
   std::atomic<int>       attemptsStarted{0};   // total attempts launched so far (this deal)
   std::atomic<int>       attemptsFinished{0};
   std::atomic<long long> totalNodes{0};        // positions expanded, all attempts
   std::atomic<int>       bestKings{0};         // most kings "in place" seen (goal: 8)
   std::atomic<int>       bestPlaced{0};        // most cards in place (foundations + finished king columns), of 104
   // The most advanced attempt currently running (deepest current line):
   std::atomic<int>       leadAttempt{0};       // its number (1-based)
   std::atomic<int>       leadDepth{0};         // moves in its current line
   std::atomic<int>       leadMaxDepth{0};      // deepest line it has reached
   std::atomic<long long> leadNodes{0};         // nodes used in it
   std::atomic<long long> leadBudget{0};        // its node budget
   std::atomic<int>       threads{0};
   std::atomic<int>       bestPrefixLen{0};     // length of the shared "best known line" attempts are now building on (see Shared::bestPrefix)
};

struct Result {
   bool solved = false;
   bool cancelled = false;
   bool timedOut = false;
   std::vector<Mv> moves;       // the full winning line, in play order (solved==true only)
   // Best-effort progress even when not solved: the best line any attempt
   // reached (see AttemptOutcome::lastPath). A caller that wants to keep
   // looking later can hand this straight back in as `resumePrefix` to
   // solveDeal() and pick up building on it immediately, instead of starting
   // over from scratch and re-deriving the same ground again.
   std::vector<Mv> bestEffort;
   int bestPotential = -1;
};

// ── rules (mode-dependent, independent of the global mode pointer) ───────────
inline bool canDrop(const GameState& g, const Card& head, int tc, bool freeMode){
   if(g.cols[tc].empty()) return freeMode ? true : head.rank==King;
   const Card& b = g.cols[tc].back();
   return (int)b.rank==(int)head.rank+1 && b.isRed()!=head.isRed();
}
inline bool skipOnDealMode(const GameState& g, int col, bool freeMode){
   if(freeMode) return false;
   if(g.cols[col].empty()) return false;
   if(g.cols[col][0].rank!=King) return false;
   return g.validSeqFrom(col,0);
}

// Cards "in place" as far as the win condition goes: everything on the
// foundations plus every column that is a complete king-headed run; also the
// number of such kings (foundation piles topped by a king count too).
inline void progressOf(const GameState& g, int& kings, int& placed){
   kings=0; placed=0;
   for(int f=0;f<NUM_FOUND;f++){
      placed += (int)g.found[f].size();
      if(!g.found[f].empty() && g.found[f].back().rank==King) kings++;
   }
   for(int c=0;c<NUM_COLS;c++){
      if(!g.cols[c].empty() && g.cols[c][0].rank==King && g.validSeqFrom(c,0)){
         kings++; placed += (int)g.cols[c].size();
      }
   }
}

// A more forgiving "how close to solved" measure than progressOf()'s raw
// placed-card count, used only for the stall cutoff below (progressOf()
// itself still drives the UI's "best progress" display, which should stay
// literal). Winning some deals — king-only mode especially — genuinely
// requires temporarily sending a card back from a foundation to the table to
// use it in a sequence; that briefly DECREASES the foundation count, so
// gating "progress" on that count alone would make the stall cutoff actively
// fight the very strategy some deals need. Adding the length of each
// column's already-correctly-ordered bottom run keeps a buffering detour
// from looking like a regression as long as it is building useful table
// structure elsewhere.
inline int solverPotential(const GameState& g){
   int k,pl; progressOf(g,k,pl);
   int runs=0;
   for(int c=0;c<NUM_COLS;c++) runs += g.seqLenFromBottom(c);
   return pl*3 + runs;
}

// Position hash: column ORDER is irrelevant (columns are hashed one by one and
// combined as a sorted multiset), and so is which of a suit's two foundation
// slots holds which pile. The reserve is implied by the number of cards left
// outside it, so it needs no part in the hash.
inline uint64_t stateHash(const GameState& g){
   static const struct T { uint64_t card[52]; uint64_t found[4][14][14];
      T(){ uint64_t s=0x9E3779B97F4A7C15ULL; auto nx=[&]{ s+=0x9E3779B97F4A7C15ULL; uint64_t z=s; z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL; z=(z^(z>>27))*0x94D049BB133111EBULL; return z^(z>>31); };
           for(auto& v:card) v=nx(); for(auto& a:found) for(auto& r:a) for(auto& v:r) v=nx(); } } tab;
   uint64_t ch[NUM_COLS];
   for(int c=0;c<NUM_COLS;c++){
      uint64_t h=0xcbf29ce484222325ULL + (uint64_t)g.cols[c].size();
      for(const Card& k : g.cols[c]) h=(h^tab.card[k.typeId()])*1099511628211ULL;
      ch[c]=h;
   }
   std::sort(ch,ch+NUM_COLS);
   uint64_t h=0x1234567ULL;
   for(int c=0;c<NUM_COLS;c++){ h ^= ch[c] + 0x9E3779B97F4A7C15ULL + (h<<6) + (h>>2); }
   // foundation piles as a multiset of (top card) values; an empty pile is tied to its twin-slot pair
   // (only that suit's Ace may start it), and a pile may sit in a slot of another suit
   uint64_t fv[NUM_FOUND];
   for(int f=0;f<NUM_FOUND;f++) fv[f] = g.found[f].empty() ? (uint64_t)(0x100+f/2) : (uint64_t)g.found[f].back().typeId()+1;
   std::sort(fv,fv+NUM_FOUND);
   for(int f=0;f<NUM_FOUND;f++){ h ^= (fv[f]*0x9E3779B97F4A7C15ULL) + (h<<7) + (h>>3); }
   return h ? h : 1;
}

// Exact (layout-sensitive) hash: column positions and foundation slots matter. Used when
// jumping between positions of one line, where the moves that follow refer to concrete
// column / slot numbers.
inline uint64_t exactHash(const GameState& g){
   uint64_t h=0xcbf29ce484222325ULL;
   auto mix=[&](uint64_t v){ h=(h^v)*1099511628211ULL; h^=h>>29; };
   for(int c=0;c<NUM_COLS;c++){ mix(0xC0FFEE00ULL+c); for(const Card& k:g.cols[c]) mix((uint64_t)k.typeId()+1); }
   for(int f=0;f<NUM_FOUND;f++){ mix(0xF0F0F000ULL+f); mix(g.found[f].size()); if(!g.found[f].empty()) mix((uint64_t)g.found[f].back().typeId()+1); }
   return h ? h : 1;
}

// ── search state: a GameState used as a plain board + a reserve read position ─
struct Undo {
   Mv mv; int n=0; Card card{}; int dealt=0; int8_t dealCols[NUM_COLS]; int prevRpos=0;
};

struct Board {
   GameState g;                 // cols / found used; g.reserve is the FULL, untouched reserve
   int rpos = 0;                // next reserve card to be dealt
   bool freeMode = false;

   Undo apply(const Mv& m){
      Undo u; u.mv=m; u.prevRpos=rpos;
      if(m.type==0){
         auto& src=g.cols[m.a]; auto& dst=g.cols[m.b];
         u.n=(int)src.size()-m.ci;
         dst.insert(dst.end(), src.begin()+m.ci, src.end());
         src.resize(m.ci);
      } else if(m.type==1){
         u.card=g.cols[m.a].back();
         g.found[m.b].push_back(u.card);
         g.cols[m.a].pop_back();
      } else if(m.type==2){
         u.card=g.found[m.a].back();
         g.cols[m.b].push_back(u.card);
         g.found[m.a].pop_back();
      } else {
         int ri=rpos, rs=(int)g.reserve.size(), d=0;
         for(int col=0; col<NUM_COLS && ri<rs; col++){
            if(skipOnDealMode(g,col,freeMode)) continue;
            g.cols[col].push_back(g.reserve[ri++]);
            u.dealCols[d++]=(int8_t)col;
         }
         u.dealt=d; rpos=ri;
      }
      return u;
   }
   void undo(const Undo& u){
      const Mv& m=u.mv;
      if(m.type==0){
         auto& src=g.cols[m.a]; auto& dst=g.cols[m.b];
         src.insert(src.end(), dst.end()-u.n, dst.end());
         dst.resize(dst.size()-u.n);
      } else if(m.type==1){
         g.cols[m.a].push_back(u.card);
         g.found[m.b].pop_back();
      } else if(m.type==2){
         g.found[m.a].push_back(u.card);
         g.cols[m.b].pop_back();
      } else {
         for(int i=u.dealt-1;i>=0;i--) g.cols[u.dealCols[i]].pop_back();
         rpos=u.prevRpos;
      }
   }
};

// Fills `out` with every legal move of the position, each with its ordering score.
inline void generate(const Board& B, std::vector<Mv>& out){
   const GameState& g=B.g; const bool fm=B.freeMode;
   out.clear();
   int firstEmpty=-1;
   for(int c=0;c<NUM_COLS;c++) if(g.cols[c].empty()){ firstEmpty=c; break; }

   // column bottoms that can go straight to a foundation (used by the scores below)
   bool bottomToFound[NUM_COLS];
   for(int c=0;c<NUM_COLS;c++){
      bottomToFound[c]=false;
      if(g.cols[c].empty()) continue;
      const Card& b=g.cols[c].back(); int f=g.bestFoundSlot(b);
      bottomToFound[c]=(f>=0 && g.canDropOnFound(b,f));
   }

   // column -> foundation
   for(int c=0;c<NUM_COLS;c++){
      if(!bottomToFound[c]) continue;
      const Card& b=g.cols[c].back();
      Mv m; m.type=1; m.a=(int8_t)c; m.b=(int8_t)g.bestFoundSlot(b);
      m.score=1000 + (int)(13-(int)b.rank);            // low cards first
      if(g.cols[c].size()==1) m.score+=150;             // empties a column
      else if(g.alreadySittingCorrectly(c,(int)g.cols[c].size()-1)) m.score-=60; // breaks a run
      out.push_back(m);
   }
   // column -> column
   for(int fc=0;fc<NUM_COLS;fc++){
      if(g.cols[fc].empty()) continue;
      int n=(int)g.cols[fc].size();
      int sl=g.seqLenFromBottom(fc), si=n-sl;
      for(int ci=si; ci<n; ci++){
         const Card& head=g.cols[fc][ci];
         bool sittingOk = g.alreadySittingCorrectly(fc,ci);
         for(int tc=0;tc<NUM_COLS;tc++){
            if(tc==fc) continue;
            bool tEmpty=g.cols[tc].empty();
            if(tEmpty){
               if(tc!=firstEmpty) continue;             // all empty columns are equivalent
               if(ci==0) continue;                      // whole column to an empty one: no-op
            }
            if(!canDrop(g,head,tc,fm)) continue;
            Mv m; m.type=0; m.a=(int8_t)fc; m.b=(int8_t)tc; m.ci=(int16_t)ci;
            int sc=100;
            if(!tEmpty){
               sc+=15;
               if(g.cols[tc].back().suit==head.suit) sc+=10;    // same-suit stacking helps the foundation later
            } else {
               sc += fm ? -70 : 60;                      // king-only: a king on a free column is good; free mode: keep columns free
            }
            if(ci==0){ sc+=140; }                        // column becomes free
            else {
               const Card& x=g.cols[fc][ci-1];           // card that gets uncovered
               if(sittingOk) sc-=45;                     // splits a run
               else sc+=25;
               int f=g.bestFoundSlot(x);
               if(f>=0 && g.canDropOnFound(x,f)) sc+=60;
               else {
                  // could the uncovered card find a new home on another column?
                  for(int oc=0;oc<NUM_COLS;oc++)
                     if(oc!=fc && oc!=tc && !g.cols[oc].empty() && canDrop(g,x,oc,fm)){ sc+=12; break; }
               }
               if(x.rank<=Three) sc+=10;
            }
            // uncovering the low cards buried in the source column
            sc += (int)(13-(int)head.rank)/2;
            m.score=sc;
            out.push_back(m);
         }
      }
   }
   // foundation -> column (reversible buffer moves; last resort)
   for(int f=0;f<NUM_FOUND;f++){
      if(g.found[f].empty()) continue;
      const Card& c=g.found[f].back();
      // same-suit twin slot with an identical top card gives the same move: only the lower slot
      int fa,fb; GameState::preferredSlots(c.suit,fa,fb);
      if(f==fb && !g.found[fa].empty() && g.found[fa].back()==c) continue;
      for(int tc=0;tc<NUM_COLS;tc++){
         if(g.cols[tc].empty()){ if(tc!=firstEmpty) continue; }
         if(!canDrop(g,c,tc,fm)) continue;
         Mv m; m.type=2; m.a=(int8_t)f; m.b=(int8_t)tc; m.score=-50;
         out.push_back(m);
      }
   }
   // deal
   if(B.rpos<(int)g.reserve.size()){
      Mv m; m.type=3; m.score=-100;
      out.push_back(m);
   }
}

// ── visited set: open addressing, 8 bytes per slot ───────────────────────────
struct HashSet {
   std::vector<uint64_t> t; uint64_t mask=0;
   void init(size_t minEntries){
      size_t cap=1024; while(cap<minEntries*2) cap<<=1;
      t.assign(cap,0); mask=cap-1;
   }
   // true if newly inserted
   bool insert(uint64_t h){
      size_t i=(size_t)(h*0x9E3779B97F4A7C15ULL>>20)&mask;
      for(;;){
         uint64_t v=t[i];
         if(v==0){ t[i]=h; return true; }
         if(v==h) return false;
         i=(i+1)&mask;
      }
   }
};

inline uint64_t nextRand(uint64_t& s){ s^=s<<13; s^=s>>7; s^=s<<17; return s; }

// Node budget of attempt number `k` (0-based): grows geometrically, capped.
inline long long budgetForAttempt(int k){
   double b=120000.0; for(int i=0;i<k && b<2.0e6;i++) b*=1.35;
   return (long long)std::min(b,2.0e6);
}

struct AttemptOutcome {
   bool solved=false; bool exhausted=false; bool aborted=false;
   std::vector<Mv> moves;    // winning line (solved==true only)
   // Best-effort report for a FAILED attempt: wherever its own current line
   // happened to be when it stopped (budget used up, cancelled, or backtracked
   // all the way out) — see the "seedPrefix" comment on runAttempt() for what
   // this feeds into. -1 = nothing usable (e.g. it exhausted at the very root).
   int lastPot=-1;
   std::vector<Mv> lastPath;
};

// How many consecutive moves an attempt may make without beating its own best
// "cards in place" count reached so far ON THE CURRENT PATH before that path is
// treated as a dead end (backtracked out of, exactly like running out of legal
// moves). Without this, the search below is effectively a single very long
// greedy/randomised WALK, not a real backtracking search: with a large branching
// factor a node's subtree is essentially never fully exhausted within any
// reasonable node budget, so the DFS almost always just keeps diving along
// whatever move currently scores best instead of ever reconsidering an early
// choice. A stall cutoff forces frequent, cheap backtracking — the same idea
// main.cpp's own auto-player uses (FOUNDATION_STALL_LIMIT) against endless
// non-progress, applied here per search path instead of to real play.
//
// Paired with depthLimit below, on the same odd/even split: the depth-capped
// (odd) attempts are meant to stay cheap and are what typically solves an
// ordinary deal in a handful of quick attempts, so they keep a short leash.
// The uncapped (even) attempts are the ones aimed at genuinely hard deals —
// where untangling one badly buried column can legitimately take several
// hundred moves of re-sequencing before "cards in place" ticks over again —
// so they get a generous budget from the very first one, rather than only
// once dozens of earlier attempts have already run.
inline int stallLimitForAttempt(int attemptNo){
   return (attemptNo%2==1) ? (60 + 10*std::min(attemptNo,20)) : 150;
}

// One depth-first attempt with its own visited set and node budget.
// `stop()` is polled every few thousand nodes (cancel / deadline / another
// thread already found a solution).
//
// `seedPrefix`/`seedLen`, when given, are replayed move-for-move BEFORE the
// attempt's own search starts (for free — none of it counts against this
// attempt's node budget), so the search proper explores onward from wherever
// that prefix leads rather than from the original deal. This is how attempts
// build on each other's progress instead of every one separately re-deriving
// the same easy opening stretch: solveDeal() hands back its single best
// known prefix (the deepest genuine progress ANY attempt has reached so far,
// win or not — see AttemptOutcome::lastPath/lastPot), and each new attempt
// gets a randomly-truncated slice of it (see workerProc), so attempts stay
// diverse instead of all converging on identical continuations.
template<class StopFn>
inline AttemptOutcome runAttempt(const GameState& start, bool freeMode, int attemptNo /*0-based*/,
                                  long long budget, Progress& P, StopFn stop,
                                  const std::vector<Mv>* seedPrefix=nullptr, size_t seedLen=0){
   AttemptOutcome out;
   Board B; B.g=start.boardOnly(); B.g.reserve=start.reserve; B.rpos=0; B.freeMode=freeMode;
   HashSet seen; seen.init((size_t)budget+16);
   seen.insert(stateHash(B.g));

   std::vector<Mv> prefix;
   if(seedPrefix && seedLen>0){
      prefix.assign(seedPrefix->begin(), seedPrefix->begin()+std::min(seedLen,seedPrefix->size()));
      for(const Mv& mv : prefix){ B.apply(mv); seen.insert(stateHash(B.g)); }
   }

   struct Frame { std::vector<Mv> mvs; int next=0; Undo cur; bool applied=false; };
   std::vector<Frame> st; st.reserve(4096);
   uint64_t rng=0x9E3779B97F4A7C15ULL ^ ((uint64_t)(attemptNo+1)*0xD1B54A32D192ED03ULL);
   for(int i=0;i<8;i++) nextRand(rng);
   const int noiseAmp = attemptNo==0 ? 0 : 20 + 12*std::min(attemptNo,25);
   // Longest line an attempt may dive to. A cap keeps the lines short (an unbounded dive
   // happily wanders through thousands of equivalent shuffles before it wins); it
   // relaxes with every attempt, so hard deals are still reachable.
   const int depthLimit = (attemptNo%2==1) ? 450 + 45*attemptNo : 1000000; // attempt 0 and every other one after it are uncapped

   auto expand=[&](size_t depth){
      if(st.size()<=depth) st.resize(depth+1);
      Frame& f=st[depth]; f.next=0; f.applied=false;
      generate(B,f.mvs);
      if(noiseAmp>0) for(Mv& m: f.mvs) m.score += (int)(nextRand(rng)%(uint64_t)(2*noiseAmp+1)) - noiseAmp;
      std::stable_sort(f.mvs.begin(),f.mvs.end(),[](const Mv& x,const Mv& y){ return x.score>y.score; });
   };

   // Per-path bookkeeping for the stall cutoff: bestOnPath[d]/stallOnPath[d] are
   // the best "placed" count reached anywhere from the root down to (and
   // including) depth d on the CURRENT path, and how many moves in a row since
   // then failed to beat it. Indexed exactly like `st` (resized alongside it).
   // The baseline (index 0) is the position search actually STARTS from —
   // after the replayed prefix, if any.
   std::vector<int> bestOnPath, stallOnPath;
   bestOnPath.push_back(solverPotential(B.g)); stallOnPath.push_back(0);

   // Builds a full move list — prefix, then st[0..d] — for either a win or a
   // best-effort "how far did this attempt get" report.
   auto fullPath=[&](size_t d)->std::vector<Mv>{
      std::vector<Mv> line; line.reserve(prefix.size()+d+1);
      line.insert(line.end(), prefix.begin(), prefix.end());
      for(size_t i=0;i<=d;i++) line.push_back(st[i].cur.mv);
      return line;
   };

   long long nodes=0; int maxDepth=0, bestK=0, bestPl=0;
   bool lead=false;
   expand(0);
   size_t depth=0;
   for(;;){
      Frame& f=st[depth];
      if(f.applied){ B.undo(f.cur); f.applied=false; }
      if(f.next>=(int)f.mvs.size()){
         if(depth==0){ out.exhausted=true; break; }
         depth--; continue;
      }
      const Mv mv=f.mvs[f.next++];
      f.cur=B.apply(mv); f.applied=true;
      if(!seen.insert(stateHash(B.g))) continue;           // already expanded in this attempt
      nodes++;
      // progress bookkeeping
      int k,pl; progressOf(B.g,k,pl);
      if(k>bestK) bestK=k;
      if(pl>bestPl) bestPl=pl;
      if(k>P.bestKings.load(std::memory_order_relaxed)) P.bestKings.store(k,std::memory_order_relaxed);
      if(pl>P.bestPlaced.load(std::memory_order_relaxed)) P.bestPlaced.store(pl,std::memory_order_relaxed);
      if(B.g.checkWin()){
         out.solved=true;
         out.moves=fullPath(depth);
         break;
      }
      if((nodes&1023)==0){
         P.totalNodes.fetch_add(1024,std::memory_order_relaxed);
         int d=(int)depth+1; if(d>maxDepth) maxDepth=d;
         // the attempt with the deepest current line is the one shown as "current"
         int ld=P.leadDepth.load(std::memory_order_relaxed);
         if(lead || d>=ld || P.leadAttempt.load(std::memory_order_relaxed)==attemptNo+1){
            lead=true;
            P.leadAttempt.store(attemptNo+1,std::memory_order_relaxed);
            P.leadDepth.store(d,std::memory_order_relaxed);
            P.leadMaxDepth.store(maxDepth,std::memory_order_relaxed);
            P.leadNodes.store(nodes,std::memory_order_relaxed);
            P.leadBudget.store(budget,std::memory_order_relaxed);
         }
      }
      int pot = solverPotential(B.g);         // computed once, used by both the checks below
      if((nodes&4095)==0 && stop()){ out.aborted=true; out.lastPot=pot; out.lastPath=fullPath(depth); break; }
      if(nodes>=budget){                       // budget used up: just end this attempt here
         out.lastPot=pot; out.lastPath=fullPath(depth); break;
      }
      if((int)depth+1>=depthLimit) continue;    // line long enough: do not dive further, try a sibling instead
      // Stall cutoff: this move didn't beat the path's own best-so-far, and
      // neither did the STALL_LIMIT before it — treat this branch as a dead
      // end (same "continue without descending" as the depthLimit case above,
      // so the top of the loop undoes it and the next sibling move is tried).
      size_t nd=depth+1;
      if(bestOnPath.size()<=nd){ bestOnPath.resize(nd+1); stallOnPath.resize(nd+1); }
      bool improved = pot>bestOnPath[depth];
      bestOnPath[nd] = improved ? pot : bestOnPath[depth];
      stallOnPath[nd] = improved ? 0 : stallOnPath[depth]+1;
      if(stallOnPath[nd]>stallLimitForAttempt(attemptNo)) continue;
      depth++;
      expand(depth);
   }
   P.totalNodes.fetch_add(nodes&1023,std::memory_order_relaxed);
   if(lead && P.leadAttempt.load()==attemptNo+1){ P.leadDepth.store(0); P.leadAttempt.store(0); } // let another attempt take over the display
   // Exhausted right at the seeded starting position (depth==0, nothing ever
   // applied): nothing better than the seed itself to report — the caller
   // already knows that one.
   return out;
}


// The depth-first search finds *a* solution, usually one wandering through
// thousands of pointless moves. This removes the wandering: whenever a state
// on the line can reach some LATER state of the same line (up to column order
// and the twin foundation slots) with a single move — or IS such a state —
// jump straight there. Because the later state may hold its columns in a
// different order, the rest of the line is re-labelled through a column / slot
// map (rebuilt after every jump). Repeated until nothing more can be cut.
// Only positions of the line itself are used, so the result is always a valid
// line; callers still re-verify it by replaying it with the real rules.
inline void matchLayout(const GameState& O, const GameState& R, int colMap[NUM_COLS], int slotMap[NUM_FOUND]){
   // colMap[oc] = column of R holding the same cards as column oc of O
   bool used[NUM_COLS]={false};
   for(int oc=0;oc<NUM_COLS;oc++){
      colMap[oc]=-1;
      for(int rc=0;rc<NUM_COLS;rc++){
         if(used[rc]) continue;
         if(O.cols[oc].size()!=R.cols[rc].size()) continue;
         bool same=true;
         for(size_t k=0;k<O.cols[oc].size();k++) if(!(O.cols[oc][k]==R.cols[rc][k])){ same=false; break; }
         if(same){ colMap[oc]=rc; used[rc]=true; break; }
      }
   }
   // foundation piles: match by content anywhere (a pile may sit in another suit's slot)...
   bool usedS[NUM_FOUND]={false};
   for(int os=0;os<NUM_FOUND;os++){
      slotMap[os]=-1;
      if(O.found[os].empty()) continue;
      for(int rs=0;rs<NUM_FOUND;rs++){
         if(usedS[rs] || R.found[rs].empty()) continue;
         if(O.found[os].size()!=R.found[rs].size()) continue;
         if(!(O.found[os].back()==R.found[rs].back())) continue;
         slotMap[os]=rs; usedS[rs]=true; break;
      }
   }
   // ...empty slots stay within their twin pair (only that suit's Ace can start a pile there)
   for(int os=0;os<NUM_FOUND;os++){
      if(!O.found[os].empty()) continue;
      int pair0=(os/2)*2;
      if(!usedS[os] && R.found[os].empty()){ slotMap[os]=os; usedS[os]=true; continue; }
      for(int rs=pair0;rs<pair0+2;rs++)
         if(!usedS[rs] && R.found[rs].empty()){ slotMap[os]=rs; usedS[rs]=true; break; }
   }
}
// Replays `line` from `start` under the rules and reports whether every move is legal
// and the final position is won. `badIdx` (optional) receives the index of the first
// illegal move (or -1 if all moves were legal but the end is not a win).
inline bool validateLine(const GameState& start, bool freeMode, const std::vector<Mv>& line, int* badIdx=nullptr){
   Board B; B.g=start.boardOnly(); B.g.reserve=start.reserve; B.rpos=0; B.freeMode=freeMode;
   if(badIdx) *badIdx=-1;
   for(size_t i=0;i<line.size();i++){
      const Mv& m=line[i]; bool ok=true;
      const GameState& g=B.g;
      if(m.type==0){
         ok = m.a>=0&&m.a<NUM_COLS&&m.b>=0&&m.b<NUM_COLS&&m.a!=m.b && m.ci>=0 && m.ci<(int)g.cols[m.a].size();
         if(ok){ int n=(int)g.cols[m.a].size(); ok = (n-m.ci)<=g.seqLenFromBottom(m.a) && canDrop(g,g.cols[m.a][m.ci],m.b,freeMode); }
      } else if(m.type==1){
         ok = m.a>=0&&m.a<NUM_COLS&&m.b>=0&&m.b<NUM_FOUND && !g.cols[m.a].empty() && g.canDropOnFound(g.cols[m.a].back(),m.b);
      } else if(m.type==2){
         ok = m.a>=0&&m.a<NUM_FOUND&&m.b>=0&&m.b<NUM_COLS && !g.found[m.a].empty() && canDrop(g,g.found[m.a].back(),m.b,freeMode);
      } else if(m.type==3){
         ok = B.rpos<(int)g.reserve.size();
      } else ok=false;
      if(!ok){ if(badIdx) *badIdx=(int)i; return false; }
      B.apply(m);
   }
   return B.g.checkWin();
}
struct Jump { int to=-1; int len=0; Mv seq[3]; };

// Looks for the largest saving reachable from the board's current position:
// a sequence of at most `maxLen` moves that lands on a state of the line
// (found through `last`) with an index far enough ahead to be worth it.
inline void findJump(Board& B, int i, int maxLen, const std::unordered_map<uint64_t,int>& last,
                     int len, Mv* seq, Jump& best){
   std::vector<Mv> cand; generate(B,cand);
   for(const Mv& m: cand){
      Undo u=B.apply(m);
      seq[len]=m;
      auto it=last.find(stateHash(B.g));
      if(it!=last.end()){
         int saving=it->second-(i+len+1);
         int bestSaving=best.to<0?0:best.to-(i+best.len);
         if(saving>0 && saving>bestSaving){ best.to=it->second; best.len=len+1; for(int k=0;k<=len;k++) best.seq[k]=seq[k]; }
      }
      if(len+1<maxLen) findJump(B,i,maxLen,last,len+1,seq,best);
      B.undo(u);
   }
}
template<class StopFn>
inline void shortenLine(const GameState& start, bool freeMode, std::vector<Mv>& line, StopFn stop){
   int maxLen=1; int idle=0;
   for(int pass=0; pass<200; pass++){
      const int n=(int)line.size();
      if(n<3) return;
      Board B; B.g=start.boardOnly(); B.g.reserve=start.reserve; B.rpos=0; B.freeMode=freeMode;
      std::vector<uint64_t> h(n+1);
      { Board R=B; h[0]=stateHash(R.g);
        for(int i=0;i<n;i++){ R.apply(line[i]); h[i+1]=stateHash(R.g); } }
      std::unordered_map<uint64_t,int> last; last.reserve((size_t)n*2);
      for(int i=0;i<=n;i++) last[h[i]]=i;              // later occurrence wins
      std::vector<Jump> jumps(n);
      for(int i=0;i<n;i++){
         if(stop()) return;
         int again=last[h[i]];
         if(again>i){ jumps[i].to=again; jumps[i].len=0; }     // state repeats later: skip the loop between
         else if(maxLen==1 || n<=4000 || i%(maxLen)==0){
            Mv seq[3]; findJump(B,i,maxLen,last,0,seq,jumps[i]);
         }
         B.apply(line[i]);
      }
      // rebuild the line, following jumps
      std::vector<Mv> nl; nl.reserve(n);
      Board O; O.g=start.boardOnly(); O.g.reserve=start.reserve; O.rpos=0; O.freeMode=freeMode;   // original line's state
      Board R=O;                                                                                    // state actually reached
      int colMap[NUM_COLS], slotMap[NUM_FOUND];
      for(int c=0;c<NUM_COLS;c++) colMap[c]=c;
      for(int s=0;s<NUM_FOUND;s++) slotMap[s]=s;
      auto translate=[&](Mv m)->Mv{
         if(m.type==0){ m.a=(int8_t)colMap[m.a]; m.b=(int8_t)colMap[m.b]; }
         else if(m.type==1){ m.a=(int8_t)colMap[m.a]; m.b=(int8_t)slotMap[m.b]; }
         else if(m.type==2){ m.a=(int8_t)slotMap[m.a]; m.b=(int8_t)colMap[m.b]; }
         return m;
      };
      int cur=0; bool ok=true;
      while(cur<n){
         if(jumps[cur].to>cur){
            int to=jumps[cur].to;
            for(int k=0;k<jumps[cur].len;k++){ Mv m=translate(jumps[cur].seq[k]); nl.push_back(m); R.apply(m); }
            for(int k=cur;k<to;k++) O.apply(line[k]);
            cur=to;
            matchLayout(O.g,R.g,colMap,slotMap);
            for(int c=0;c<NUM_COLS;c++) if(colMap[c]<0) ok=false;
            for(int s=0;s<NUM_FOUND;s++) if(slotMap[s]<0) ok=false;
            if(!ok) break;
         } else {
            Mv m=translate(line[cur]); nl.push_back(m); R.apply(m); O.apply(line[cur]); cur++;
         }
      }
      if(!ok) return;
      if(!validateLine(start,freeMode,nl,nullptr)) return;      // safety net: never hand back a broken line
      if(nl.size()>=line.size()){
         // nothing gained with this reach: widen the search (short lines only), else finish
         if(maxLen<3 && (maxLen<2 || n<700)) { maxLen++; continue; }
         return;
      }
      line.swap(nl);
   }
}

// ── multi-threaded driver ────────────────────────────────────────────────────
struct Shared {
   const GameState* start=nullptr; bool freeMode=false;
   Progress* P=nullptr; const std::atomic<bool>* cancel=nullptr; ULONGLONG deadline=0;
   std::atomic<bool> done{false};        // stop everything: a good enough line exists (or the search was cancelled)
   std::atomic<bool> haveBest{false};
   ULONGLONG firstFound=0;               // tick of the first solution
   CRITICAL_SECTION cs; std::vector<Mv> best;
   // Best progress ANY attempt has reached so far, win or not (see
   // AttemptOutcome::lastPath/lastPot and runAttempt()'s seedPrefix comment) —
   // both protected by `cs`, same as `best` above. New attempts build on this
   // instead of every one separately re-deriving the same opening stretch.
   int bestPotential=-1; std::vector<Mv> bestPrefix;
};
static const size_t GOOD_ENOUGH_LINE = 400;          // a line this short ends the search at once
static const ULONGLONG POLISH_EXTRA_MS = 30000;      // otherwise keep looking for a shorter one for this long

inline DWORD WINAPI workerProc(LPVOID p){
   Shared* S=(Shared*)p;
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
   auto stop=[&]{
      if(S->done.load(std::memory_order_relaxed) || S->cancel->load(std::memory_order_relaxed)) return true;
      ULONGLONG now=GetTickCount64();
      if(S->haveBest.load(std::memory_order_relaxed)) return now>S->firstFound+POLISH_EXTRA_MS;
      return now>S->deadline;
   };
   while(!stop()){
      int k=S->P->attemptsStarted.fetch_add(1);
      // Build on the best progress found so far (by ANY attempt, win or not)
      // instead of starting fresh every time — but not for every attempt: a
      // third of them (and the first handful, before any prefix exists yet)
      // still explore completely fresh, in case the shared prefix has led
      // everyone into a trap. A random 50-100% slice of it (rather than
      // always the whole thing) keeps seeded attempts from all repeating
      // its exact tail identically.
      const std::vector<Mv>* seedPtr=nullptr; size_t seedLen=0; std::vector<Mv> seedCopy;
      if(k>=4 && k%3!=0){
         EnterCriticalSection(&S->cs);
         if(S->bestPotential>=0) seedCopy=S->bestPrefix;
         LeaveCriticalSection(&S->cs);
         if(!seedCopy.empty()){
            uint64_t r=0x9E3779B97F4A7C15ULL ^ ((uint64_t)(k+1)*0xBF58476D1CE4E5B9ULL);
            for(int i=0;i<4;i++) nextRand(r);
            size_t full=seedCopy.size(), minLen=full/2;
            seedLen = minLen + (size_t)(nextRand(r)%(full-minLen+1));
            seedPtr=&seedCopy;
         }
      }
      AttemptOutcome out=runAttempt(*S->start,S->freeMode,k,budgetForAttempt(k),*S->P,stop,seedPtr,seedLen);
      S->P->attemptsFinished.fetch_add(1);
      if(!out.solved){
         if(out.lastPot>=0 && !out.lastPath.empty()){
            EnterCriticalSection(&S->cs);
            if(out.lastPot>S->bestPotential){
               S->bestPotential=out.lastPot; S->bestPrefix=out.lastPath;
               S->P->bestPrefixLen.store((int)out.lastPath.size(),std::memory_order_relaxed);
            }
            LeaveCriticalSection(&S->cs);
         }
         continue;
      }
      // The depth-first line usually wanders; cut the detours out of it first.
      std::vector<Mv> cand=out.moves;
      ULONGLONG limit=GetTickCount64()+90000;
      shortenLine(*S->start,S->freeMode,cand,[&]{ return S->cancel->load() || GetTickCount64()>limit; });
      if(!validateLine(*S->start,S->freeMode,cand,nullptr)){
         cand=out.moves;
         if(!validateLine(*S->start,S->freeMode,cand,nullptr)) continue;
      }
      EnterCriticalSection(&S->cs);
      if(!S->haveBest.load() || cand.size()<S->best.size()){
         S->best=cand;
         if(!S->haveBest.load()){ S->firstFound=GetTickCount64(); S->haveBest.store(true); }
      }
      if(S->best.size()<=GOOD_ENOUGH_LINE) S->done.store(true);
      LeaveCriticalSection(&S->cs);
   }
   return 0;
}

inline int defaultThreads(){
   SYSTEM_INFO si; GetSystemInfo(&si); int c=(int)si.dwNumberOfProcessors;
   return std::max(1,std::min(8,c-2));
}

// Searches `start` (the table right after the deal, reserve complete) until a
// solution is found, `cancel` is raised or GetTickCount64() passes `deadline`.
// Blocking; call it from a worker thread of your own. Attempt numbering
// continues across calls that share the same Progress, so "keep searching"
// simply calls it again with a later deadline.
//
// `resumePrefix` (optional): a line from an earlier, unsuccessful call's
// Result::bestEffort. When given, attempts start building on it right away
// instead of only picking up shared progress once some attempt within THIS
// call has reported one — so a resumed search continues from where the
// previous one left off rather than re-deriving that ground from scratch.
inline Result solveDeal(const GameState& start, bool freeMode, Progress& P,
                        const std::atomic<bool>& cancel, ULONGLONG deadline, int threads,
                        const std::vector<Mv>* resumePrefix=nullptr){
   Result res;
   Shared S; S.start=&start; S.freeMode=freeMode; S.P=&P; S.cancel=&cancel; S.deadline=deadline;
   InitializeCriticalSection(&S.cs);
   if(resumePrefix && !resumePrefix->empty()){
      S.bestPrefix=*resumePrefix;
      Board B; B.g=start.boardOnly(); B.g.reserve=start.reserve; B.rpos=0; B.freeMode=freeMode;
      for(const Mv& mv: S.bestPrefix) B.apply(mv);
      S.bestPotential=solverPotential(B.g);
      P.bestPrefixLen.store((int)S.bestPrefix.size(),std::memory_order_relaxed);
   }
   if(threads<1) threads=1;
   P.threads.store(threads);
   std::vector<HANDLE> hs;
   for(int t=1;t<threads;t++){ HANDLE h=CreateThread(nullptr,0,workerProc,&S,0,nullptr); if(h) hs.push_back(h); }
   workerProc(&S);                       // this thread works too
   if(!hs.empty()){ WaitForMultipleObjects((DWORD)hs.size(),hs.data(),TRUE,INFINITE); for(HANDLE h:hs) CloseHandle(h); }
   if(S.haveBest.load()){
      res.moves=S.best;
      res.solved=validateLine(start,freeMode,res.moves,nullptr);
   } else {
      res.cancelled=cancel.load();
      res.timedOut=!res.cancelled && GetTickCount64()>=deadline;
   }
   res.bestEffort=S.bestPrefix;
   res.bestPotential=S.bestPotential;
   DeleteCriticalSection(&S.cs);
   return res;
}
} // namespace solver
