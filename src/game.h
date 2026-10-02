#pragma once
// Developed with AI assistance (Claude, Anthropic) — see README.md.
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <set>
#include <random>
#include <unordered_map>
#include <cstdint>
#include <array>
#include <atomic>
#include <functional>

enum Suit { Hearts=0, Diamonds=1, Clubs=2, Spades=3 };
enum Rank { Ace=1,Two,Three,Four,Five,Six,Seven,Eight,Nine,Ten,Jack,Queen,King };

// Deterministic, fully portable Fisher-Yates shuffle: same seed -> same
// permutation on EVERY compiler/standard-library/machine, forever.
//
// std::mt19937 itself is bit-for-bit standardized (same seed -> same raw
// 32-bit output sequence everywhere), but std::shuffle is NOT: the standard
// leaves the exact algorithm that turns the engine's raw output into a
// bounded random index up to the implementation (via uniform_int_distribution,
// whose own internal algorithm also isn't pinned down). libstdc++, libc++ and
// MSVC's STL each do this differently, so std::shuffle(deck, mt19937(seed))
// used to produce a DIFFERENT deal for the same seed depending on which
// compiler/standard library built the exe — this is exactly what happened
// between two machines running builds made by different toolchains (found by
// the user and their sister entering the same game number and getting two
// different tables). boundedRand()/deterministicShuffle() below replace both
// std::shuffle and uniform_int_distribution with hand-written code that only
// ever calls mt19937::operator()() and does its own (unbiased, rejection-
// sampling) reduction to a bounded index — nothing here is implementation-
// defined, so the result is pinned down for good.
//
// NOTE: this redefines what every existing game NUMBER deals compared to any
// exe built before this change — a save FILE (.dat) is unaffected, since it
// stores the dealt cards directly rather than re-deriving them from the seed.
inline uint32_t boundedRand(std::mt19937& gen, uint32_t bound){
   uint32_t threshold=(0u-bound)%bound; // largest value where r%bound would be biased; reject below it
   for(;;){ uint32_t r=gen(); if(r>=threshold) return r%bound; }
}
template<class T>
inline void deterministicShuffle(std::vector<T>& deck, std::mt19937& gen){
   for(size_t i=deck.size();i>1;){
      --i;
      uint32_t j=boundedRand(gen,(uint32_t)(i+1));
      std::swap(deck[i],deck[j]);
   }
}

struct Card {
   Suit suit; Rank rank;
   bool isRed() const { return suit==Hearts||suit==Diamonds; }
   std::string imgKey() const {
      const char* R[]={"","A","2","3","4","5","6","7","8","9","T","J","Q","K"};
      const char  S[]="HDCS";
      return std::string(R[rank])+S[suit];
   }
   bool operator==(const Card& o) const { return suit==o.suit&&rank==o.rank; }
   // Card "type" 0..51 — identifies suit+rank only, deliberately ignoring which
   // of the two decks a card came from (2 decks = 2 physical copies can share
   // a type). "4 kier z talii 1 to taka sama karta jak 4 kier z talii 2."
   int typeId() const { return (int)suit*13 + ((int)rank-1); }
};

static const int NUM_COLS=10, NUM_FOUND=8;

// Which physical deck (0 or 1) each card of the INITIAL reserve came from, for
// a deal made from `seed` — only used to pick the matching card-back colour on
// the reserve pile (the one place a back is ever drawn). Cards don't carry deck
// identity (see Card::typeId), but it's fully recoverable: GameState::newGame()
// builds the deck deck-1-then-deck-2 and shuffles it with deterministicShuffle(),
// so shuffling a parallel array of deck numbers with the same seed moves each
// tag to exactly where its card lands. The reserve only ever shrinks from the
// front (a deal) and grows back by undoing one, so it is always a suffix of this
// list: the next card to be dealt is tags[tags.size()-reserve.size()].
inline std::vector<uint8_t> reserveDeckTagsForSeed(unsigned int seed){
   std::vector<uint8_t> tags(104);
   for(int i=0;i<104;i++) tags[i]=(uint8_t)(i/52);
   std::mt19937 gen(seed);
   deterministicShuffle(tags,gen);
   return std::vector<uint8_t>(tags.begin()+NUM_COLS*(NUM_COLS+1)/2,tags.end());
}
enum LocType { LOC_COLUMN, LOC_FOUNDATION };

struct MoveHint {
   bool valid=false;
   LocType fromType=LOC_COLUMN, toType=LOC_COLUMN;
   int fromIdx=0, fromCard=0, toIdx=0;
   std::wstring desc;
};

struct Snapshot {
   std::vector<Card> cols[NUM_COLS];
   std::vector<Card> found[NUM_FOUND];
   std::vector<Card> reserve;
   bool isDealBoundary=false; // true if the move right after this snapshot was a reserve deal
};

// ============================================================================
// AI engine support types — forward-declared here, implemented as free
// functions AFTER the GameState struct (they need its full definition), but
// referenced from GameState's own inline methods below. See the block right
// after the closing brace of GameState for the actual implementations and
// design notes (Zobrist hashing/canonicalization, the 15-rule move-scoring
// table, and the search).
// ============================================================================
struct GameState;
static uint64_t boardCanonicalHash(const GameState& g);
static bool     isTrivialBoard(const GameState& g);
static int      countEmptyCols(const GameState& g);
static int      dealWillOccupyCount(const GameState& g);
static bool     isGlobalLowestRank(const GameState& g, Rank r);
static int      revealBonus(const GameState& g, int fc, int ci);
static int      scoreMove(const GameState& before, const MoveHint& mv,
                           const std::array<int,NUM_COLS>* artificialSince=nullptr, int curPly=0);

// One fully-searched root candidate, as produced by GameState::getRankedMoves():
// `move` itself, its own rule-table score (`score` — what getBestMove's
// outScore returns for it, used e.g. for the hint score>0 filter), and the
// engine's planned continuation AFTER `move` (`pv`, does not include `move`).
// Computing the whole sorted list once and letting callers walk it (instead
// of re-searching from scratch for every history-exclusion retry) is what
// keeps doAutoMove()'s/performHintNow()'s retry loops cheap.
struct RankedMove {
   MoveHint move;
   int score = 0;
   std::vector<MoveHint> pv;
};

struct GameState {
   std::vector<Card> cols[NUM_COLS];
   std::vector<Card> found[NUM_FOUND];
   std::vector<Card> reserve;
   std::deque<Snapshot> undoStack;
   std::deque<Snapshot> redoStack;
   Snapshot initialDeal; // board state right after dealing, for "try again" replay

   // seed: caller-supplied so a deal can be reproduced later from its
   // "game number" — the caller decides whether that seed is a random one
   // it just generated, or a number the player typed in.
   void newGame(unsigned int seed) {
      for(auto& c:cols)  c.clear();
      for(auto& f:found) f.clear();
      reserve.clear(); undoStack.clear(); redoStack.clear();
      std::vector<Card> deck;
      for(int d=0;d<2;d++) for(int s=0;s<4;s++) for(int r=1;r<=13;r++)
         deck.push_back({(Suit)s,(Rank)r});
      std::mt19937 gen(seed);
      deterministicShuffle(deck,gen);
      int idx=0;
      for(int col=0;col<NUM_COLS;col++)
         for(int k=0;k<NUM_COLS-col;k++) cols[col].push_back(deck[idx++]);
      for(;idx<(int)deck.size();idx++) reserve.push_back(deck[idx]);
      // Remember this exact deal so "try again" can replay it later
      for(int i=0;i<NUM_COLS;i++)  initialDeal.cols[i]=cols[i];
      for(int i=0;i<NUM_FOUND;i++) initialDeal.found[i].clear();
      initialDeal.reserve=reserve;
   }

   // Restore the board to exactly how it looked right after the last deal —
   // used by "try again" on the no-moves dialog, instead of dealing a brand
   // new random deck.
   void replayDeal() {
      for(int i=0;i<NUM_COLS;i++)  cols[i]=initialDeal.cols[i];
      for(int i=0;i<NUM_FOUND;i++) found[i]=initialDeal.found[i];
      reserve=initialDeal.reserve;
      undoStack.clear(); redoStack.clear();
   }

   static const int MAX_UNDO_HISTORY = 1000; // long enough for a solver replay (see solver.h / Solved*.dat)

   void saveUndo(bool isDeal=false) {
      Snapshot s;
      for(int i=0;i<NUM_COLS;i++)  s.cols[i]=cols[i];
      for(int i=0;i<NUM_FOUND;i++) s.found[i]=found[i];
      s.reserve=reserve;
      s.isDealBoundary=isDeal;
      undoStack.push_back(s);
      if((int)undoStack.size()>MAX_UNDO_HISTORY) undoStack.pop_front();
      redoStack.clear(); // new move clears redo
   }

   bool undo() {
      if(undoStack.empty()) return false;
      // Save current state to redo
      Snapshot cur;
      for(int i=0;i<NUM_COLS;i++)  cur.cols[i]=cols[i];
      for(int i=0;i<NUM_FOUND;i++) cur.found[i]=found[i];
      cur.reserve=reserve;
      // Carry the deal-boundary tag forward so redo can still find it later
      cur.isDealBoundary=undoStack.back().isDealBoundary;
      redoStack.push_back(cur);
      if((int)redoStack.size()>MAX_UNDO_HISTORY) redoStack.pop_front();
      // Restore
      auto& s=undoStack.back();
      for(int i=0;i<NUM_COLS;i++)  cols[i]=s.cols[i];
      for(int i=0;i<NUM_FOUND;i++) found[i]=s.found[i];
      reserve=s.reserve;
      undoStack.pop_back();
      return true;
   }

   bool redo() {
      if(redoStack.empty()) return false;
      // Save current to undo
      Snapshot cur;
      for(int i=0;i<NUM_COLS;i++)  cur.cols[i]=cols[i];
      for(int i=0;i<NUM_FOUND;i++) cur.found[i]=found[i];
      cur.reserve=reserve;
      cur.isDealBoundary=redoStack.back().isDealBoundary;
      undoStack.push_back(cur);
      if((int)undoStack.size()>MAX_UNDO_HISTORY) undoStack.pop_front();
      // Restore redo
      auto& s=redoStack.back();
      for(int i=0;i<NUM_COLS;i++)  cols[i]=s.cols[i];
      for(int i=0;i<NUM_FOUND;i++) found[i]=s.found[i];
      reserve=s.reserve;
      redoStack.pop_back();
      return true;
   }

   // Undo repeatedly until (and including) the last reserve deal is undone.
   bool undoToDeal() {
      bool did=false;
      while(!undoStack.empty()){
         bool wasDeal=undoStack.back().isDealBoundary;
         if(!undo()) break;
         did=true;
         if(wasDeal) return true;
      }
      return did;
   }
   // Redo repeatedly until (and including) the next reserve deal is redone.
   bool redoToDeal() {
      bool did=false;
      while(!redoStack.empty()){
         bool wasDeal=redoStack.back().isDealBoundary;
         if(!redo()) break;
         did=true;
         if(wasDeal) return true;
      }
      return did;
   }

   bool validSeqFrom(int col, int si) const {
      const auto& c=cols[col]; int n=c.size();
      if(si<0||si>=(int)n) return false;
      for(int i=si;i<n-1;i++) {
         if((int)c[i].rank!=(int)c[i+1].rank+1) return false;
         if(c[i].isRed()==c[i+1].isRed()) return false;
      }
      return true;
   }

   int seqLenFromBottom(int col) const {
      const auto& c=cols[col]; int n=c.size(); if(!n) return 0;
      int len=1;
      for(int i=n-2;i>=0;i--) {
         if((int)c[i].rank!=(int)c[i+1].rank+1) break;
         if(c[i].isRed()==c[i+1].isRed()) break;
         len++;
      }
      return len;
   }

   // Free column mode: any card/sequence can go on empty column
   // Set by g_freeColMode in main.cpp; accessed via this static pointer
   static bool* s_freeColMode;

   // EXPERIMENTAL (candidate fix #1, being trialled): real, cross-real-move
   // persistent counterpart to scoreMove()'s per-search-line `artificialSince`
   // tracking (see the long comment above scoreMove()/nextArtificialSince()).
   // That tracking resets to "nothing tracked" at the start of EVERY fresh
   // rankCandidates() call, so a column parked and freed again several REAL,
   // independently-decided moves later looks exactly like a long-standing,
   // genuinely free column each time — getting rule 3's full, undiscounted
   // +500 reward on every such round trip, even though the whole point was to
   // discount exactly this. main.cpp maintains these across real AUTOMATIC
   // moves only (reset — like its anti-loop history — on any manual
   // interaction) and wires them here; null when unwired (this file's own
   // test harnesses, the batch simulator), which reproduces the exact prior
   // behaviour (every search line starts untracked).
   static std::array<int,NUM_COLS>* s_realArtificialSince;
   static int* s_realMoveCounter;

   // EXPERIMENTAL (candidate fix #2, being trialled alongside #1): real,
   // cross-real-move "foundation retrieval cooldown" for rule 14 — the same
   // idea as s_realArtificialSince above, mirrored onto CARDS instead of
   // COLUMNS: `s_realFoundSentAt[cardIndex(c)]` holds the real-move number
   // at which card `c` was last sent to a foundation, so scoreMove can
   // penalize retrieving it again shortly afterward much more than the flat
   // -50 rule 14 always charges. Shares s_realMoveCounter above.
   static std::array<int,56>* s_realFoundSentAt;
   static int cardIndex(const Card& c){ return (int)c.suit*14+(int)c.rank; }

   // User-configurable base search depth, per mode (Settings dialog; see
   // main.cpp's g_searchDepthFree/g_searchDepthKing). Defaults (when unwired,
   // e.g. this file's own test harnesses): 4 for free-column mode, 5 for
   // king-only mode. See rankCandidates() for how each mode turns its base
   // into the actual depth used for a given board.
   static int* s_searchDepthFree;
   static int* s_searchDepthKing;

   bool canDropOnCol(const Card& head, int tc) const {
      if(cols[tc].empty()){
         if(s_freeColMode && *s_freeColMode) return true;  // any card on empty
         return head.rank==King;
      }
      const Card& b=cols[tc].back();
      return (int)b.rank==(int)head.rank+1 && b.isRed()!=head.isRed();
   }

   bool canDropOnFound(const Card& c, int fi) const {
      if(found[fi].empty()){
         if(c.rank!=Ace) return false;
         // Empty slot only accepts Ace of the preferred suit
         int fa,fb; preferredSlots(c.suit,fa,fb);
         return fi==fa||fi==fb;
      }
      const Card& t=found[fi].back();
      return t.suit==c.suit&&(int)c.rank==(int)t.rank+1;
   }

   // Preferred foundation slots: Spades=0,1  Hearts=2,3  Diamonds=4,5  Clubs=6,7
   static void preferredSlots(Suit s, int& fa, int& fb) {
      if(s==Spades)        { fa=0; fb=1; }
      else if(s==Hearts)   { fa=2; fb=3; }
      else if(s==Diamonds) { fa=4; fb=5; }
      else                 { fa=6; fb=7; }
   }

   // Returns the best foundation index for card c (-1 if none valid).
   int bestFoundSlot(const Card& c) const {
      int fa, fb;
      preferredSlots(c.suit, fa, fb);
      // 1. Preferred slot with same suit continuing sequence
      if(canDropOnFound(c,fa) && !found[fa].empty()) return fa;
      if(canDropOnFound(c,fb) && !found[fb].empty()) return fb;
      // 2. Preferred empty slot for Ace
      if(c.rank==Ace) {
         if(found[fa].empty()) return fa;
         if(found[fb].empty()) return fb;
      }
      // 3. Any matching suit
      for(int f=0;f<NUM_FOUND;f++)
         if(!found[f].empty() && canDropOnFound(c,f)) return f;
      // 4. Any empty slot for Ace
      if(c.rank==Ace)
         for(int f=0;f<NUM_FOUND;f++)
            if(found[f].empty()) return f;
      return -1;
   }


   // Preferred foundation slots per suit:
   //   Spades:   0, 1  |  Hearts:   2, 3
   //   Diamonds: 4, 5  |  Clubs:    6, 7

   // Best foundation slot for card c, respecting preferred positions.
   // Returns -1 if no valid slot exists.

   // Preferred foundation slots per suit:
   //   Spades:   0, 1
   //   Hearts:   2, 3
   //   Diamonds: 4, 5
   //   Clubs:    6, 7

   // Find the best foundation slot for card c, respecting suit preferences.
   // Returns -1 if no slot accepts the card.
   // Priority:
   //   1. Preferred slot that already has this suit (can continue the sequence)
   //   2. Preferred empty slot (start a new sequence in the right place)
   //   3. Any slot that accepts the card (fallback)

   bool skipOnDeal(int col) const {
      // In free mode: never skip — deal to every column including king-sequence ones
      if(s_freeColMode && *s_freeColMode) return false;
      // In king-only mode: skip columns with a complete king-topped sequence
      if(cols[col].empty()) return false;
      if(cols[col][0].rank!=King) return false;
      return validSeqFrom(col,0);
   }

   std::vector<std::pair<int,Card>> dealReserve() {
      std::vector<std::pair<int,Card>> placed;
      int ri=0;
      for(int col=0;col<NUM_COLS&&ri<(int)reserve.size();col++) {
         if(skipOnDeal(col)) continue;
         placed.push_back({col,reserve[ri]});
         cols[col].push_back(reserve[ri]); ri++;
      }
      reserve.erase(reserve.begin(),reserve.begin()+ri);
      return placed;
   }

   bool checkWin() const {
      int kings=0;
      for(int col=0;col<NUM_COLS;col++)
         if(!cols[col].empty()&&cols[col][0].rank==King&&validSeqFrom(col,0)) kings++;
      for(int f=0;f<NUM_FOUND;f++)
         if(!found[f].empty()&&found[f].back().rank==King) kings++;
      return kings>=8;
   }

   // Any table or foundation move possible (ignores reserve)
   bool hasTableOrFoundationMove() const {
      for(int fc=0;fc<NUM_COLS;fc++) {
         if(cols[fc].empty()) continue;
         int sl=seqLenFromBottom(fc),si=(int)cols[fc].size()-sl;
         for(int p=0;p<sl;p++) {
            int ci=si+p;
            const Card& h=cols[fc][ci];
            for(int tc=0;tc<NUM_COLS;tc++) {
               if(tc==fc) continue;
               if(!canDropOnCol(h,tc)) continue;
               if(isPointlessColToCol(fc,ci,tc)) continue;
               return true;
            }
         }
         const Card& b=cols[fc].back();
         for(int f=0;f<NUM_FOUND;f++) if(canDropOnFound(b,f)) return true;
      }
      return false;
   }

   bool hasAnyMove() const {
      return hasTableOrFoundationMove() || !reserve.empty();
   }



   // -----------------------------------------------------------------------
   // Move quality checks
   // -----------------------------------------------------------------------

   // Is head (at fc[ci]) already sitting correctly on the card directly
   // above it in the same column (fc[ci-1])?
   // "Correctly" = ci-1 is one rank higher and opposite colour.
   bool alreadySittingCorrectly(int fc, int ci) const {
      if(ci == 0) return false;
      const Card& head  = cols[fc][ci];
      const Card& above = cols[fc][ci-1];
      return (int)above.rank == (int)head.rank+1 && above.isRed() != head.isRed();
   }

   // Is moving cols[fc][ci..end] to column tc pointless?
   // Minimal pointless check for user-initiated clicks:
   // only block if head would land on the exact same card it already sits on
   // (same suit+rank) AND the move uncovers nothing new below.
   bool isStrictlyPointless(int fc, int ci, int tc) const {
      const Card& head=cols[fc][ci];
      // Block if the card above head in source col is identical to target top,
      // but only when ci is at the very bottom of the visible sequence (ci==0
      // means nothing is uncovered; ci==seqStart means the whole sequence moves
      // and the card at ci-1 is already exposed).
      // Simpler rule: if moving exposes a NEW card (ci > 0 and ci is the start
      // of the sequence being moved, so cols[fc][ci-1] was under the sequence),
      // the move is never strictly pointless.
      if(ci>0 && !cols[tc].empty()){
         // Would land on the same card it sits on now — pointless only if
         // nothing new is uncovered (ci == 0 is handled above; if ci>0 the
         // card at ci-1 gets exposed, which is always potentially useful).
         // Exception: if ci-1 is already the bottom of another valid sequence
         // above ci, uncovering it adds no value — but that's too complex to
         // check here; we accept the occasional harmless move through.
         if(cols[fc][ci-1]==cols[tc].back()){
            // Only pointless if ci is 0 (nothing below to uncover).
            // If ci > 0 something gets uncovered — allow it.
            if(ci==0) return true;
            // ci > 0: card at ci-1 gets uncovered. Allow the move.
         }
      }
      if(head.rank==King && ci==0 && cols[tc].empty()){
         if(!s_freeColMode || !*s_freeColMode) return true;
      }
      return false;
   }

   bool isPointlessColToCol(int fc, int ci, int tc) const {
      const Card& head = cols[fc][ci];

      // Pointless case A: head moves to the EXACT same card it already sits on.
      if(ci > 0 && !cols[tc].empty())
         if(cols[fc][ci-1] == cols[tc].back()) return true;

      // Pointless case B: head already sits correctly on the card above it in fc
      // AND would sit in the SAME relationship on tc.back().
      // "Same relationship" = tc.back() has the same rank AND same colour as above,
      // so swapping produces no new information.
      // Example: 2♥ on 3♣ → moving to 3♠ is pointless (both black 3s, same rank+colour).
      if(ci > 0 && !cols[tc].empty() && alreadySittingCorrectly(fc, ci)) {
         const Card& above  = cols[fc][ci-1];
         const Card& tcBot  = cols[tc].back();
         // Same rank and same colour-class (red/black) = equivalent relationship
         if(above.rank == tcBot.rank && above.isRed() == tcBot.isRed()) {
            // Exception: even though the two "parent" cards look
            // interchangeable, WHICH specific card ends up exposed
            // underneath still matters. If moving away exposes a card that
            // can go straight to a foundation, that's real progress — e.g.
            // splitting a 7-6-5♣-4-3 run to park "4-3" on a black 5 (any
            // black 5) frees 5♣ specifically, and only 5♣ (not some other
            // black 5) can follow a 4♣ already on the foundation.
            int f=bestFoundSlot(above);
            bool freesToFoundation = (f>=0 && canDropOnFound(above,f));
            if(!freesToFoundation) return true;
         }
      }

      // Pointless case C: king at top of column to empty col (only in King-only mode)
      // In free mode, moving any card to empty is meaningful
      if(head.rank == King && ci == 0 && cols[tc].empty()){
         if(!s_freeColMode || !*s_freeColMode) return true;
      }

      return false;
   }

   // Does column fc have any non-pointless table move?
   bool hasTableMovesFrom(int fc) const {
      if(cols[fc].empty()) return false;
      int sl=seqLenFromBottom(fc);
      int si=(int)cols[fc].size()-sl;
      for(int part=0;part<sl;part++){
         int ci=si+part;
         const Card& head=cols[fc][ci];
         for(int tc=0;tc<NUM_COLS;tc++){
            if(tc==fc) continue;
            if(!canDropOnCol(head,tc)) continue;
            if(isPointlessColToCol(fc,ci,tc)) continue;
            return true;
         }
      }
      return false;
   }

   // Does ANY column have a non-pointless table move?
   bool hasAnyTableMove() const {
      for(int fc=0;fc<NUM_COLS;fc++)
         if(hasTableMovesFrom(fc)) return true;
      return false;
   }

   // ── move ranking (used by hint + auto-move so they pick the BEST legal
   //    move, not just the first one found) ──────────────────────────────────

   // Lightweight copy of just the board — no undo/redo history — for cheap
   // throwaway simulation when evaluating candidate moves.
   GameState boardOnly() const {
      GameState g;
      for(int i=0;i<NUM_COLS;i++)  g.cols[i]=cols[i];
      for(int i=0;i<NUM_FOUND;i++) g.found[i]=found[i];
      g.reserve=reserve;
      return g;
   }

   // Applies a single column→column or column→foundation move directly to
   // *this (mutates in place). Used only on throwaway boardOnly() clones
   // during evaluation — never on the live game state.
   bool applyHint(const MoveHint& h){
      if(!h.valid) return false;
      if(h.fromType==LOC_COLUMN && h.toType==LOC_COLUMN){
         int fc=h.fromIdx, tc=h.toIdx, ci=h.fromCard;
         if(fc<0||fc>=NUM_COLS||tc<0||tc>=NUM_COLS) return false;
         if(ci<0||ci>=(int)cols[fc].size()) return false;
         std::vector<Card> moving(cols[fc].begin()+ci,cols[fc].end());
         for(auto& c:moving) cols[tc].push_back(c);
         cols[fc].erase(cols[fc].begin()+ci,cols[fc].end());
         return true;
      }
      if(h.fromType==LOC_COLUMN && h.toType==LOC_FOUNDATION){
         int fc=h.fromIdx, ci=h.fromCard, f=h.toIdx;
         if(fc<0||fc>=NUM_COLS||f<0||f>=NUM_FOUND) return false;
         if(ci!=(int)cols[fc].size()-1) return false; // only the bottom card
         found[f].push_back(cols[fc][ci]);
         cols[fc].pop_back();
         return true;
      }
      if(h.fromType==LOC_FOUNDATION && h.toType==LOC_COLUMN){
         int f=h.fromIdx, tc=h.toIdx;
         if(f<0||f>=NUM_FOUND||tc<0||tc>=NUM_COLS) return false;
         if(found[f].empty()) return false;
         Card c=found[f].back();
         if(!canDropOnCol(c,tc)) return false;
         cols[tc].push_back(c);
         found[f].pop_back();
         return true;
      }
      if(h.fromIdx==-1){ // deal from reserve
         if(reserve.empty()) return false;
         dealReserve();
         return true;
      }
      return false;
   }

   // Repeatedly plays any card that can legally go straight to a foundation —
   // these are always safe (never block a future move) — until none remain.
   // Used to look a little further ahead: a move that doesn't win outright
   // may still unlock a cascade of "free" foundation moves that does.
   void autoCompleteFoundations(){
      bool progress=true;
      while(progress){
         progress=false;
         for(int col=0;col<NUM_COLS;col++){
            if(cols[col].empty()) continue;
            const Card& b=cols[col].back();
            int f=bestFoundSlot(b);
            if(f>=0 && canDropOnFound(b,f)){
               found[f].push_back(b);
               cols[col].pop_back();
               progress=true;
            }
         }
      }
   }


   static std::wstring cardName(const Card& c) {
      const wchar_t* R[]={L"",L"A",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"10",L"J",L"Q",L"K"};
      const wchar_t* S[]={L"♥",L"♦",L"♣",L"♠"};
      return std::wstring(R[c.rank])+S[c.suit];
   }

   // ── New AI: empty-column occupation gate ───────────────────────────────
   // Occupying a free column is only worth its steep -500 cost (rule 15) when
   // it actually buys something. Allowed only when ci>0 (something really
   // gets revealed in the source column — ci==0 means the WHOLE column moves
   // to another empty one, a pure wash) and at least one of:
   //   a) the revealed card was NOT already correctly sequenced with what
   //      just moved away (so this really is a fresh reveal, not a
   //      cosmetic split of one long valid run);
   //   b) the revealed card can go straight to a foundation, which would
   //      raise the total card count across all foundations;
   //   c) some other column's currently-movable head/sequence could land on
   //      the revealed card on the very next move.
   bool emptyColumnOccupyAllowed(int fc, int ci) const {
      // King-only mode (user-requested): none of the conditions below are
      // there to protect — only a King can EVER occupy an empty column in
      // this mode, so there's no "wasting a valuable free slot on a cosmetic
      // move" risk to gate against the way there is in free mode, where any
      // card could park there. Always allow it here.
      if(s_freeColMode && !*s_freeColMode) return true;
      if(ci<=0) return false; // nothing is revealed — never worth it
      const Card& revealed = cols[fc][ci-1];
      if(!alreadySittingCorrectly(fc,ci)) return true;                 // (a)
      int rf=bestFoundSlot(revealed);
      if(rf>=0 && canDropOnFound(revealed,rf)) return true;            // (b)
      for(int oc=0;oc<NUM_COLS;oc++){                                  // (c)
         if(oc==fc || cols[oc].empty()) continue;
         int sl=seqLenFromBottom(oc);
         int si=(int)cols[oc].size()-sl;
         for(int part=0;part<sl;part++){
            const Card& head=cols[oc][si+part];
            if((int)revealed.rank==(int)head.rank+1 && revealed.isRed()!=head.isRed())
               return true;
         }
      }
      return false;
   }

   // User-added rule: splitting an already-valid sequence — moving only part
   // of it and leaving a shorter, still-valid run behind at `fc` — is worth
   // doing ONLY when it immediately buys foundation progress. Otherwise it's
   // exactly the "carry a card somewhere just to relocate it again" pattern
   // that works against the secondary goal of winning in as few moves as
   // possible (rebuilding a sequence purely to rebuild it doesn't shorten the
   // game). Allowed only when both hold:
   //   a) removing the split-off portion reveals a card that can go straight
   //      to a foundation RIGHT NOW;
   //   b) the card the split-off portion would land ON cannot itself go
   //      straight to a foundation right now — if it could, that card should
   //      just go to the foundation directly instead of being built on. An
   //      EMPTY destination trivially satisfies this (there's no card there
   //      to send anywhere), so it never blocks a move on its own.
   // A move that is NOT a split (ci<=si: moving the whole valid run, or a
   // single card that wasn't part of one) is never restricted by this rule.
   bool sequenceSplitAllowed(int fc, int ci, int tc) const {
      int sl=seqLenFromBottom(fc), si=(int)cols[fc].size()-sl;
      if(ci<=si) return true; // not a split at all
      const Card& revealed = cols[fc][ci-1];
      int rf=bestFoundSlot(revealed);
      if(!(rf>=0 && canDropOnFound(revealed,rf))) return false;   // (a)
      if(!cols[tc].empty()){
         const Card& dst = cols[tc].back();
         int df=bestFoundSlot(dst);
         if(df>=0 && canDropOnFound(dst,df)) return false;        // (b)
      }
      return true;
   }

   // User-added rule: moving cards to an empty column is only worth
   // considering when there's genuinely nowhere else for them to go right
   // now — otherwise take the direct move instead of parking the cards in
   // the empty column first and relocating them again later, which is
   // exactly the kind of detour that inflates the move count without
   // helping. Checks BOTH other options a move to an empty column could
   // have taken instead: landing on some other, non-empty column, or (for
   // the single bottom-most card of the moved portion only) going straight
   // to a foundation.
   //
   // King-only mode note (per user question): the "other column" loop below
   // is already always false for a King head there — canDropOnCol() only
   // ever accepts a King onto an EMPTY column in this mode, never onto a
   // non-empty one (no rank is "King+1"), so this check is naturally a
   // structural no-op for that mode without needing any change. The
   // foundation half below still matters in every mode (a King ready for
   // its foundation should go there instead of parking) and is left as is.
   bool hasOtherLegalDestination(int fc, int ci) const {
      const Card& head = cols[fc][ci];
      for(int oc=0; oc<NUM_COLS; oc++){
         if(oc==fc || cols[oc].empty()) continue;
         if(canDropOnCol(head,oc)) return true;
      }
      if(ci == (int)cols[fc].size()-1){ // only the bottommost card can go to a foundation
         int f = bestFoundSlot(head);
         if(f>=0 && canDropOnFound(head,f)) return true;
      }
      return false;
   }

   // Collects EVERY currently-legal move the new AI is willing to consider —
   // used both by the search below and (via getBestMove's 'seen' exclusion)
   // by the hint list. Column→column moves onto an EMPTY column are gated by
   // emptyColumnOccupyAllowed() instead of the older ad-hoc heuristics; every
   // other kind of move here is filtered only by ordinary legality, since the
   // scoring table (see scoreMove(), after this struct) is what judges
   // whether a legal move is actually any good, not the generator.
   std::vector<MoveHint> allCandidateMoves(const std::set<std::pair<int,int>>& seen={}, bool withDesc=false) const {
      std::vector<MoveHint> out;
      out.reserve(32);
      // fromType is folded into the key (column sources use their plain
      // index; foundation sources use -1000-f) so a column-sourced and a
      // foundation-sourced candidate can never collide just because they
      // happen to share a numeric fromIdx/fromCard — see moveExclusionKey()
      // below the GameState struct, which callers use to build this 'seen'
      // set from MoveHints they've already tried (an earlier mismatch here,
      // where callers plainly used h.fromIdx without this offset, silently
      // failed to exclude foundation-sourced moves and could spin an
      // auto-move retry loop forever on one).
      auto inSeen=[&](LocType fromType,int fromIdx,int fromCard,int toIdx){
         int fromKey = (fromType==LOC_COLUMN) ? fromIdx : (-1000-fromIdx);
         return seen.count({fromKey*100+fromCard,toIdx})>0;
      };
      // col → col (non-empty destination)
      for(int fc=0;fc<NUM_COLS;fc++){
         if(cols[fc].empty()) continue;
         int sl=seqLenFromBottom(fc);
         int si=(int)cols[fc].size()-sl;
         for(int part=0;part<sl;part++){
            int ci=si+part;
            const Card& head=cols[fc][ci];
            for(int tc=0;tc<NUM_COLS;tc++){
               if(tc==fc || cols[tc].empty()) continue;
               if(!canDropOnCol(head,tc)) continue;
               if(!sequenceSplitAllowed(fc,ci,tc)) continue;
               if(inSeen(LOC_COLUMN,fc,ci,tc)) continue;
               MoveHint h; h.valid=true; h.fromType=LOC_COLUMN; h.fromIdx=fc;
               h.fromCard=ci; h.toType=LOC_COLUMN; h.toIdx=tc;
               if(withDesc){
                  int cnt=(int)cols[fc].size()-ci;
                  h.desc=L"Przenieś "+std::to_wstring(cnt)+
                         L" kartę/karty: kol."+std::to_wstring(fc+1)+
                         L" → kol."+std::to_wstring(tc+1);
               }
               out.push_back(h);
            }
         }
      }
      // col → foundation
      for(int col=0;col<NUM_COLS;col++){
         if(cols[col].empty()) continue;
         const Card& b=cols[col].back();
         int ci=(int)cols[col].size()-1;
         int f=bestFoundSlot(b);
         if(f<0||!canDropOnFound(b,f)) continue;
         if(inSeen(LOC_COLUMN,col,ci,f)) continue;
         MoveHint h; h.valid=true; h.fromType=LOC_COLUMN; h.fromIdx=col;
         h.fromCard=ci; h.toType=LOC_FOUNDATION; h.toIdx=f;
         if(withDesc) h.desc=L"Przenieś "+cardName(b)+L" na stos";
         out.push_back(h);
      }
      // col → empty column, gated by emptyColumnOccupyAllowed()
      for(int fc=0;fc<NUM_COLS;fc++){
         if(cols[fc].empty()) continue;
         int sl=seqLenFromBottom(fc);
         int si=(int)cols[fc].size()-sl;
         for(int part=0;part<sl;part++){
            int ci=si+part;
            if(ci==0 && si==0) continue; // whole free column → another free one: no-op
            if(!emptyColumnOccupyAllowed(fc,ci)) continue;
            if(hasOtherLegalDestination(fc,ci)) continue;
            const Card& head=cols[fc][ci];
            for(int tc=0;tc<NUM_COLS;tc++){
               if(tc==fc || !cols[tc].empty()) continue;
               if(!canDropOnCol(head,tc)) continue;
               if(!sequenceSplitAllowed(fc,ci,tc)) continue;
               if(inSeen(LOC_COLUMN,fc,ci,tc)) continue;
               MoveHint h; h.valid=true; h.fromType=LOC_COLUMN; h.fromIdx=fc;
               h.fromCard=ci; h.toType=LOC_COLUMN; h.toIdx=tc;
               if(withDesc){
                  int cnt=(int)cols[fc].size()-ci;
                  h.desc=L"Przenieś "+std::to_wstring(cnt)+
                         L" kartę/karty: kol."+std::to_wstring(fc+1)+
                         L" → wolne miejsce";
               }
               out.push_back(h);
            }
         }
      }
      int hasTblFnd=-1; // lazily computed hasTableOrFoundationMove() (const for this whole call)
      // foundation → col: a temporary buffer move (rule 14 in the scoring
      // table always penalizes it; it earns its keep only when it unlocks a
      // follow-up move for some other column's head/sequence).
      for(int f=0;f<NUM_FOUND;f++){
         if(found[f].empty()) continue;
         const Card& c=found[f].back();
         for(int tc=0;tc<NUM_COLS;tc++){
            if(!canDropOnCol(c,tc)) continue;
            if(inSeen(LOC_FOUNDATION,f,0,tc)) continue;
            // User-added rule (mirrors the "no other legal move" check that
            // already gates column->empty-column moves): parking a
            // foundation card in an EMPTY column is only worth considering
            // when there's genuinely no other legal column->column or
            // column->foundation move already available on the table —
            // otherwise take the direct move instead of detouring a card
            // through the empty column first.
            if(cols[tc].empty()){
               if(hasTblFnd<0) hasTblFnd = hasTableOrFoundationMove() ? 1 : 0;
               if(hasTblFnd) continue;
            }
            bool unlocksSomething=false;
            for(int oc=0;oc<NUM_COLS && !unlocksSomething;oc++){
               if(oc==tc || cols[oc].empty()) continue;
               int sl=seqLenFromBottom(oc);
               int si=(int)cols[oc].size()-sl;
               for(int part=0;part<sl;part++){
                  const Card& head=cols[oc][si+part];
                  if((int)c.rank==(int)head.rank+1 && c.isRed()!=head.isRed()){
                     // The "unlock" only counts if landing `head` on `c` is
                     // actually the better thing for `head` to do — if `head`
                     // could just go straight to ITS OWN foundation right
                     // now, building it onto `c` instead is never actually
                     // going to happen (the search always prefers the direct
                     // foundation move), so this would be a promise the plan
                     // never keeps — exactly mirroring sequenceSplitAllowed's
                     // condition (b) for the column->column split case.
                     int hf=bestFoundSlot(head);
                     if(hf>=0 && canDropOnFound(head,hf)) continue;
                     unlocksSomething=true; break;
                  }
               }
            }
            if(!unlocksSomething) continue;
            MoveHint h; h.valid=true; h.fromType=LOC_FOUNDATION; h.fromIdx=f;
            h.fromCard=0; h.toType=LOC_COLUMN; h.toIdx=tc;
            if(withDesc) h.desc=L"Cofnij "+cardName(c)+L" ze stosu na stół";
            out.push_back(h);
         }
      }
      // Dealing the reserve is a real, competing option too.
      if(!reserve.empty() && !inSeen(LOC_COLUMN,-1,0,0)){
         MoveHint h; h.valid=true; h.fromType=LOC_COLUMN; h.fromIdx=-1;
         if(withDesc) h.desc=L"Dobierz karty z rezerwy ("+std::to_wstring(reserve.size())+L")";
         out.push_back(h);
      }
      return out;
   }

   // ── New AI: search entry point ──────────────────────────────────────────
   // Picks the single best legal move using a beam-limited, transposition-
   // table-backed search (see the free functions and searchValue() below for
   // the engine itself). Signature-compatible with the old getBestMove() so
   // every existing call site (background precompute thread, hint list,
   // auto-move) keeps working unchanged; outScore/outPlan are optional and
   // used by the callers that need them (hint filtering, plan continuity).
   MoveHint getBestMove(const std::set<std::pair<int,int>>& seen={},
                        int* outScore=nullptr, std::vector<MoveHint>* outPlan=nullptr) const;

   // Same search as getBestMove(), but returns the full sorted beam (best to
   // worst, up to topN entries) instead of just the winner. A caller that
   // needs to skip a handful of candidates for reasons the engine itself
   // knows nothing about (main.cpp's 20-move anti-repetition history) should
   // call this ONCE and walk the list, rather than calling getBestMove() again
   // for every exclusion — each call here is a full beam search, so repeating
   // it per attempt is what made the retry loops in main.cpp expensive.
   // `bypassEmptyColShortcut`, when true, skips rankCandidates()'s "3+ empty
   // columns" single-candidate hard rule (see its comment) so the caller gets
   // the FULL ranked beam even in that situation — used by main.cpp's hint
   // feature as a rare fallback (see performHintNow()) when that hard rule's
   // one candidate turns out not to be a "sensible" (score>0) move to hint,
   // and nothing else was offered to fall back to.
   // `abortFlag` (optional): when it becomes true mid-search the search unwinds
   // early and the (partial, meaningless) result must be discarded — used by
   // main.cpp's background thread once the position it is searching has been
   // superseded.
   std::vector<RankedMove> getRankedMoves(int topN=20, bool bypassEmptyColShortcut=false,
                                           const std::atomic<bool>* abortFlag=nullptr) const;
};

// Builds the exact key allCandidateMoves()'s 'seen' exclusion set expects for
// a given MoveHint. ANY caller that wants to exclude a specific already-tried
// move on a follow-up allCandidateMoves()/getBestMove() call (main.cpp's
// auto-move retry loop and hint-list builder both do this) MUST go through
// this helper rather than hand-rolling {h.fromIdx*100+h.fromCard, h.toIdx} —
// that naive encoding collides column-sourced and foundation-sourced moves
// that happen to share a numeric index, which silently fails to exclude
// foundation-buffer moves and can spin a retry loop forever re-offering the
// same one.
inline std::pair<int,int> moveExclusionKey(const MoveHint& h){
   int fromKey = (h.fromType==LOC_COLUMN) ? h.fromIdx : (-1000-h.fromIdx);
   return { fromKey*100 + h.fromCard, h.toIdx };
}

// ============================================================================
// AI engine — implementation
//
// Design summary (see the "Czas na poprawkę SI" spec this was built from):
//
//  * Zobrist hashing with canonicalization: boardCanonicalHash() combines a
//    per-column hash (order-SENSITIVE within a column, since card order in a
//    column matters) using XOR across the 10 columns. XOR is commutative, so
//    the result is automatically invariant to which physical column holds
//    which pile — two boards that differ only by column permutation hash
//    identically. The two "twin" foundation slots per suit are folded the
//    same way (XOR of the two slot hashes), and since a foundation pile must
//    always be its suit's cards in strict ascending order from the Ace, its
//    content is fully described by its SIZE alone — so twin-slot invariance
//    reduces to XOR-ing the two sizes' hashes. Card::typeId() never encodes
//    which of the 2 decks a card came from, so deck-invariance ("4 kier z
//    talii 1 to taka sama karta jak 4 kier z talii 2") falls out for free.
//    The Zobrist tables are seeded with a FIXED constant (never a random
//    device), which is what makes the whole search reproducible: undo an
//    automatic move and trigger it again, and the identical hash table drives
//    the identical search to the identical answer.
//
//  * The reserve is deliberately never hashed and never inspected by the
//    search. Its exact composition is fully recoverable from cols+found (2
//    decks = 8 copies of each rank; whatever isn't on the table or on a
//    foundation is in the reserve) — see isGlobalLowestRank() — but its
//    ORDER is never used anywhere in this file, per "Nie wolno do obliczeń i
//    przewidywań podglądać kolejności kart w rezerwie." A "deal from the
//    reserve" candidate is therefore always a search HORIZON: it is scored
//    (rule 11, or the trivial-board exception) but the search never expands
//    past it, since doing so would require pretending to know which card
//    lands on which column.
//
//  * "Improved Minimax" for a one-player game reduces to a best-first
//    accumulated-score search: there is no adversary to minimize against, so
//    every node just picks the legal move maximizing (its own move-score +
//    the best achievable continuation). searchBest() is that recursion, beam-
//    limited per ply (branching in this game is too wide for an exhaustive
//    4-8 ply search) and backed by a transposition table keyed on the
//    canonical hash (folded together with the remaining depth) so that the
//    many different move ORDERS that converge on the same canonical board —
//    extremely common in solitaire — are only evaluated once ("Move
//    Dependency / Locality": move ordering also tries moves touching the
//    same column as the last move first, both to strengthen the beam cut and
//    to raise the transposition table's hit rate).
//
//  * Search depth is 4 plies normally, 8 whenever the CURRENT board has any
//    empty column (per spec) — with a narrower beam at that depth to keep
//    the cost bounded. A hard node-budget safety valve (SearchCtx::nodeBudget)
//    guarantees the search always terminates quickly even in a pathological
//    worst case, rather than ever hanging the app.
//
//  * getBestMove() applies the exact 3-level tie-break the spec calls for at
//    the root: deepest cumulative score first, then the move's own immediate
//    score, then — if still tied — preferring whichever move reveals the
//    LOWER-ranked card. Everything here (hashing, ordering, tie-breaks) is
//    fully deterministic, so undoing an automatic move and repeating it
//    always reproduces the exact same choice.
// ============================================================================

// Rule 1 — winning the game. Scaled by how many moves it took to GET there
// (12000 / move-number, move-number counted from 1 for an immediately-
// winning move), instead of a flat bonus: a flat WIN_BONUS let the search
// prefer stalling for a few extra plies — e.g. parking a card in an empty
// column and then taking it right back out a couple of moves later, purely
// to bank rule 3's +500 "freed a slot" bonus again — over taking an already
// available win, since the flat bonus doesn't care when the win happens (and
// reaching it stops the search from collecting anything further, while NOT
// winning yet lets it keep farming that stall). Scaling down the reward for
// a later win removes that incentive and keeps the search aligned with the
// "win in the fewest moves" goal. Integer division is intentional — this is
// already a heuristic score, not a precise quantity.
static const int WIN_BONUS_BASE = 12000; // rule 1
static int winBonusForPly(int plyNumber){ return WIN_BONUS_BASE / plyNumber; }

// Rule 2 — reaching a "tidy" board (every column down to an empty slot, a
// single card, or one valid sequence — see isTrivialBoard()). Scaled the
// same way as rule 1: 4000 on the move that first reaches it, 2000 if it
// takes two moves, and so on — awarded once, exactly on the move that
// crosses from "not tidy" to "tidy" (never repeatedly for as long as the
// board simply stays tidy afterward).
static const int TIDY_BONUS_BASE = 4000; // rule 2 (free-column mode)
static const int TIDY_BONUS_KING_BASE = 2000; // rule 2, king-only mode (user-specified base, same /move-count scaling as rule 1)
static int tidyBonusForPly(int plyNumber){
   bool kingOnlyMode = (GameState::s_freeColMode && !*GameState::s_freeColMode);
   if(kingOnlyMode) return TIDY_BONUS_KING_BASE / plyNumber;
   return TIDY_BONUS_BASE / plyNumber;
}

namespace PasjansAI {
   // Fixed-seed Zobrist tables — NOT std::random_device. Determinism (same
   // board -> same hash -> same search result, run after run) is required by
   // the spec ("jeśli gracz cofnie ruch automatyczny... musi być to ten sam
   // ruch"), so the seed must never vary between runs.
   struct ZobristTables {
      uint64_t card[52];
      uint64_t foundSize[4][14];
      ZobristTables(){
         std::mt19937_64 rng(0x9E3779B97F4A7C15ULL);
         for(auto& v : card) v = rng();
         for(auto& row : foundSize) for(auto& v : row) v = rng();
      }
   };
   inline const ZobristTables& zobrist(){ static ZobristTables z; return z; }
}

// Order-sensitive combine (card order WITHIN a column matters).
static uint64_t columnCanonicalHash(const std::vector<Card>& col){
   uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
   for(const auto& c : col){
      h ^= PasjansAI::zobrist().card[c.typeId()];
      h *= 1099511628211ULL; // FNV prime
   }
   return h;
}

// Column-permutation invariant (XOR across columns) and twin-foundation-slot
// invariant (XOR of each suit's two slot hashes) board hash. Deck-invariant
// because Card::typeId() is. Deliberately excludes the reserve — see the
// design note above.
static uint64_t boardCanonicalHash(const GameState& g){
   uint64_t colsXor = 0;
   for(int c=0;c<NUM_COLS;c++) colsXor ^= columnCanonicalHash(g.cols[c]);
   uint64_t foundHash = 0;
   for(int s=0;s<4;s++){
      int fa,fb; GameState::preferredSlots((Suit)s, fa, fb);
      int sizeA=(int)g.found[fa].size(), sizeB=(int)g.found[fb].size();
      foundHash ^= PasjansAI::zobrist().foundSize[s][sizeA] ^ PasjansAI::zobrist().foundSize[s][sizeB];
   }
   return colsXor ^ foundHash;
}

static int countEmptyCols(const GameState& g){
   int n=0;
   for(int c=0;c<NUM_COLS;c++) if(g.cols[c].empty()) n++;
   return n;
}

// How many of the CURRENTLY-empty columns will actually receive a card if the
// reserve is dealt right now — a dry run of dealReserve()'s own loop (same
// column order, same skipOnDeal() calls, same one-card-per-non-skipped-column
// consumption) without mutating anything. Needed because dealReserve() does
// NOT treat an empty column as special: skipOnDeal() never skips one (see its
// own comment), so a deal genuinely OCCUPIES every empty column it reaches —
// it does not "maintain" them the way rule 4 describes. The only way a
// currently-empty column can end up still empty right after a deal is if the
// reserve itself runs out before the deal-loop reaches that column.
static int dealWillOccupyCount(const GameState& g){
   int occupied=0, ri=0;
   int rsize=(int)g.reserve.size();
   for(int col=0; col<NUM_COLS && ri<rsize; col++){
      if(g.skipOnDeal(col)) continue;
      if(g.cols[col].empty()) occupied++;
      ri++;
   }
   return occupied;
}

// "Reaching a state where every column has either an empty slot, a single
// card, or one valid sequence" — rule 2's trigger condition, and also the
// "trivial board" that makes dealing from the reserve free (no rule-11 point)
// per the additional rules.
static bool isTrivialBoard(const GameState& g){
   for(int c=0;c<NUM_COLS;c++){
      if(g.cols[c].empty()) continue;
      if(g.cols[c].size()==1) continue;
      if(g.validSeqFrom(c,0)) continue;
      return false;
   }
   return true;
}

// Rule 5's "lowest rank among all cards on the table AND in the reserve" —
// computed WITHOUT ever looking at cols or reserve, purely from what's
// already on the foundations, which is exactly what "Uwaga!" requires: with
// 2 decks there are exactly 8 copies of each rank; any rank with fewer than 8
// copies already on a foundation still has at least one copy somewhere in
// play (table or reserve — it doesn't matter which for this rule), and the
// smallest such rank is the target.
static bool isGlobalLowestRank(const GameState& g, Rank rank){
   // A foundation pile is always A,2,3,... of one suit, so the number of
   // copies of rank r already on foundations is just the number of piles
   // holding at least r cards — no need to walk every card.
   for(int r=(int)Ace; r<=(int)King; r++){
      int cnt=0;
      for(int f=0; f<NUM_FOUND; f++) if((int)g.found[f].size() >= r) cnt++;
      if(cnt < 8) return r == (int)rank;
   }
   return false; // all ranks complete — the game is already wonn
}

// Rules 6 / 9 / 10 / 16 — everything scored purely off THAT a card gets
// revealed in a column, and (for 6/9/10) WHICH card it is and WHAT is still
// buried beneath it. Called only when ci>0 (something really is revealed at
// before.cols[fc][ci-1]).
static int revealBonus(const GameState& g, int fc, int ci){
   // King-only mode retuning (user-specified): several of these sub-bonuses
   // use higher values in king-only mode; free-column mode keeps the
   // original values.
   bool kingOnlyMode = (GameState::s_freeColMode && !*GameState::s_freeColMode);
   // Rule 16 (user-added): a flat reward for revealing a card in a column at
   // all, independent of which card it turns out to be — additive with
   // rules 6/9/10 below, not a replacement for them.
   int score = 20;
   // Rule 6: the revealed card was not sitting correctly-sequenced with what
   // just moved away — i.e. this is a genuinely new/different reveal.
   if(!g.alreadySittingCorrectly(fc,ci)) score += 60;

   // Rule 9: buried cards (still under the newly-revealed top, i.e. indices
   // 0..ci-2) that are low-rank and thus "inaccessible" — summed, then
   // multiplied by how many such cards there are. "wśród 8 kart jest as i
   // dwójka. Wtedy (5+4)*2=18 punktów."
   int lowSum=0, lowCount=0;
   for(int i=0;i<ci-1;i++){
      switch(g.cols[fc][i].rank){
         case Ace:   lowSum+=(kingOnlyMode?8:5); lowCount++; break;
         case Two:   lowSum+=(kingOnlyMode?6:4); lowCount++; break;
         case Three: lowSum+=3; lowCount++; break;
         case Four:  lowSum+=1; lowCount++; break;
         default: break;
      }
   }
   if(lowCount>0) score += lowSum*lowCount;

   // Rule 10: three (or more) buried cards sharing rank AND colour (all red
   // or all black) among the same buried range.
   int rankColorCount[14][2] = {{0}};
   for(int i=0;i<ci-1;i++){
      const Card& c=g.cols[fc][i];
      rankColorCount[(int)c.rank][c.isRed()?1:0]++;
   }
   for(int r=(int)Ace;r<=(int)King && score>=0;r++){
      if(rankColorCount[r][0]>=3 || rankColorCount[r][1]>=3){ score += (kingOnlyMode?20:10); break; }
   }
   return score;
}

// Rule 17's bonus for a col->col move that lands a King-headed run on an
// empty column, completing that King's whole pile right there (see its own
// comment inside scoreMove). Sized to counter rule 15's -500 occupy-a-free-
// column penalty in the one case that penalty was never meant to price.
// Found via a replayed human-solved game (tools/replay.cpp) whose engine
// sent to Claude: 5 of its pile-completing moves ranked as low as 14th of 17
// legal moves in this engine's own ranking, each time for exactly this
// reason. Paired 300-game A/B (tools/sim.cpp): 221->225 wins, 12 deals
// flipped loss->win against 8 win->loss.
static const int SC_KING_PILE_COMPLETED = 0;

// The full 15-rule scoring table for a single discrete move `mv` made from
// board `before`. Deliberately computed WITHOUT materializing the resulting
// board: every rule here (including the empty-column delta behind rules
// 3/15) can be worked out analytically from `before` plus the move itself,
// since a single move only ever touches at most two columns. This matters
// for performance, not just tidiness — scoreMove() is called on every
// candidate at every search node, and the search only needs to pay for
// actually materializing a resulting GameState (a real allocation-heavy copy)
// for the handful of candidates that survive the beam cut below.
//
// Rule 4 ("maintaining an empty column": +200) only ever applies to the DEAL
// action — an ordinary move gets it implicitly for free by simply not
// touching the empty column (see rules 3/15 below), and an earlier version
// of this engine that instead awarded +200 to ANY move whenever an empty
// column merely existed turned out to be a serious bug: it handed every
// unrelated move a huge "for free" bonus for as long as some column stayed
// empty, making pure busywork net positive indefinitely and driving the
// search into genuine multi-hundred-move cycles in testing.
//
// For the deal action specifically: dealReserve() does NOT skip empty
// columns (see skipOnDeal()'s own comment) — a deal OCCUPIES every empty
// column it reaches, it never "maintains" one that exists. So rule 4's
// bonus applies only to the (usually rare) empty column a deal DOESN'T reach
// because the reserve runs out first — dealWillOccupyCount() works out the
// real split. Every column the deal DOES fill gets rule 15's usual -500
// occupying-a-free-column penalty, on top of a flat -100 (user-added rule)
// for dealing at all while any column sits empty — testing showed the old
// blanket +200 could make the AI deal into an empty column instead of using
// it for an available, more direct move on the table.
// `artificialSince`, when provided, tracks — per search LINE, not globally —
// which columns are currently occupied only because a move earlier in this
// same line parked something there (value = the ply number that occupied
// it; -1 = not tracked, either genuinely never touched this line or already
// free again). `curPly` is the ply number of the move being scored right
// now. When a move FREES a column that is tracked this way, rule 3's +500
// is scaled down by how many plies it's been since the artificial
// occupation (500/age instead of a flat 500) — see the long comment above
// searchBest() for why: without this, parking a card on an empty column
// and taking it back out a few moves later can bank rule 3's reward again
// almost for free, even after rule 15 was made symmetric with rule 3,
// because an unrelated move or two in between still lets the round trip
// come out ahead. A column that was ALREADY occupied before this search
// line began (untracked, artificialSince<0) is never discounted this way —
// genuinely freeing a long-standing column keeps earning the full reward.
static int scoreMove(const GameState& before, const MoveHint& mv,
                      const std::array<int,NUM_COLS>* artificialSince, int curPly){
   // King-only mode retuning (user-specified): computed once, up front, so
   // every branch below (including the dealing branch, which returns early)
   // can use it; free-column mode's values are all unchanged.
   bool kingOnlyMode = (GameState::s_freeColMode && !*GameState::s_freeColMode);
   if(mv.fromIdx==-1){
      if(isTrivialBoard(before)) return 0; // trivial-board exception
      int score = 1; // rule 11
      // User-added "dealing while a free column exists" penalties (-100 flat,
      // -500/column the deal occupies, +200/column it genuinely leaves
      // empty) are NOT part of the user's king-only-mode rule list at all —
      // removed there (king-only mode's dealing now scores a plain rule-11
      // +1, same as any other board state); still applied unchanged in
      // free-column mode.
      if(!kingOnlyMode){
         int emptyBefore = countEmptyCols(before);
         if(emptyBefore >= 1){
            score += -100; // user-added rule: dealing while free spaces exist
            int willOccupy = dealWillOccupyCount(before);
            int staysEmpty = emptyBefore - willOccupy;
            score += -500 * willOccupy; // rule 15 (symmetric with rule 3's +500), per column the deal actually occupies
            score += 200 * staysEmpty; // rule 4, per column the deal genuinely leaves empty
         }
      }
      return score;
   }

   int score = -30; // rule 12 (raised from -5 by user request, to discourage roundabout play)
   int beforeEmpty = countEmptyCols(before);
   int afterEmpty  = beforeEmpty;
   // (kingOnlyMode already computed near the top of this function.)

   if(mv.fromType==LOC_FOUNDATION && mv.toType==LOC_COLUMN){
      // rule 14 (symmetric with rule 7's +50): card moved from a foundation back to the table.
      // King-only mode softened from -120 to -40: an empty column there only ever takes a
      // King, so "park it, dig, retrieve later" has none of free mode's flexibility and
      // genuinely often NEEDS a foundation card brought back to the table to unblock a
      // column — a paired batch test (tools/sim.cpp, 150 king-mode deals, same seeds,
      // win/loss compared move-for-move against the unsoftened baseline) showed 4 deals
      // flipped loss->win and zero flipped win->loss at -40. The
      // same softening tried in free mode (-15) came out net negative there (2 win, 4
      // loss) and was dropped — free mode already has empty columns as cheap buffers, so
      // it has much less need for this move and the softer penalty just wastes moves.
      score += (kingOnlyMode?-40:-50);
      // EXPERIMENTAL (candidate fix #2): extra "just sent, costly to
      // retrieve" penalty when real history shows the card was sent to a
      // foundation recently — see s_realFoundSentAt's comment.
      if(GameState::s_realFoundSentAt && GameState::s_realMoveCounter){
         const Card& c = before.found[mv.fromIdx].back();
         int idx = GameState::cardIndex(c);
         int sentAt = (*GameState::s_realFoundSentAt)[idx];
         if(sentAt>=0){
            int age = (*GameState::s_realMoveCounter) - sentAt;
            if(age<1) age=1;
            static const int REAL_FOUND_COOLDOWN_WINDOW = 20;
            if(age<=REAL_FOUND_COOLDOWN_WINDOW) score += -(450/age);
         }
      }
      if(before.cols[mv.toIdx].empty()) afterEmpty -= 1; // destination gets occupied
   } else if(mv.toType==LOC_FOUNDATION){
      const Card& moved = before.cols[mv.fromIdx][mv.fromCard];
      score += isGlobalLowestRank(before, moved.rank) ? 200 : (kingOnlyMode?120:50); // rule 5 / rule 7
      int fc=mv.fromIdx, ci=mv.fromCard;
      if(ci>0) score += revealBonus(before, fc, ci); // rules 6/9/10/16
      // Rule 13 (was -30 for splitting a sequence) was removed: splitting a
      // sequence to send its bottom card to a foundation is always genuine
      // progress, so it no longer needs a scoring penalty — and unlike the
      // column->column case below, it was never gated by the new hard
      // splitting rule either, since requiring the card underneath to ALSO
      // be immediately foundation-ready would block many ordinary, perfectly
      // good foundation moves.
      if(before.cols[fc].size()==1) afterEmpty += 1; // source column emptied
   } else { // column -> column
      int fc=mv.fromIdx, ci=mv.fromCard, tc=mv.toIdx;
      bool destWasEmpty = before.cols[tc].empty();
      if(!destWasEmpty) score += (kingOnlyMode?30:20); // rule 8: joining 2 cards into a sequence
      if(ci>0) score += revealBonus(before, fc, ci); // rules 6/9/10/16
      // Rule 13 (was -30 for splitting a sequence) was removed: splitting a
      // sequence between two columns is now a hard legality question, not a
      // scoring one — see sequenceSplitAllowed(), which allCandidateMoves()
      // consults before this move can even become a candidate. So by the
      // time scoreMove() sees it, it's already known to be worth doing.
      if(ci==0) afterEmpty += 1;      // whole source column emptied
      if(destWasEmpty) afterEmpty -= 1; // destination gets occupied

      // Rule 17 (user-added, free-column mode only): landing a King-headed
      // run on an empty column completes that King's whole pile right there
      // — genuine progress toward the win condition (1/8 of it), however the
      // move scores otherwise — so it deserves to at least partly offset
      // rule 15's -500 "occupied a free column" charge just below, which was
      // never meant to price this particular case (a paired A/B test found 5
      // deals where exactly this made the engine's own ranking put a
      // pile-completing move as low as 14th of 17 legal options). King-only
      // mode: rule 15 never taxes occupying a column there in the first
      // place, so there's nothing here to offset.
      //
      // Deliberately DESTINATION only. An earlier version also credited the
      // SOURCE column's remainder whenever splitting a run happened to leave
      // a valid King pile behind — symmetric with the idea above, but with
      // no rule-15-shaped cost on that side to offset, it was simply free
      // points on a fairly common pattern (any split that exposes a
      // stand-alone King at the very bottom of a column, no empty column
      // involved at all) — see its removal note in CHANGES.md. That alone
      // dropped this engine's own win rate on a 300-game batch from 73.7% to
      // 48.3%. Kept scoped to only the case it was actually built for.
      if(!kingOnlyMode && destWasEmpty && before.cols[fc][ci].rank==King)
         score += SC_KING_PILE_COMPLETED;
      // A symmetric SOURCE-side bonus (crediting a split that exposes an
      // already-valid King prefix left behind) was tried and dropped: even
      // gated to genuine splits only (ci past the column's own recognized
      // run — see sequenceSplitAllowed's similar gate), it was still common
      // enough to do real harm (300-game paired A/B: 8 deals flipped
      // loss->win but 49 flipped win->loss). The destination side above
      // stayed net positive (12 vs 8) because it only ever fires on the
      // specific move rule 15 taxes; there is no equivalent naturally-rare
      // trigger on the source side, so any version of it tried ended up
      // rewarding a fairly ordinary pattern instead of a special one.
   }

   // Rules 3 / 15 — how this move changed the number of empty columns
   // (rule 4's "maintained" case is handled above, on the deal path only).
   //
   // King-only mode (user-requested retuning): a MOVE can only ever free or
   // occupy a column via a King (nothing else can legally land on, or is
   // the sole remaining occupant of a column that empties this way — see
   // allCandidateMoves()'s "whole column -> another empty one" exclusion,
   // which already rules out the one case that could otherwise trigger rule
   // 3 for free). Concretely, in this mode: rule 15 only ever fires when an
   // exposed King (with something unrelated still sitting beneath it) is
   // relocated to a fresh empty column — the single most important
   // productive move type available once a King mode game has kings buried
   // under other cards, so taxing it -500 like free mode's general-purpose
   // "stash anything here" parking move actively fights the engine's own
   // best moves; and rule 3 only ever fires when the very last card left in
   // a column (necessarily a lone King) goes to the FOUNDATION, which is
   // really just an ordinary reveal-shaped event (something new becomes
   // reachable — here, a landing spot only a King can use) rather than the
   // large, farmable "parking spot" rule 3/15 were built to price for free
   // mode. So: no occupy penalty at all, and the free bonus is a flat,
   // undecayed +50 (rule 6's ballpark, not rule 3's) rather than 500/age.
   // (kingOnlyMode already computed near the top of this function.)
   if(afterEmpty > beforeEmpty){
      // Only a move's own SOURCE column can ever become empty (a
      // destination only ever gets occupied), so mv.fromIdx unambiguously
      // names which column this is.
      int bonus;
      if(kingOnlyMode){
         bonus = 50;
      } else {
         bonus = 500;
         if(artificialSince){
            int freedCol = mv.fromIdx;
            if(freedCol>=0 && freedCol<NUM_COLS && (*artificialSince)[freedCol]>=0){
               int age = curPly - (*artificialSince)[freedCol];
               if(age<1) age=1;
               bonus = 500/age;
            }
         }
      }
      score += bonus * (afterEmpty - beforeEmpty); // rule 3: freed a slot
   } else if(afterEmpty < beforeEmpty){
      if(!kingOnlyMode)
         score += -500 * (beforeEmpty - afterEmpty); // rule 15 (symmetric with rule 3's +500): occupied a free column
      // King-only mode: no penalty at all (see comment above).
   }

   return score;
}

// Derives the child node's `artificialSince` tracking array (see scoreMove's
// comment) after playing `mv` at ply `playedAtPly` from board `before`. A
// column that gets occupied (from empty) is stamped with the ply it happened
// at; a column that gets freed stops being tracked (it's genuinely empty
// again — the next thing to occupy it, if any, starts its own fresh clock).
static std::array<int,NUM_COLS> nextArtificialSince(const GameState& before,
                                                      const std::array<int,NUM_COLS>& cur,
                                                      const MoveHint& mv, int playedAtPly){
   std::array<int,NUM_COLS> next = cur;
   if(mv.fromIdx==-1) return next; // dealing never occupies/frees a single named column this way
   if(mv.toType==LOC_COLUMN && before.cols[mv.toIdx].empty()) next[mv.toIdx] = playedAtPly;
   if(mv.fromType==LOC_COLUMN){
      bool srcBecomesEmpty = (mv.fromCard==0);
      if(srcBecomesEmpty) next[mv.fromIdx] = -1;
   }
   return next;
}
// Folds the per-column artificial-occupation ages (as of `atPly`, i.e. the
// ply at which the node being cached will itself be searched) into a hash
// component, so the transposition key captures everything scoreMove()'s
// rule-3 decay can react to going forward — not just the raw board. Two
// search lines that reach the same board at the same depth but with
// different occupation ages must NOT share a cache entry, since the decay
// (and therefore every future rule-3 event downstream) would come out
// differently for whichever line didn't actually compute it. Ages are
// small — bounded by this call's fixed search depth, at most 8 — so this
// stays cheap and exact; no clamping or approximation is needed.
static uint64_t trackingKeyComponent(const std::array<int,NUM_COLS>& a, int atPly){
   uint64_t h = 0;
   for(int c=0;c<NUM_COLS;c++){
      int tag = (a[c]<0) ? 0 : (1 + (atPly - a[c])); // 0 = untracked, else 1+age
      h = h*19 + (uint64_t)(unsigned)tag;
   }
   return h;
}

// ── Search ───────────────────────────────────────────────────────────────
struct AISearchResult {
   int value = 0;               // best cumulative score achievable from here
   std::vector<MoveHint> pv;    // the moves that achieve it, in play order
};

// One memoized node in the transposition table: the best cumulative value
// found so far for a given (board, column-decay-state) key, together with
// `depthAtCompute` — how much search-depth budget was actually available
// when that value was computed — and the PV that achieves it. The key
// deliberately does NOT include depthRemaining (see searchBest()): two
// different root candidates that reach the identical physical board via a
// different number of real moves share this single entry, and whichever one
// gets there with MORE remaining depth budget (i.e. via the SHORTER path)
// is the one that ends up computing and owning it — every other arrival,
// regardless of order, just inherits that value instead of re-deriving a
// shallower (and, per the beam-width schedule, sometimes wildly different)
// one of its own. This also means ply-scaled bonuses inside that shared
// subtree (WIN_BONUS, tidy bonus) get credited using the earliest ply at
// which that board was actually reached by any candidate — exactly what
// "the shorter route to the same continuation is worth more" should mean.
struct TTEntry {
   int value = 0;
   int depthAtCompute = -1;
   std::vector<MoveHint> pv;
};

// Cross-branch "shortest known way to reach this board" record, shared for
// the whole current rankCandidates()/getBestMove() call (unlike TTEntry
// above, which is scoped to a board+decay-state pair and caches ONLY the
// continuation from it). `moves` is the fewest real moves — counted from
// the true root of this whole call — at which `board`'s exact card layout
// has been reached by any explored branch so far; `prefixScore` is the
// cumulative (decay-adjusted) score banked to get there via that many
// moves. See reconcileShortestArrival()'s comment for why any OTHER,
// longer-but-higher-scoring arrival at the identical board must always be
// capped down to this value, never trusted at face value.
struct ShortestArrival {
   int moves;
   int prefixScore;
};

struct AISearchCtx {
   std::unordered_map<uint64_t,TTEntry> tt;  // canonical-hash(+decay state) -> best (value,depth,pv) found so far
   std::unordered_map<uint64_t,ShortestArrival> shortest; // canonical-hash(board only) -> fewest-move arrival + its banked score
   long long nodeBudget = 60000;             // safety valve against worst-case blow-up
   // EXPERIMENTAL (candidate fix #1): constant offset added to every local
   // plyNumber (1, 2, 3, ... within THIS call) when it's used as scoreMove's
   // `curPly` / nextArtificialSince's `playedAtPly` — never when it's used to
   // scale WIN_BONUS/tidy bonus, which must stay local to this call. Set once
   // at the top of rankCandidates() from GameState::s_realMoveCounter (0 when
   // unwired, i.e. no-op).
   int realPlyOffset = 0;
   // Cooperative cancellation: when non-null and set, searchBest() unwinds
   // immediately (returning an empty result). Only ever set when the position
   // being searched has already been superseded, so the result is thrown away.
   const std::atomic<bool>* abort = nullptr;
};

// Undo record for applyMoveInPlace()/undoMoveInPlace(): lets the search walk
// down and back up ONE shared board instead of paying for a fresh
// allocation-heavy GameState copy per explored child.
struct MoveUndo {
   int kind = -1;   // 0 col->col, 1 col->foundation, 2 foundation->col
   int a = 0, b = 0, n = 0;
   Card card{};
};

// Same effect as GameState::applyHint() for the three move kinds the search
// expands (never a reserve deal — that is always a leaf), but reversible.
// Candidates come from allCandidateMoves(), so they are always legal here.
static MoveUndo applyMoveInPlace(GameState& g, const MoveHint& mv){
   MoveUndo u;
   if(mv.fromType==LOC_COLUMN && mv.toType==LOC_COLUMN){
      int fc=mv.fromIdx, tc=mv.toIdx, ci=mv.fromCard;
      u.kind=0; u.a=fc; u.b=tc; u.n=(int)g.cols[fc].size()-ci;
      auto& src=g.cols[fc]; auto& dst=g.cols[tc];
      dst.insert(dst.end(), src.begin()+ci, src.end());
      src.resize(ci);
   } else if(mv.fromType==LOC_COLUMN && mv.toType==LOC_FOUNDATION){
      u.kind=1; u.a=mv.fromIdx; u.b=mv.toIdx;
      u.card=g.cols[u.a].back();
      g.found[u.b].push_back(u.card);
      g.cols[u.a].pop_back();
   } else {
      u.kind=2; u.a=mv.fromIdx; u.b=mv.toIdx;
      u.card=g.found[u.a].back();
      g.cols[u.b].push_back(u.card);
      g.found[u.a].pop_back();
   }
   return u;
}
static void undoMoveInPlace(GameState& g, const MoveUndo& u){
   if(u.kind==0){
      auto& src=g.cols[u.a]; auto& dst=g.cols[u.b];
      src.insert(src.end(), dst.end()-u.n, dst.end());
      dst.resize(dst.size()-u.n);
   } else if(u.kind==1){
      g.cols[u.a].push_back(u.card);
      g.found[u.b].pop_back();
   } else {
      g.found[u.a].push_back(u.card);
      g.cols[u.b].pop_back();
   }
}

// Registers `after` (reached via `movesUsed` real moves from the root of the
// current rankCandidates()/getBestMove() call, having banked `rawPrefix`
// points — scoreMove totals, decay and all — to get there) against ctx's
// cross-branch "shortest known arrival" memo, and returns the value that
// should actually be used in its place.
//
// Why capping is always safe and, per the user's request, should ALWAYS
// apply regardless of how far apart the two arrivals are: if the identical
// board is reachable both via M moves banking P points and via M' < M moves
// banking P' points, nothing about the board itself can ever again tell the
// two arrivals apart — same cards, same everything, identical future
// potential. So whenever P > P', that excess cannot reflect real, lasting
// progress: it can only be rewarding something (a reveal, a freed column, a
// joined sequence...) that was later undone on the way to this identical
// board — the exact shape of bug the reveal-bonus/rule-16 case turned out to
// be. Capping P down to P' removes that loophole in general, for any rule,
// present or future, rather than patching each one individually.
//
// A strictly shorter arrival always becomes the new reference point for
// this board, whatever its own score — length wins outright, per spec. Two
// arrivals of EQUAL length simply keep the higher (both are equally
// legitimate, nothing to discount). Only a LONGER arrival ever gets capped.
static int reconcileShortestArrival(AISearchCtx& ctx, uint64_t bh, int movesUsed, int rawPrefix){
   auto it = ctx.shortest.find(bh);
   if(it == ctx.shortest.end()){
      ctx.shortest.emplace(bh, ShortestArrival{movesUsed, rawPrefix});
      return rawPrefix;
   }
   if(movesUsed < it->second.moves){
      it->second = ShortestArrival{movesUsed, rawPrefix};
      return rawPrefix;
   }
   if(movesUsed == it->second.moves){
      if(rawPrefix > it->second.prefixScore) it->second.prefixScore = rawPrefix;
      return it->second.prefixScore;
   }
   // A strictly shorter route to this exact board is already on record —
   // cap our own banked total down to what it achieved.
   return (rawPrefix > it->second.prefixScore) ? it->second.prefixScore : rawPrefix;
}

// Picks which of `candidates` are worth exploring further at this node: sorts
// by (cheap, analytic) immediate score, tie-broken toward moves touching
// `touchedCol` ("Move Dependency / Locality" — try moves related to the last
// one first), and returns the indices of the top `beamWidth` — the only ones
// for which a real GameState copy will be paid for.
static std::vector<int> orderAndBeam(const std::vector<MoveHint>& candidates,
                                      const std::vector<int>& immediate,
                                      int touchedCol, int beamWidth){
   std::vector<int> idx(candidates.size());
   for(size_t i=0;i<idx.size();i++) idx[i]=(int)i;
   std::stable_sort(idx.begin(), idx.end(), [&](int a,int b){
      if(immediate[a]!=immediate[b]) return immediate[a]>immediate[b];
      auto touches=[&](int i){
         const MoveHint& m=candidates[i];
         return (m.fromType==LOC_COLUMN && m.fromIdx==touchedCol) ||
                (m.toType  ==LOC_COLUMN && m.toIdx  ==touchedCol);
      };
      bool ta=touches(a), tb=touches(b);
      if(ta!=tb) return ta;
      return false;
   });
   if((int)idx.size()>beamWidth) idx.resize(beamWidth);
   return idx;
}

// Recursively finds the best achievable cumulative move-score starting at `g`
// with `depthRemaining` plies of budget left. `justPlayed` is the move that
// led to `g` — its exact reverse is excluded from consideration here so a
// single search line can never "discover" that shuffling a card away and
// straight back ties with doing nothing (the same anti-ping-pong idea the
// old engine used, now local to one search line instead of driving the
// whole loop-detection story — the REAL 20-move history lives in main.cpp
// and is applied only at the root, since it concerns actually-PLAYED moves).
// `plyNumber` counts real moves from the root (the root's own candidates are
// ply 1, this function's candidates start at ply 2, ...) — used to scale
// WIN_BONUS and the tidy-board bonus by how soon each is actually reached
// (see the notes above their definitions): it never affects the
// transposition cache key below because, within one root search call, it is
// always a fixed function of depthRemaining (both change by exactly 1 per
// recursion level), so depthRemaining alone still uniquely identifies it.
// `artificialSince` is the per-line column-occupation tracking scoreMove()
// uses to decay rule 3 (see its comment); it DOES need to affect caching —
// see trackingKeyComponent() below, folded into the transposition key.
// `prefixScore` is the cumulative (decay-adjusted) score already banked to
// reach `g` from the true root of the enclosing rankCandidates()/
// getBestMove() call — needed so each candidate move's resulting board can
// be checked against ctx's cross-branch shortest-arrival memo (see
// reconcileShortestArrival()) before its own score is trusted.
static AISearchResult searchBest(GameState& g, int depthRemaining, AISearchCtx& ctx,
                                  const MoveHint* justPlayed, int plyNumber,
                                  const std::array<int,NUM_COLS>& artificialSince,
                                  int prefixScore){
   AISearchResult res;
   if(depthRemaining<=0 || ctx.nodeBudget<=0) return res;
   if(ctx.abort && ctx.abort->load(std::memory_order_relaxed)) return res;
   ctx.nodeBudget--;

   std::vector<MoveHint> candidates = g.allCandidateMoves();
   if(justPlayed && justPlayed->fromIdx!=-1){
      candidates.erase(std::remove_if(candidates.begin(),candidates.end(),
         [&](const MoveHint& c){
            return c.fromType==justPlayed->toType   && c.fromIdx==justPlayed->toIdx &&
                   c.toType  ==justPlayed->fromType && c.toIdx  ==justPlayed->fromIdx;
         }), candidates.end());
   }
   if(candidates.empty()) return res;

   std::vector<int> immediate(candidates.size());
   for(size_t i=0;i<candidates.size();i++) immediate[i]=scoreMove(g, candidates[i], &artificialSince, plyNumber+ctx.realPlyOffset);

   int touchedCol = (justPlayed && justPlayed->toType==LOC_COLUMN) ? justPlayed->toIdx : -1;
   int beamWidth = (depthRemaining>4) ? 4 : 6; // the depth-8 (empty-column) horizon stays narrower per ply
   std::vector<int> idx = orderAndBeam(candidates, immediate, touchedCol, beamWidth);

   bool wasTidy = isTrivialBoard(g);
   int best = -1000000000; std::vector<MoveHint> bestPv; bool have=false;
   for(int i : idx){
      const MoveHint& mv = candidates[i];
      int mvScore = immediate[i];
      int contValue = 0;
      std::vector<MoveHint> contPv;
      if(mv.fromIdx==-1){
         // Deal from the reserve — never expanded past (see design note).
      } else {
         // Derived from the board BEFORE the move, so compute it first.
         std::array<int,NUM_COLS> childArtificial = nextArtificialSince(g, artificialSince, mv, plyNumber+ctx.realPlyOffset);
         // Walk down on the shared board and back up again (see MoveUndo)
         // instead of copying it for every child.
         MoveUndo undo = applyMoveInPlace(g, mv);
         bool wins = g.checkWin();
         if(wins){
            mvScore += winBonusForPly(plyNumber);
         } else if(!wasTidy && isTrivialBoard(g)){
            mvScore += tidyBonusForPly(plyNumber);
         }
         uint64_t afterHash = boardCanonicalHash(g);
         // Cross-branch dominance check (see reconcileShortestArrival's
         // comment): if some OTHER already-explored branch reached this
         // exact board via fewer real moves, whatever this move's own score
         // just banked gets capped down to what that shorter branch banked
         // — any excess could only be rewarding something later undone on
         // the way to this identical board, never real progress.
         mvScore = reconcileShortestArrival(ctx, afterHash, plyNumber, prefixScore+mvScore) - prefixScore;
         if(!wins){
            // Deliberately NOT including depthRemaining in this key (see
            // TTEntry's comment): two different root candidates reaching
            // this identical (board, decay-state) pair via a different
            // number of real moves must land on the SAME entry, so whichever
            // one gets here with more depth budget left (the shorter path)
            // is the one that ends up computing and owning it.
            uint64_t key = afterHash * 1000003ULL
                         + trackingKeyComponent(childArtificial, plyNumber+1+ctx.realPlyOffset) * 2654435761ULL;
            int neededDepth = depthRemaining - 1;
            auto it = ctx.tt.find(key);
            if(it != ctx.tt.end() && it->second.depthAtCompute >= neededDepth){
               // Already explored at least this deep (by this or an earlier
               // — typically shorter — arrival at the same node): reuse it
               // rather than re-deriving a shallower value of our own.
               contValue = it->second.value;
               contPv = it->second.pv;
            } else {
               AISearchResult child = searchBest(g, neededDepth, ctx, &mv, plyNumber+1, childArtificial, prefixScore+mvScore);
               contValue = child.value;
               contPv = child.pv;
               if(it == ctx.tt.end() || neededDepth > it->second.depthAtCompute)
                  ctx.tt[key] = TTEntry{contValue, neededDepth, std::move(child.pv)};
            }
         }
         undoMoveInPlace(g, undo);
      }
      int total = mvScore + contValue;
      if(total > best){
         best = total; have = true;
         bestPv.clear();
         bestPv.push_back(mv);
         for(auto& m : contPv) bestPv.push_back(m);
      }
   }
   if(have){ res.value = best; res.pv = std::move(bestPv); }
   return res;
}

// ── Tiny fork/join helper (Win32 threads — deliberately not std::thread, which
// some mingw-w64 builds lack) used to spread independent root-candidate
// searches over the machine's cores. The calling thread works too; workers run
// at below-normal priority so the UI stays responsive.
static int aiWorkerThreads(){
   // Optional override for testing/tuning: environment variable
   // PASJANS_AI_THREADS=1..16 (1 = fully sequential).
   static int n = []{
      SYSTEM_INFO si; GetSystemInfo(&si); int c=(int)si.dwNumberOfProcessors;
      char buf[16]; DWORD len=GetEnvironmentVariableA("PASJANS_AI_THREADS",buf,sizeof(buf));
      if(len>0 && len<sizeof(buf)){ int v=atoi(buf); if(v>=1) c=v; }
      return c<1?1:(c>16?16:c); }();
   return n;
}
struct AIParJob {
   std::atomic<int> next{0};
   int count = 0;
   const std::function<void(int)>* fn = nullptr;
};
static DWORD WINAPI aiParWorker(LPVOID p){
   AIParJob* j = (AIParJob*)p;
   for(;;){
      int i = j->next.fetch_add(1);
      if(i >= j->count) break;
      (*j->fn)(i);
   }
   return 0;
}
static void aiParallelFor(int count, const std::function<void(int)>& fn, bool allowThreads){
   int T = allowThreads ? std::min(count, aiWorkerThreads()) : 1;
   if(T <= 1){ for(int i=0;i<count;i++) fn(i); return; }
   AIParJob job; job.count = count; job.fn = &fn;
   HANDLE h[16]; int hc = 0;
   for(int t=1;t<T;t++){
      HANDLE x = CreateThread(nullptr, 0, aiParWorker, &job, 0, nullptr);
      if(x){ SetThreadPriority(x, THREAD_PRIORITY_BELOW_NORMAL); h[hc++] = x; }
   }
   aiParWorker(&job); // the caller pulls work too; if no worker thread could be created it simply does everything
   if(hc){ WaitForMultipleObjects(hc, h, TRUE, INFINITE); for(int i=0;i<hc;i++) CloseHandle(h[i]); }
}
// Shared root-search core for both getBestMove() and getRankedMoves(): scores
// every one of `candidates` cheaply (analytic scoreMove), beam-limits to the
// top `topN` by that immediate score ("Move Dependency / Locality" plays no
// role at the root — there's no `justPlayed` to be local to), runs a full
// depth-limited search for only those, and returns them sorted by the exact
// 3-level tie-break the spec calls for (deep cumulative score, then immediate
// score, then lower-rank-reveal preference). That ordering — together with
// the fixed Zobrist seed and the total absence of randomness anywhere in this
// file — is what guarantees an undone automatic move recomputes to the
// identical move when redone.
static std::vector<RankedMove> rankCandidates(const GameState& g, std::vector<MoveHint> candidates, int topN,
                                                bool bypassEmptyColShortcut=false,
                                                const std::atomic<bool>* abortFlag=nullptr){
   std::vector<RankedMove> result;
   if(candidates.empty()) return result;

   // Hard rule (user-specified, matches isTrivialBoard()'s own doc comment):
   // once every column is reduced to empty, a single card, or one complete
   // valid sequence, dealing from the reserve is MANDATORY — "rozdaj z
   // rezerwy bez liczenia punktów" (deal without even counting points), not
   // merely penalty-free the way scoreMove()'s isTrivialBoard() exception
   // treats it. Without this, a trivial board's deal only ever scored a
   // neutral 0, so any other legal move with a positive score (a card going
   // straight to a foundation, say) still outranked it and got played
   // instead — real save files showed dozens of such moves happening before
   // a deal ever occurred. When a deal is actually available (reserve not
   // empty), short-circuit the whole scoring/search below and hand back
   // just that one move; if the reserve is empty there is nothing to deal,
   // so fall through to the normal ranking of whatever moves remain.
   if(isTrivialBoard(g)){
      for(const auto& mv : candidates){
         if(mv.fromIdx==-1){
            result.push_back(RankedMove{ mv, 0, {} });
            return result;
         }
      }
   }

   // EXPERIMENTAL hard rule (user-proposed, being trialled): once 3 or more
   // columns are empty, immediately occupy one of them with whichever legal
   // move targeting an empty column scores best, bypassing the full deep
   // search entirely — both for speed (several empty columns multiply the
   // branching factor of EVERY other move too, since in free mode almost
   // anything can be parked on any of them) and on the theory that once
   // there's more free "workspace" than the position actually needs, using
   // one up is rarely wrong and searching to confirm it is wasted effort.
   // Skipped when the board is already trivial (that hard rule above always
   // wins) or when no candidate actually targets an empty column (e.g.
   // king-only mode with no king currently exposed) — falls through to the
   // normal ranking below in that case.
   // EXPERIMENTAL (candidate fix #1): seed from real, cross-real-move history
   // when main.cpp has wired it (see s_realArtificialSince's comment) — 0
   // offset / all-untracked when unwired, which reproduces prior behaviour
   // exactly.
   std::array<int,NUM_COLS> realStart; realStart.fill(-1);
   int realNow = 0;
   if(GameState::s_realArtificialSince) realStart = *GameState::s_realArtificialSince;
   if(GameState::s_realMoveCounter) realNow = *GameState::s_realMoveCounter;

   // King-only mode (user-specified): this hard "3+ empty columns" short-
   // circuit is NOT part of the user's king-only-mode rule list at all —
   // removed there, so king-only mode always falls through to the normal
   // full-search ranking below. Free-column mode keeps it unchanged.
   bool kingOnlyModeForHardRule = (GameState::s_freeColMode && !*GameState::s_freeColMode);
   if(!bypassEmptyColShortcut && !kingOnlyModeForHardRule && !isTrivialBoard(g) && countEmptyCols(g) >= 3){
      int bestIdx = -1, bestScore = 0;
      for(size_t i=0;i<candidates.size();i++){
         const MoveHint& mv = candidates[i];
         if(mv.fromIdx==-1) continue; // a deal isn't "occupying" a free column this way
         if(mv.toType!=LOC_COLUMN || !g.cols[mv.toIdx].empty()) continue;
         int sc = scoreMove(g, mv, &realStart, 1+realNow);
         if(bestIdx<0 || sc>bestScore){ bestIdx=(int)i; bestScore=sc; }
      }
      if(bestIdx>=0){
         result.push_back(RankedMove{ candidates[bestIdx], bestScore, {} });
         return result;
      }
   }

   // User-configurable base search depth (Settings dialog), separately per
   // mode — defaults 4 (free-column) / 5 (king-only) when unwired. Free mode
   // always searches 4 plies deeper whenever any column is empty (branching
   // explodes there, since almost anything can park on it); king-only mode
   // does NOT get that doubling (user-requested: only a King can ever occupy
   // an empty column there — see emptyColumnOccupyAllowed()'s comment — so
   // an empty column doesn't multiply the branching factor of every other
   // move the way it does in free mode, and searching deeper there buys much
   // less).
   bool kingOnlyModeForDepth = (GameState::s_freeColMode && !*GameState::s_freeColMode);
   int baseDepth = kingOnlyModeForDepth
      ? (GameState::s_searchDepthKing ? *GameState::s_searchDepthKing : 5)
      : (GameState::s_searchDepthFree ? *GameState::s_searchDepthFree : 4);
   if(baseDepth<1) baseDepth=1;
   if(baseDepth>20) baseDepth=20; // keep in sync with MAX_SEARCH_DEPTH in main.cpp
   int depth = kingOnlyModeForDepth ? baseDepth
             : ((countEmptyCols(g) > 0) ? baseDepth+4 : baseDepth);
   std::vector<int> immediate(candidates.size());
   std::vector<int> revealRank(candidates.size(), 99); // 99 = "reveals nothing", loses every reveal tiebreak
   for(size_t i=0;i<candidates.size();i++){
      immediate[i] = scoreMove(g, candidates[i], &realStart, 1+realNow);
      const MoveHint& mv = candidates[i];
      if(mv.fromType==LOC_COLUMN && mv.fromIdx>=0 && mv.fromCard>0)
         revealRank[i] = (int)g.cols[mv.fromIdx][mv.fromCard-1].rank;
   }
   // Only the most promising root candidates get a full deep search — the
   // same beam-limiting idea as inside searchBest(), applied here so that a
   // board with many legal moves doesn't multiply the (already beam-limited)
   // per-candidate search cost by an unbounded root branching factor.
   std::vector<int> rootIdx = orderAndBeam(candidates, immediate, -1, topN);

   struct RootCand {
      int ci;         // index into `candidates`
      int deep;       // primary tie-break key: full-search cumulative score
      std::vector<MoveHint> pv;
      std::array<int,NUM_COLS> childArtificial; // decay tracking handed to the child search
      bool needsSearch = false;
   };
   std::vector<RootCand> rc;
   rc.reserve(rootIdx.size());

   // ── Phase 1 (sequential, cheap): score every root candidate's own move and
   // register the board it reaches against the cross-candidate "shortest
   // arrival" memo (see reconcileShortestArrival's comment — this is where it
   // matters most, since two ROOT candidates reaching an identical board via a
   // different number of moves, e.g. one going straight to a foundation,
   // another detouring through a free column first, is exactly the shape the
   // user's report showed up as).
   AISearchCtx rootCtx;
   rootCtx.shortest.emplace(boardCanonicalHash(g), ShortestArrival{0,0}); // the starting board itself: 0 moves, 0 banked
   bool rootWasTidy = isTrivialBoard(g);
   GameState work = g.boardOnly();
   for(int i : rootIdx){
      const MoveHint& mv = candidates[i];
      RootCand r; r.ci = i; r.deep = immediate[i]; r.childArtificial = realStart;
      if(mv.fromIdx!=-1){
         r.childArtificial = nextArtificialSince(g, realStart, mv, 1+realNow);
         MoveUndo undo = applyMoveInPlace(work, mv);
         bool wins = work.checkWin();
         if(wins){
            r.deep += winBonusForPly(1); // this candidate itself is move #1
         } else if(!rootWasTidy && isTrivialBoard(work)){
            r.deep += tidyBonusForPly(1);
         }
         r.deep = reconcileShortestArrival(rootCtx, boardCanonicalHash(work), 1, r.deep);
         r.needsSearch = !wins;
         undoMoveInPlace(work, undo);
      }
      rc.push_back(std::move(r));
   }

   // ── Phase 2: one fully independent deep search per candidate. Each gets
   // its OWN context (transposition table, node budget) instead of all
   // sharing one, which fixes two problems at once: the shared budget used to
   // be spent by whichever candidates happened to come first, leaving the
   // later ones with an empty (0-valued) search; and independent searches can
   // run on separate threads. The result of each task depends only on the
   // input position and the task's own candidate — never on thread timing or
   // on the order tasks finish — so the outcome is fully deterministic.
   std::vector<int> todo;
   for(size_t k=0;k<rc.size();k++) if(rc[k].needsSearch) todo.push_back((int)k);
   if(!todo.empty()){
      const int nodesPerCand = std::max(8000, std::min(30000, 90000/(int)todo.size()));
      auto runTask = [&](int t){
         if(abortFlag && abortFlag->load(std::memory_order_relaxed)) return;
         RootCand& r = rc[todo[t]];
         const MoveHint& mv = candidates[r.ci];
         AISearchCtx ctx;
         ctx.shortest = rootCtx.shortest;   // start-board + first-move arrivals of every root candidate
         ctx.nodeBudget = nodesPerCand;
         ctx.realPlyOffset = realNow;
         ctx.abort = abortFlag;
         GameState b = g.boardOnly();
         applyMoveInPlace(b, mv);
         AISearchResult sr = searchBest(b, depth-1, ctx, &mv, 2, r.childArtificial, r.deep); // its continuation starts at move #2
         r.deep += sr.value;
         r.pv = std::move(sr.pv);
      };
      // Threads only pay off for the big (depth >= 6) searches; the shallow
      // ones finish in a couple of milliseconds, less than spawning costs.
      aiParallelFor((int)todo.size(), runTask, depth>=6);
   }

   std::stable_sort(rc.begin(), rc.end(), [&](const RootCand& a, const RootCand& b){
      if(a.deep != b.deep) return a.deep > b.deep;
      if(immediate[a.ci] != immediate[b.ci]) return immediate[a.ci] > immediate[b.ci];
      if(revealRank[a.ci] != revealRank[b.ci]) return revealRank[a.ci] < revealRank[b.ci];
      return false;
   });

   result.reserve(rc.size());
   for(auto& r : rc) result.push_back(RankedMove{ candidates[r.ci], immediate[r.ci], std::move(r.pv) });
   return result;
}

std::vector<RankedMove> GameState::getRankedMoves(int topN, bool bypassEmptyColShortcut,
                                                    const std::atomic<bool>* abortFlag) const {
   return rankCandidates(*this, allCandidateMoves(), topN, bypassEmptyColShortcut, abortFlag);
}

MoveHint GameState::getBestMove(const std::set<std::pair<int,int>>& seen,
                                 int* outScore, std::vector<MoveHint>* outPlan) const {
   const int kRootBeam = 20;
   std::vector<RankedMove> ranked = rankCandidates(*this, allCandidateMoves(), kRootBeam);
   for(auto& rm : ranked){
      if(!seen.empty() && seen.count(moveExclusionKey(rm.move))) continue;
      if(outScore) *outScore = rm.score;
      if(outPlan){
         outPlan->clear();
         outPlan->push_back(rm.move);
         for(auto& m : rm.pv) outPlan->push_back(m);
      }
      return rm.move;
   }
   // Rare fallback: every one of the top kRootBeam candidates is on the
   // exclusion list. Regenerate with `seen` filtered at the SOURCE (the
   // exhaustive behaviour this function always used before getRankedMoves()
   // existed), so correctness never depends on beam width even though this
   // path costs a second full search.
   if(!seen.empty()){
      std::vector<RankedMove> filtered = rankCandidates(*this, allCandidateMoves(seen), kRootBeam);
      if(!filtered.empty()){
         const RankedMove& top = filtered.front();
         if(outScore) *outScore = top.score;
         if(outPlan){
            outPlan->clear();
            outPlan->push_back(top.move);
            for(auto& m : top.pv) outPlan->push_back(m);
         }
         return top.move;
      }
   }
   if(outScore) *outScore = 0;
   if(outPlan) outPlan->clear();
   return MoveHint{};
}
