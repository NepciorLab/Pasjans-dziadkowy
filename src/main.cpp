// Pasjans Dziadkowy v7
// Developed with AI assistance (Claude, Anthropic) — see README.md.
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _WIN32_WINNT 0x0600
#include <objbase.h>
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <set>
#include <random>
#include <memory>
#include "game.h"
#include "solver.h"
#include "layout.h"
#include "card_images_d2d.h"
#include "anim.h"
#include "renderer_d2d.h"
#include "sound.h"
#include "keybindings.h"
#include "update.h"
KeyBinding g_keys[KA_COUNT];
using namespace Gdiplus;

// Bump this (and tag the matching GitHub release vMAJOR.MINOR.PATCH) on every
// release meant to reach users through the updater — see update.h.
static const wchar_t* APP_VERSION = L"1.0.3";

// Define GameState static member
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;
static ID2D1Factory*          g_d2dFactory=nullptr;
static ID2D1HwndRenderTarget* g_d2dRT=nullptr;
static IDWriteFactory*        g_dwFactory=nullptr;
static IWICImagingFactory*    g_wicFactory=nullptr;
static RendererD2D            g_renderer;

// ============================================================================
// Constants
// ============================================================================
static const UINT TIMER_DEAL=1, TIMER_FW=2, TIMER_BLINK=3;
static const int  ID_NEW=201,ID_HINT=202,ID_FS=203,ID_UNDO=204,ID_REDO=205;
static const int  ID_SAMOGRAJ=213;
static const int  ID_SOLVER  =240;

// ============================================================================
// Card flight animation
// ============================================================================
static const DWORD CARD_ANIM_MS         = 220;  // duration of normal move animation
static const DWORD CARD_ANIM_PREVIEW_MS = 440;  // duration of hint preview flight
static const DWORD CARD_ANIM_PAUSE_MS   = 500;  // hint: pause at dest before vanish

// ── Animation speed (Settings → Ogólne) ─────────────────────────────────────
// -2..+2, 0 = the original/default speed (slider centred). Each step is a
// 50% change: slowing down multiplies the duration (so "50% slower" means
// half the speed, i.e. the animation takes twice as long); speeding up
// divides it the same way in reverse (50% faster = 1.5x the speed, so 1/1.5
// of the duration). Steps compound multiplicatively, so -2/+2 are a further
// 50% on top of -1/+1, not simply double the single-step change.
static int   g_animSpeedStep = 0;
static float g_animDurationMul = 1.0f; // derived from g_animSpeedStep — multiplies every animation's base duration
static void applyAnimSpeedStep(int step){
   if(step<-2) step=-2; if(step>2) step=2;
   g_animSpeedStep=step;
   switch(step){
      case -2: g_animDurationMul=4.0f;      break; // speed x0.25
      case -1: g_animDurationMul=2.0f;      break; // speed x0.5
      case  1: g_animDurationMul=1.f/1.5f;  break; // speed x1.5
      case  2: g_animDurationMul=1.f/2.25f; break; // speed x2.25 (1.5 x1.5)
      default: g_animDurationMul=1.0f;      break; // step 0: unchanged
   }
}
static std::wstring animSpeedLabel(int step){
   switch(step){
      case -2: return L"Wolniej (x0,25)";
      case -1: return L"Wolniej (x0,5)";
      case  1: return L"Szybciej (x1,5)";
      case  2: return L"Szybciej (x2,25)";
      default: return L"Normalna";
   }
}

// "Samograj bez końca" (Shift+1): once the current deal ends (win or stuck),
// automatically deal a fresh one and keep self-playing, instead of stopping.
// Only ever true while g_samogranoActive is also true (see toggleMarathon()/
// cancelSamograj(), defined further down near doAutoMove()) - the two are
// kept in lockstep. Declared up here (rather than alongside g_samogranoActive)
// so effectiveAnimMul() below — used by CardAnim::duration(), which is
// defined right after this — can see it.
static bool g_samogranoMarathon = false;

// ── Auto-play scoreboard (toolbar, right of the Settings button) ─────────────
// Counts, for the current run of Samograj / Samograj bez końca only, how many
// deals were played to the end, how many of those were won, and the win rate.
// Shown as three lines, one under the other. Reset each time auto-play is
// switched on, and hidden again as soon as auto-play is left.
static int  g_autoPlayed = 0, g_autoWon = 0;
static bool g_autoDealCounted = false; // this deal already added to the counters
static bool g_autoDealStuck = false;   // engine has nothing left to play although the game itself still sees a legal move
static HWND g_hAutoStat[3] = {nullptr,nullptr,nullptr};
static void updateAutoStatsUI(bool autoActive){
   if(!g_hAutoStat[0]) return;
   wchar_t b0[48], b1[48], b2[48];
   swprintf(b0,48,L"Rozdań: %d",g_autoPlayed);
   swprintf(b1,48,L"Wygranych: %d",g_autoWon);
   if(g_autoPlayed>0){
      wchar_t pct[24]; swprintf(pct,24,L"%.1f",100.0*g_autoWon/g_autoPlayed);
      for(wchar_t* p=pct;*p;++p) if(*p==L'.') *p=L',';
      swprintf(b2,48,L"Zwycięstw: %ls%%",pct);
   } else swprintf(b2,48,L"Zwycięstw: -");
   const wchar_t* t[3]={b0,b1,b2};
   bool show = autoActive; // visible only while auto-play is on
   for(int i=0;i<3;i++){
      wchar_t cur[48]={}; GetWindowTextW(g_hAutoStat[i],cur,48);
      if(wcscmp(cur,t[i])!=0) SetWindowTextW(g_hAutoStat[i],t[i]);
      ShowWindow(g_hAutoStat[i],show?SW_SHOWNA:SW_HIDE);
   }
}
static void resetAutoStats(){ g_autoPlayed=0; g_autoWon=0; g_autoDealCounted=false; }
// While the marathon is running, animations always play at this fixed base
// duration, regardless of whatever "Prędkość animacji" step is set in
// Settings for manual play — a steady, comfortable pace for a long
// unattended run instead of whatever speed happens to be configured.
static const float MARATHON_ANIM_MS = 20.0f;
static float effectiveAnimMul(){
   if(g_samogranoMarathon) return MARATHON_ANIM_MS/(float)CARD_ANIM_MS;
   return g_animDurationMul;
}
// Referenced from sound.h's playSound() (declared there, defined here since
// it needs g_samogranoMarathon just above).
bool isSoundMuted(){ return g_samogranoMarathon; }

struct CardAnim {
   std::vector<Card> cards;
   float sx, sy;
   float ex, ey;
   float srcOv, dstOv;
   float arcH;
   DWORD startTime;
   DWORD dur;      // custom duration in ms; 0 = use default
   bool  toFound, isPreview;
   int   phase;

   DWORD duration() const {
      DWORD base = dur>0 ? dur : (isPreview ? CARD_ANIM_PREVIEW_MS : CARD_ANIM_MS);
      return (DWORD)((float)base * effectiveAnimMul());
   }
   float t() const {
      DWORD now=timeGetTime();
      if(now<startTime) return 0.f;  // stagger: not started yet
      float elapsed=(float)(now-startTime);
      float tt=elapsed/(float)duration();
      return tt>1.f?1.f:tt;
   }
   float ease() const {
      float tt=t();
      return 1.f-(1.f-tt)*(1.f-tt)*(1.f-tt);
   }
   float renderOv() const {
      if(isPreview&&phase==1) return dstOv;
      return srcOv+(dstOv-srcOv)*ease();
   }
   float cx() const {
      if(isPreview&&phase==1) return ex;
      return sx+(ex-sx)*ease();
   }
   float cy() const {
      if(isPreview&&phase==1) return ey;
      float s=ease();
      float arc=arcH*4.f*s*(1.f-s);
      return sy+(ey-sy)*s - arc;
   }
   bool flightDone() const { return t()>=1.f; }
   bool done() const {
      if(!isPreview) return flightDone();
      return phase>=2;
   }
};

static std::vector<CardAnim> g_cardAnims;
static bool g_animating        = false; // true while real move anim is running (blocks input)
static bool g_previewAnimating = false; // true during hint preview (does NOT block input)
// Cards to hide from board during animation (their col index + card index in source col)
static int  g_hideCol=-1, g_hideFromIdx=-1;     // hide src cards during preview
static int  g_hideDstCol=-1, g_hideDstFromIdx=-1; // hide dest cards during real move anim
static int  g_hideDstFound=-1;                    // hide top card of this foundation during flight
// Per-column: hide cards from this index onward during deal animation
// -1 = no hiding for that column
static int  g_hideDealCol[NUM_COLS];              // initialized in WM_CREATE / newGame
static const int  DRAG_THR=5;

// ============================================================================
// Globals
// ============================================================================
static HWND           g_hwnd      = nullptr;  // main window (toolbar + menu)
static HWND           g_gameHwnd  = nullptr;  // child window for D2D game area
static GameState      g_game;
static Layout         g_layout;

// ── Background best-move precomputation ────────────────────────────────────
// The hint/auto-move search (getBestMove, with its multi-move lookahead) can
// take a moment. Rather than making the user wait for it right when they
// press the button, we kick it off in a background thread immediately after
// every move, so the answer is usually already sitting there ready by the
// time it's actually asked for. Uses a private copy of the board (boardOnly())
// so the worker thread never touches the live g_game the UI thread is using.
static CRITICAL_SECTION g_bestMoveLock;
static bool             g_bestMoveLockInit      = false;
static int              g_boardGeneration       = 0;   // bumped on every move
static int              g_bestMoveGeneration    = -1;  // generation the cache below is for
// The background thread computes the FULL ranked beam (best to worst), not
// just the single winner — so callers that need to skip a handful of
// candidates for reasons the engine itself knows nothing about (the 20-move
// anti-repetition history, the foundation-stall counter) can walk the
// already-sorted, already-searched list instead of re-running a fresh full
// search once per excluded candidate. That per-attempt re-search used to be
// the actual cost driver in doAutoMove()'s/performHintNow()'s retry loops.
static std::vector<RankedMove> g_cachedRankedMoves;
static bool             g_bestMoveComputing     = false;
// Cooperative cancel for the background search: raised (under g_bestMoveLock,
// together with the g_boardGeneration bump) the moment the position being
// searched is superseded, so the worker stops burning cores on an answer that
// is going to be thrown away; cleared again when the next job is launched.
static std::atomic<bool> g_bestMoveAbort{false};

struct BestMoveJob { GameState board; int generation; };

static void kickOffBestMovePrecompute();

static DWORD WINAPI bestMoveThreadProc(LPVOID param){
   std::unique_ptr<BestMoveJob> job((BestMoveJob*)param);
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); // keep the UI thread snappy
   std::vector<RankedMove> ranked = job->board.getRankedMoves(20, false, &g_bestMoveAbort);
   bool stale;
   EnterCriticalSection(&g_bestMoveLock);
   stale = (job->generation != g_boardGeneration);
   if(!stale){
      g_cachedRankedMoves = std::move(ranked);
      g_bestMoveGeneration = job->generation;
   }
   g_bestMoveComputing = false;
   LeaveCriticalSection(&g_bestMoveLock);
   // The board moved on again while we were thinking — the result we just
   // computed no longer applies, so immediately start over for the current
   // position instead of leaving the cache stale.
   if(stale) kickOffBestMovePrecompute();
   return 0;
}

static void kickOffBestMovePrecompute(){
   if(!g_bestMoveLockInit){ InitializeCriticalSection(&g_bestMoveLock); g_bestMoveLockInit=true; }
   EnterCriticalSection(&g_bestMoveLock);
   if(g_bestMoveComputing){ LeaveCriticalSection(&g_bestMoveLock); return; }
   g_bestMoveComputing = true;
   g_bestMoveAbort.store(false);
   int gen = g_boardGeneration;
   LeaveCriticalSection(&g_bestMoveLock);
   auto* job = new BestMoveJob{ g_game.boardOnly(), gen };
   HANDLE h = CreateThread(nullptr,0,bestMoveThreadProc,job,0,nullptr);
   if(h) CloseHandle(h);
   else {
      delete job;
      EnterCriticalSection(&g_bestMoveLock);
      g_bestMoveComputing = false;
      LeaveCriticalSection(&g_bestMoveLock);
   }
}

// Call after ANY move (including undo/redo) so the precomputed cache tracks
// the current position instead of a stale one.
static void invalidateBestMoveCache(){
   if(!g_bestMoveLockInit){ InitializeCriticalSection(&g_bestMoveLock); g_bestMoveLockInit=true; }
   EnterCriticalSection(&g_bestMoveLock);
   g_boardGeneration++;
   g_bestMoveAbort.store(true); // a search still running for the old position is now pointless
   LeaveCriticalSection(&g_bestMoveLock);
   kickOffBestMovePrecompute();
}

// Returns the precomputed ranked-move beam if it's ready and current for the
// live board; otherwise computes it synchronously right now (same fallback
// guarantee the old single-move cache gave: a cache miss never produces a
// worse answer, just a slower one).
static std::vector<RankedMove> getRankedMovesFast(){
   if(!g_bestMoveLockInit){ InitializeCriticalSection(&g_bestMoveLock); g_bestMoveLockInit=true; }
   EnterCriticalSection(&g_bestMoveLock);
   bool ready = (g_bestMoveGeneration == g_boardGeneration);
   std::vector<RankedMove> cached = ready ? g_cachedRankedMoves : std::vector<RankedMove>{};
   LeaveCriticalSection(&g_bestMoveLock);
   if(ready) return cached;
   return g_game.getRankedMoves();
}

// Returns the precomputed best move if it's ready and current; otherwise
// falls back to computing it synchronously right now (same as before this
// feature existed) — so the only user-visible effect of a cache miss is
// exactly the old behaviour, never a wrong or worse answer.
static MoveHint getBestMoveFast(const std::set<std::pair<int,int>>& seen={}, int* outScore=nullptr){
   std::vector<RankedMove> ranked = getRankedMovesFast();
   for(auto& rm : ranked){
      if(!seen.empty() && seen.count(moveExclusionKey(rm.move))) continue;
      if(outScore) *outScore = rm.score;
      return rm.move;
   }
   return g_game.getBestMove(seen, outScore);
}

// True once the background precompute thread has a best-move answer ready
// for the CURRENT board position (i.e. a top-level getBestMoveFast({}) call
// would return instantly instead of blocking on a fresh synchronous search).
static bool isBestMoveReady(){
   if(!g_bestMoveLockInit) return false;
   EnterCriticalSection(&g_bestMoveLock);
   bool ready = (g_bestMoveGeneration == g_boardGeneration);
   LeaveCriticalSection(&g_bestMoveLock);
   return ready;
}

// ── "Myślę" thinking indicator ──────────────────────────────────────────────
// Hint (P) and Auto-move (A) used to fall back to a synchronous search (and a
// frozen UI) whenever they were pressed before the background precompute
// thread had an answer ready. Now they instead defer: show a pulsing
// "Myślę" under the game clock and carry out the requested action only once
// the background result is in, so the UI never blocks.
enum PendingAction { PA_NONE, PA_HINT, PA_AUTOMOVE };
static bool          g_thinking      = false;
static PendingAction g_pendingAction = PA_NONE;

static void startThinking(PendingAction act){
   g_pendingAction = act;
   g_thinking = true;
}

// ── "Samograj" (self-play) state ────────────────────────────────────────────
// Declared here (definitions live further down, near doAutoMove()) so
// newGame()/loadGameFrom() can cancel a running self-play session when the
// deal on screen is being replaced.
static bool g_samogranoActive = false;
static void cancelSamograj();

static DealAnimator   g_deal;
static FireworkSystem g_fw;
static MoveHint       g_hint;
static bool           g_hintActive  = false;
static bool           g_hintBlinking= false;   // true during blink animation
static int            g_hintBlinkCount = 0;    // 0..5 (3 on + 3 off cycles)
static bool           g_hintShowSrc  = false;  // blink phase: show source
static bool           g_hintShowDst  = false;  // blink phase: show dest
static std::vector<MoveHint> g_hintList;        // all available hints
static int            g_hintIndex = 0;          // current hint in list
static bool           g_dealing   = false;
static bool           g_gameReady = false; // set after first game loaded; gates WM_SIZE snap
static bool           g_needsFirstLayout = false; // deferred layout after load
static std::wstring   g_status;
// Move counter label position (in game-window coordinates, i.e. with OY subtracted)
static float          g_moveLabelX   = -1.f; // -1 = use default (auto)
static float          g_moveLabelY   = -1.f; // -1 = use default (auto)
static bool           g_moveLabelCustom = false; // true once user has dragged it
static bool           g_moveLabelDragging = false;
static float          g_moveLabelDragOffX = 0.f;
static float          g_moveLabelDragOffY = 0.f;
static D2D1_RECT_F    g_moveLabelDrawnRect = {0,0,0,0}; // last drawn bounds (game coords)
static bool           g_won=false, g_noMoves=false;
static std::wstring   g_congratsMsg;  // non-empty = show after animation
static bool           g_noMovesDialogOpen=false; // prevent showing dialog twice
static bool           g_isFS=false;
static RECT           g_winRect;
static DWORD          g_winStyle=0;
static ULONG_PTR g_gdipToken=0;

// ── Settings ───────────────────────────────────────────────────────────────
static const int ID_HELP     = 206;
static const int ID_SETTINGS = 207;
static const int ID_STATS    = 208;
static const int ID_SAVE_AS  = 209;
static const int ID_LOAD_FROM= 210;
static const int ID_PLAY_NUM = 211;
static const int ID_PLAY_WIN = 212;
static const int ID_PLAY_LOST= 215;
static const int ID_RESTART  = 214;
// Stats: [mode][outcome] mode=0 King/1 Free, outcome=0 loss/1 win
static int g_statsGames[2]      = {0,0};   // total games started per mode
static int g_statsWins[2]       = {0,0};   // wins per mode
// Sum of moves for averaging: [mode][0=losses, 1=wins]
static int g_statsMoveSum[2][2] = {{0,0},{0,0}};
// Time stats [mode][0=loss, 1=win]: sum of seconds
static int g_statsTimeSum[2][2]  = {{0,0},{0,0}};
// Records [mode]: 0=best (min) moves to win, 1=best (min) time to win (seconds)
// -1 = no record yet
static int g_statsRecordMoves[2] = {-1,-1};
static int g_statsRecordTime[2]  = {-1,-1};
// Current game timer
static DWORD g_gameStartTick = 0;   // GetTickCount() at game start
static bool  g_timerPaused = false; // true while the game window is inactive
static DWORD g_pauseStartTick = 0;  // GetTickCount() when the pause began
static int   g_gameSeconds   = 0;   // elapsed seconds this game

// Format a duration in seconds as "MM:SS", or "H:MM:SS" once it reaches an hour.
static std::wstring fmtTime(int totalSeconds){
   if(totalSeconds<0) totalSeconds=0;
   int hh=totalSeconds/3600, mm=(totalSeconds%3600)/60, ss=totalSeconds%60;
   wchar_t b[24];
   if(hh>0) wsprintfW(b,L"%d:%02d:%02d",hh,mm,ss);
   else     wsprintfW(b,L"%02d:%02d",mm,ss);
   return std::wstring(b);
}
static int g_moveCount   = 0;     // moves in current game
static bool g_gameStarted= false; // at least one move made
static bool g_winCounted = false; // win already recorded for this game

static bool g_noMovesReached = false; // genuine no-moves loss already recorded for this game
static bool g_noMovesDialogShown = false; // "no moves" dialog already shown once this deal
static bool g_statsExcluded = false; // true for "try again" replays — don't count in stats
static int  g_outcomeMode = 0;       // mode captured at the moment win/no-moves was reached
static int  g_outcomeMoveCount = 0;  // move count captured at that same moment
static int  g_outcomeSeconds = 0;    // elapsed seconds captured at that same moment
static long long g_currentGameNumber = -1; // seed of the current deal (-1 = none yet)

// Auto-move anti-loop: track recent auto-moves to detect cycles
static const int AUTO_HISTORY_SIZE = 20; // "porównuj do dwudziestego ruchu wstecz"
static int g_autoHistory[AUTO_HISTORY_SIZE];  // ring buffer of move keys
static int g_autoHistoryHead = 0;
static int g_autoHistoryCount= 0;

// A second, independent anti-loop mechanism alongside the move-key ring
// buffer above: tracks the CANONICAL BOARD STATE (see boardCanonicalHash() in
// game.h) reached by each of the last up-to-20 automatic moves. The move-key
// check alone only catches a card going somewhere and immediately coming
// straight back; it can't catch a longer repeating pattern that cycles
// through several different cards/columns but keeps returning to a board
// position already seen (which happens in practice, especially in
// near-solved endgame positions with just a few cards left to shuffle) —
// this catches that by comparing STATES rather than individual moves.
static const int BOARD_HISTORY_SIZE = 20;
static uint64_t g_boardStateHistory[BOARD_HISTORY_SIZE];
static int g_boardStateHistoryHead = 0;
static int g_boardStateHistoryCount = 0;

// Third, independent anti-loop signal: total foundation card count is
// monotonic non-decreasing during real play, and completely indifferent to
// WHICH cards/columns are involved — unlike the two exact-repeat checks
// above. Testing turned up an endgame where a queen/king dance across three
// columns was interleaved with an unrelated card bouncing on and off a
// foundation slot: neither sub-pattern alone repeated a single move, or even
// the FULL board state, within any 20-move window (the two cycles are out of
// phase with each other), yet together they made zero net progress forever.
//
// A plain "did the last move add to a foundation" check isn't enough either:
// a foundation "buffer" move (rule 14, moving a card back to the table) can
// later be undone by putting the very same card right back — that briefly
// LOOKS like a fresh gain but is really just restoring a level already
// reached, and testing found exactly this (the same 6H/7H see-sawing on and
// off a foundation slot indefinitely, occasionally resetting a naive counter
// forever). So this tracks a HIGH-WATER MARK of foundation count reached
// since the last reset (new game / any manual interaction, same points that
// reset the two histories above) — only a move that pushes the total past
// its best point so far counts as genuine progress. FOUNDATION_STALL_LIMIT
// is set well above AUTO_HISTORY_SIZE/BOARD_HISTORY_SIZE (2.5x) so
// legitimate multi-step maneuvers (e.g. digging out an empty column) have
// plenty of room.
static const int FOUNDATION_STALL_LIMIT = 50;
static int g_movesSinceFoundationGain = 0;
static int g_maxFoundationSinceReset = -1; // -1 = "not yet observed this session"

static int foundationCardCount(){
   int n=0;
   for(int f=0; f<NUM_FOUND; f++) n += (int)g_game.found[f].size();
   return n;
}
// Call once after ANY successful automatic move that could have changed
// found[] (i.e. right after mutating it), to keep the high-water mark and
// the stall counter current.
static void noteFoundationProgress(){
   int fc = foundationCardCount();
   if(g_maxFoundationSinceReset<0){ g_maxFoundationSinceReset=fc; return; }
   if(fc > g_maxFoundationSinceReset){ g_maxFoundationSinceReset=fc; g_movesSinceFoundationGain=0; }
   else g_movesSinceFoundationGain++;
}

// EXPERIMENTAL (candidate fix #1, being trialled): real, cross-real-move
// persistent counterpart to scoreMove()'s per-search-line `artificialSince`
// tracking (see game.h's s_realArtificialSince comment) — maintained only
// across real AUTOMATIC moves (updateRealArtificialSince(), called from
// doAutoMove()'s three commit branches) and reset, like the anti-loop
// history above, on any manual interaction (clearAutoHistory()). Ages beyond
// REAL_ARTIFICIAL_WINDOW real moves are forgotten (treated as genuinely old,
// full reward) rather than discounted forever, so this only ever suppresses
// QUICK round trips — never penalizes a column that has legitimately been
// part of play for a while.
static const int REAL_ARTIFICIAL_WINDOW = 20; // matches AUTO_HISTORY_SIZE's own window
static std::array<int,NUM_COLS> g_realArtificialSince = []{
   std::array<int,NUM_COLS> a; a.fill(-1); return a;
}(); // -1 = untracked (must start this way, not zero-initialized)
static int g_realMoveCounter = 0;
// EXPERIMENTAL (candidate fix #2, trialled ALONGSIDE #1): real "foundation
// retrieval cooldown" tracking for rule 14 — see game.h's s_realFoundSentAt
// comment. Shares g_realMoveCounter with candidate #1 above (both just mean
// "how many real automatic moves have happened").
static std::array<int,56> g_realFoundSentAt = []{
   std::array<int,56> a; a.fill(-1); return a;
}();
static void updateRealArtificialSince(const MoveHint& h){
   int ply = ++g_realMoveCounter;
   g_realArtificialSince = nextArtificialSince(g_game, g_realArtificialSince, h, ply);
   for(int c=0;c<NUM_COLS;c++){
      if(g_realArtificialSince[c]>=0 && ply-g_realArtificialSince[c] > REAL_ARTIFICIAL_WINDOW)
         g_realArtificialSince[c] = -1;
   }
   if(h.fromType==LOC_COLUMN && h.toType==LOC_FOUNDATION){
      const Card& c = g_game.cols[h.fromIdx][h.fromCard];
      g_realFoundSentAt[GameState::cardIndex(c)] = ply;
   }
}

static void clearAutoHistory(){
   g_autoHistoryHead=0; g_autoHistoryCount=0;
   g_boardStateHistoryHead=0; g_boardStateHistoryCount=0;
   g_movesSinceFoundationGain=0;
   g_maxFoundationSinceReset=-1;
   g_realArtificialSince.fill(-1);
   g_realMoveCounter=0;
   g_realFoundSentAt.fill(-1);
}
static bool boardStateInHistory(uint64_t h){
   for(int i=0;i<g_boardStateHistoryCount;i++){
      int idx=(g_boardStateHistoryHead-1-i+BOARD_HISTORY_SIZE)%BOARD_HISTORY_SIZE;
      if(g_boardStateHistory[idx]==h) return true;
   }
   return false;
}
static void pushBoardStateHistory(uint64_t h){
   g_boardStateHistory[g_boardStateHistoryHead]=h;
   g_boardStateHistoryHead=(g_boardStateHistoryHead+1)%BOARD_HISTORY_SIZE;
   if(g_boardStateHistoryCount<BOARD_HISTORY_SIZE) g_boardStateHistoryCount++;
}
// Encoded by WHICH CARD moved and between which two columns — not by its
// position (fromCard index) in the source column. Position-based encoding
// can't detect a later reversal reliably: after a card lands somewhere, its
// index there generally differs from wherever it started, so reconstructing
// "the reverse move's key" from indices alone doesn't work in general (it
// only worked by coincidence when the card happened to be alone in its
// column). Card identity has no such problem — the same physical card
// moving back between the same two columns is unambiguous however deep in
// either column it was sitting.
static int  cardCode(const Card& c){ return (int)c.suit*13+(int)c.rank; }
static int  encodeAutoMove(int cardId,int fromCol,int toCol){
   return cardId*10000+fromCol*100+toCol;
}
static bool autoMoveInHistory(int key){
   for(int i=0;i<g_autoHistoryCount;i++){
      int idx=(g_autoHistoryHead-1-i+AUTO_HISTORY_SIZE)%AUTO_HISTORY_SIZE;
      if(g_autoHistory[idx]==key) return true;
   }
   return false;
}
static void pushAutoHistory(int key){
   g_autoHistory[g_autoHistoryHead]=key;
   g_autoHistoryHead=(g_autoHistoryHead+1)%AUTO_HISTORY_SIZE;
   if(g_autoHistoryCount<AUTO_HISTORY_SIZE) g_autoHistoryCount++;
}
static void saveStats();  // forward declaration
void newGame(bool sameDeal=false, long long gameNumber=-1, bool skipCredit=false, bool silent=false); // forward declaration
void newGameRetry();      // forward declaration — replays the same deal, excluded from stats

// Free column mode: false = King only, true = any card
static bool g_freeColMode = false;

// Check GitHub for a newer release at startup (Settings → Ogólne). See update.h.
static bool g_checkUpdatesOnStart = true;

// User-configurable AI search depth (Settings dialog), separately per mode —
// see game.h's s_searchDepthFree/s_searchDepthKing comment. Defaults match
// the engine's own previous hardcoded values (free mode: 4, doubled to 8
// whenever a column is empty — see rankCandidates(); king-only mode: 5,
// never doubled).
static const int MAX_SEARCH_DEPTH = 20; // upper bound accepted in Settings / pasjans.ini (game.h clamps to the same value)
static int g_searchDepthFree = 4;
static int g_searchDepthKing = 5;

// Background color choices
struct BgColor { const wchar_t* name; BYTE r,g,b; };
static const BgColor BG_COLORS[] = {
   {L"Zielone sukno",  20,100, 40},
   {L"Ciemnozielone",   0, 70, 20},
   {L"Granatowe",      20, 40,100},
   {L"Ciemnoniebieskie",10, 20, 80},
   {L"Bordowe",        80, 10, 20},
   {L"Ciemnoszare",    40, 40, 40},
   {L"Czarne",          5,  5,  5},
   {L"Fioletowe",      55, 20, 80},
   {L"Oliwkowe",       60, 60, 10},
   {L"Turkusowe",       0, 70, 70},
};
static const int BG_COUNT = 10;
static int  g_bgIndex   = 0;   // current background
static float g_volume   = 1.0f; // 0..1
static int   g_moveHighlightMode = 0; // 0=Nie zaznaczaj, 1=Obrys sekwencji, 2=Przyciemnij niemo\u017cliwe

// IDs for no-moves dialog buttons
static const int NM_NEW     = 101;
static const int NM_QUIT    = 102;
static const int NM_STATS   = 103;
static const int NM_CLOSE   = 104; // closed via X / Escape — do nothing
static const int NM_RETRY   = 105; // replay the same deal from scratch
static const int NM_CONTINUE= 106; // "Kontynuuj układ" — same as NM_CLOSE, just an explicit button
static void showStats(HWND parent); // forward declaration

static INT_PTR CALLBACK noMovesDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM){
   switch(msg){
   case WM_INITDIALOG:
      return TRUE;
   case WM_COMMAND:
      switch(LOWORD(wp)){
      case NM_NEW:     EndDialog(dlg, NM_NEW);     return TRUE;
      case NM_QUIT:    EndDialog(dlg, NM_QUIT);    return TRUE;
      case NM_STATS:   EndDialog(dlg, NM_STATS);   return TRUE;
      case NM_RETRY:   EndDialog(dlg, NM_RETRY);   return TRUE;
      case NM_CONTINUE:EndDialog(dlg, NM_CONTINUE);return TRUE;
      case IDCANCEL:   EndDialog(dlg, NM_CLOSE);   return TRUE; // Esc key
      }
      break;
   case WM_CLOSE:
      EndDialog(dlg, NM_CLOSE);
      return TRUE;
   }
   return FALSE;
}

// Write a WCHAR string (including null terminator) into buf, return new position
static BYTE* writeDlgStr(BYTE* p, const wchar_t* s){
   while(*s){ *(WCHAR*)p=*s++; p+=2; }
   *(WCHAR*)p=0; p+=2;
   return p;
}

// Align pointer to DWORD boundary
static BYTE* dwordAlign(BYTE* p){
   ULONG_PTR v=(ULONG_PTR)p;
   return (BYTE*)((v+3)&~(ULONG_PTR)3);
}

// Append a dialog item (button or static) to buffer, return new position
static BYTE* appendDlgItem(BYTE* p, DWORD style, short x, short y,
                           short cx, short cy, WORD id,
                           WORD clsAtom, const wchar_t* text){
   p=dwordAlign(p);
   DLGITEMTEMPLATE* it=(DLGITEMTEMPLATE*)p;
   it->style          =style;
   it->dwExtendedStyle=0;
   it->x=x; it->y=y; it->cx=cx; it->cy=cy;
   it->id=id;
   p+=sizeof(DLGITEMTEMPLATE);
   *(WORD*)p=0xFFFF; p+=2;   // class by atom
   *(WORD*)p=clsAtom; p+=2;
   p=writeDlgStr(p,text);
   *(WORD*)p=0; p+=2;         // no creation data
   return p;
}

static wchar_t g_playNumBuf[16]={};

static INT_PTR CALLBACK playNumDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM){
   switch(msg){
   case WM_INITDIALOG:
      SetFocus(GetDlgItem(dlg,101));
      SendDlgItemMessageW(dlg,101,EM_SETLIMITTEXT,10,0); // max 10 digits (fits in 32-bit seed)
      return FALSE; // we set focus ourselves
   case WM_COMMAND:
      switch(LOWORD(wp)){
      case IDOK:
         GetDlgItemTextW(dlg,101,g_playNumBuf,16);
         EndDialog(dlg, IDOK);
         return TRUE;
      case IDCANCEL:
         EndDialog(dlg, IDCANCEL);
         return TRUE;
      }
      break;
   case WM_CLOSE:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
   }
   return FALSE;
}

// "Zagraj pasjans numer..." — prompts for a number, then starts that exact deal
static void showPlayNumberDialog(HWND hwnd){
   const wchar_t* dlgTitle=L"Zagraj pasjans numer";
   const wchar_t* dlgFace =L"Segoe UI";
   const WORD     dlgPt   =9;

   const int BUFSZ=1024;
   BYTE* buf=(BYTE*)GlobalAlloc(GPTR,BUFSZ);
   if(!buf) return;

   DLGTEMPLATE* tmpl=(DLGTEMPLATE*)buf;
   tmpl->style          =DS_MODALFRAME|DS_CENTER|DS_SHELLFONT|
                         WS_POPUP|WS_CAPTION|WS_SYSMENU;
   tmpl->dwExtendedStyle=0;
   tmpl->cdit           =4; // label + edit + OK + Cancel
   tmpl->x=0; tmpl->y=0; tmpl->cx=200; tmpl->cy=70;

   BYTE* p=buf+sizeof(DLGTEMPLATE);
   *(WORD*)p=0; p+=2;   // no menu
   *(WORD*)p=0; p+=2;   // default dialog class
   p=writeDlgStr(p,dlgTitle);
   *(WORD*)p=dlgPt; p+=2;
   p=writeDlgStr(p,dlgFace);

   p=appendDlgItem(p, WS_CHILD|WS_VISIBLE, 8,10,184,12,
      (WORD)0xFFFF,0x0082, L"Podaj numer pasjansa (cyfry):");
   p=appendDlgItem(p, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,
      8,26,184,14, 101,0x0081, L"");
   p=appendDlgItem(p, WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
      36,46,60,16, IDOK,0x0080, L"OK");
   p=appendDlgItem(p, WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
      104,46,60,16, IDCANCEL,0x0080, L"Anuluj");

   g_playNumBuf[0]=0;
   INT_PTR res=DialogBoxIndirectW(
      GetModuleHandle(nullptr), (DLGTEMPLATE*)buf, hwnd, playNumDlgProc);
   GlobalFree(buf);

   if(res==IDOK && g_playNumBuf[0]){
      unsigned long long n=0;
      for(const wchar_t* c=g_playNumBuf; *c; c++)
         if(*c>=L'0' && *c<=L'9') n=n*10+(*c-L'0');
      newGame(false,(long long)(unsigned int)n); // wraps into the 32-bit seed range
   }
}

static void showNoMovesDialog(HWND hwnd){
   if(g_noMovesDialogOpen) return; // already shown — ignore duplicate call
   g_noMovesDialogOpen=true;
   g_noMovesDialogShown=true;
   // Cut off anything already playing (e.g. the "nono" sound from the failed
   // move attempt that revealed this no-moves state) so it doesn't overlap
   // with the dialog's own sound.
   SoundSystem::instance().fadeOutAll(0);
   playSound("koniec",g_volume);

   // Build dialog template in memory
   // Header layout (DS_SHELLFONT variant):
   //   DLGTEMPLATE, menu=0, cls=0, title, pointSize, faceName
   const wchar_t* dlgTitle=L"Brak ruch\u00f3w";
   const wchar_t* dlgFace =L"Segoe UI";
   const WORD     dlgPt   =9;

   const int BUFSZ=2048;
   BYTE* buf=(BYTE*)GlobalAlloc(GPTR,BUFSZ);
   if(!buf) return;

   // Fill DLGTEMPLATE
   DLGTEMPLATE* tmpl=(DLGTEMPLATE*)buf;
   tmpl->style         =DS_MODALFRAME|DS_CENTER|DS_SHELLFONT|
                        WS_POPUP|WS_CAPTION|WS_SYSMENU;
   tmpl->dwExtendedStyle=0;
   tmpl->cdit          =6;   // 1 static + 5 buttons
   tmpl->x=0; tmpl->y=0; tmpl->cx=220; tmpl->cy=108;

   BYTE* p=buf+sizeof(DLGTEMPLATE);
   *(WORD*)p=0; p+=2;   // no menu
   *(WORD*)p=0; p+=2;   // default dialog class
   p=writeDlgStr(p,dlgTitle);
   // DS_SHELLFONT: point size + face name
   *(WORD*)p=dlgPt; p+=2;
   p=writeDlgStr(p,dlgFace);

   // Item 0: static text (atom 0x0082)
   p=appendDlgItem(p,
      WS_CHILD|WS_VISIBLE|SS_CENTER,
      8,10,204,20,
      (WORD)0xFFFF,0x0082,
      L"Brak sensownych ruch\u00f3w. Co teraz?");

   // Buttons (atom 0x0080): "Kontynuuj układ" full-width on its own row,
   // then the rest in a 2×2 grid below
   struct { short x,y,cx; WORD id; const wchar_t* label; } btns[]={
      {  8,34,204, NM_CONTINUE,L"Kontynuuj uk\u0142ad"     },
      {  8,56,100, NM_NEW,     L"Nowa gra"           },
      {112,56,100, NM_RETRY,   L"Spr\u00f3buj jeszcze raz" },
      {  8,78,130, NM_QUIT,    L"Zamknij pasjansa"   },
      {142,78, 70, NM_STATS,   L"Statystyki"         }
   };
   for(auto& b:btns){
      p=appendDlgItem(p,
         WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
         b.x,b.y,b.cx,14,
         b.id,0x0080,b.label);
   }

   INT_PTR res=DialogBoxIndirectW(
      GetModuleHandle(nullptr),
      (DLGTEMPLATE*)buf,
      hwnd,
      noMovesDlgProc);

   GlobalFree(buf);
   g_noMovesDialogOpen=false;
   SoundSystem::instance().fadeOutAll(300);

   if(res==NM_NEW){ g_noMoves=false; newGame(); }
   if(res==NM_RETRY){ g_noMoves=false; newGameRetry(); }
   if(res==NM_QUIT) DestroyWindow(hwnd);
   if(res==NM_STATS) showStats(hwnd);
   // NM_CLOSE (X/Escape) and NM_CONTINUE ("Kontynuuj układ"): do nothing,
   // leave the no-moves state as is — g_noMovesDialogShown keeps this dialog
   // from popping up again until a new deal starts.
}

static BYTE bgR(){ return BG_COLORS[g_bgIndex].r; }
static BYTE bgG(){ return BG_COLORS[g_bgIndex].g; }
static BYTE bgB(){ return BG_COLORS[g_bgIndex].b; }

// ── INI persistence ────────────────────────────────────────────────────────
// Returns "<exeDir>\<name>\", creating the subdirectory first if it doesn't
// already exist (CreateDirectoryW: harmless no-op if it's already there).
// Used to keep the exe's own directory tidy — Solved*.dat replays, custom
// sound overrides and manual save games each get their own subfolder instead
// of piling up loose next to the exe.
static std::wstring exeDirSubfolder(const wchar_t* name){
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(nullptr,path,MAX_PATH);
   wchar_t* last=wcsrchr(path,L'\\');
   if(last) *(last+1)=0;
   std::wstring dir=std::wstring(path)+name;
   CreateDirectoryW(dir.c_str(),nullptr);
   return dir+L"\\";
}

static std::wstring getIniPath(){
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(nullptr,path,MAX_PATH);
   wchar_t* last=wcsrchr(path,L'\\');
   if(last) *(last+1)=0;
   return std::wstring(path)+L"pasjans.ini";
}

// WonNumbers.csv: every winning game's number and mode, one per line.
// Plain CSV (not INI) because it's a growing log of records, not settings —
// CSV is the natural fit and stays easy to open in Excel if the user is curious.
static std::wstring getWonNumbersPath(){
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(nullptr,path,MAX_PATH);
   wchar_t* last=wcsrchr(path,L'\\');
   if(last) *(last+1)=0;
   return std::wstring(path)+L"WonNumbers.csv";
}

static void appendWonNumber(long long gameNumber, int mode){
   if(gameNumber<0) return;
   std::wstring path=getWonNumbersPath();
   bool exists=(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES);
   FILE* f=nullptr;
   // Plain binary append — no text-mode / encoding-conversion CRT layer to
   // second-guess our line endings. Content is always plain ASCII (digits,
   // commas, letters), so there's no need for wide/UTF-8 text mode at all;
   // that mode was what caused doubled "\r\r\n" line endings (the CRT's own
   // \n→\r\n translation stacking with the \r\n we wrote ourselves), and in
   // at least one case dropped a line break entirely, gluing two records
   // together on one line.
   _wfopen_s(&f,path.c_str(),L"ab");
   if(!f) return;
   if(!exists){
      const char* header="GameNumber,Mode\r\n";
      fwrite(header,1,strlen(header),f);
   }
   char line[64];
   int n=sprintf_s(line,sizeof(line),"%lld,%s\r\n",
      gameNumber, mode==0?"TylkoKrol":"DowolnaKarta");
   if(n>0) fwrite(line,1,(size_t)n,f);
   fclose(f);
}

// ── LostNumbers.csv / Unsolvable.csv ─────────────────────────────────────────
// Same layout as WonNumbers.csv ("GameNumber,Mode"). LostNumbers.csv collects the
// deals that ended in "no moves" or in the auto-player getting stuck; the Solver
// (toolbar) works through that list, moving each deal it solves to WonNumbers.csv
// (with a Solved<number>.dat replay) and each one it gives up on to Unsolvable.csv.
struct NumEntry { long long num; int mode; }; // mode: 0 = TylkoKrol, 1 = DowolnaKarta
static std::wstring exeDirFile(const wchar_t* name){
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(nullptr,path,MAX_PATH);
   wchar_t* last=wcsrchr(path,L'\\');
   if(last) *(last+1)=0;
   return std::wstring(path)+name;
}

// ── Solver.log ────────────────────────────────────────────────────────────
// One line per deal the Solver actually solves: seed, total time spent
// searching for it (added up across every "keep searching?" resume — see
// SolverSaved/solverStartCurrent — not just this final stage), how many
// attempts finished, and how many positions were searched in total. Plain
// ASCII / binary-mode append, same reasoning as appendWonNumber() above
// (avoids the CRT text-mode \n->\r\n translation doubling up with the \r\n
// written here).
static std::wstring getSolverLogPath(){ return exeDirFile(L"Solver.log"); }
static void appendSolverLog(long long gameNumber, ULONGLONG elapsedMs, int attemptsFinished, long long nodesSearched, int winningMoves){
   FILE* f=nullptr; _wfopen_s(&f,getSolverLogPath().c_str(),L"ab");
   if(!f) return;
   SYSTEMTIME t; GetLocalTime(&t);
   unsigned long long secs=elapsedMs/1000;
   char line[220];
   int n=sprintf_s(line,sizeof(line),
      "%04d-%02d-%02d %02d:%02d:%02d  Uklad nr %lld  Czas poszukiwan: %llu:%02llu:%02llu  Zakonczone proby: %d  Przeszukane pozycje: %lld  Ruchy do zwyciestwa: %d\r\n",
      t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,
      gameNumber, secs/3600, (secs/60)%60, secs%60,
      attemptsFinished, nodesSearched, winningMoves);
   if(n>0) fwrite(line,1,(size_t)n,f);
   fclose(f);
}
static std::wstring getLostNumbersPath(){  return exeDirFile(L"LostNumbers.csv"); }
static std::wstring getUnsolvablePath(){   return exeDirFile(L"Unsolvable.csv"); }
static std::vector<NumEntry> readNumberFile(const std::wstring& path){
   std::vector<NumEntry> v;
   FILE* f=nullptr; _wfopen_s(&f,path.c_str(),L"rb");
   if(!f) return v;
   char line[128];
   while(fgets(line,128,f)){
      if(strncmp(line,"GameNumber",10)==0) continue;
      long long n=_atoi64(line);
      if(n<0 || (n==0 && line[0]!='0')) continue;
      v.push_back({n, strstr(line,"Dowolna")?1:0});
   }
   fclose(f);
   return v;
}
static void writeNumberFile(const std::wstring& path,const std::vector<NumEntry>& v){
   FILE* f=nullptr; _wfopen_s(&f,path.c_str(),L"wb");
   if(!f) return;
   fputs("GameNumber,Mode\r\n",f);
   for(auto& e: v) fprintf(f,"%lld,%s\r\n",e.num,e.mode==0?"TylkoKrol":"DowolnaKarta");
   fclose(f);
}
static bool numberFileHas(const std::wstring& path,long long num){
   for(auto& e: readNumberFile(path)) if(e.num==num) return true;
   return false;
}
static void appendNumberEntry(const std::wstring& path,long long num,int mode){
   if(num<0 || numberFileHas(path,num)) return;
   auto v=readNumberFile(path); v.push_back({num,mode});
   writeNumberFile(path,v);
}
static void removeNumberEntry(const std::wstring& path,long long num){
   auto v=readNumberFile(path);
   size_t before=v.size();
   v.erase(std::remove_if(v.begin(),v.end(),[&](const NumEntry& e){ return e.num==num; }),v.end());
   if(v.size()!=before) writeNumberFile(path,v);
}
// A deal that ended lost (no moves left, or the auto-player got stuck).
static void recordLostDeal(long long num,int mode){
   if(num<0) return;
   if(numberFileHas(getUnsolvablePath(),num)) return; // already known to be out of reach
   appendNumberEntry(getLostNumbersPath(),num,mode);
}

// Reads all recorded winning game numbers (optionally filtering by mode —
// not currently used, but kept simple: caller just gets numbers) and starts
// a random one. Shows a message if none are recorded yet.
static void playRandomWinningNumber(HWND hwnd){
   std::wstring path=getWonNumbersPath();
   FILE* f=nullptr;
   _wfopen_s(&f,path.c_str(),L"rb");
   if(!f){
      MessageBoxW(hwnd,L"Nie zapisano jeszcze żadnej wygranej gry.",
         L"Zagraj wygrywający",MB_OK|MB_ICONINFORMATION);
      return;
   }
   std::vector<long long> nums;
   char line[128];
   bool first=true;
   while(fgets(line,128,f)){
      if(first){ first=false; if(strncmp(line,"GameNumber",10)==0) continue; }
      long long n=_atoi64(line);
      if(n>0) nums.push_back(n);
   }
   fclose(f);
   if(nums.empty()){
      MessageBoxW(hwnd,L"Nie zapisano jeszcze żadnej wygranej gry.",
         L"Zagraj wygrywający",MB_OK|MB_ICONINFORMATION);
      return;
   }
   std::random_device rd;
   std::mt19937 gen(rd());
   std::uniform_int_distribution<size_t> dist(0,nums.size()-1);
   newGame(false,nums[dist(gen)]);
   // Replaying an already-won deal is practice, not a fresh attempt — never
   // counted towards statistics.
   g_statsExcluded=true;
}

static void saveSettings(){
   std::wstring ini=getIniPath();
   wchar_t buf[64];
   wsprintfW(buf,L"%d",g_bgIndex);
   WritePrivateProfileStringW(L"Settings",L"BgIndex",buf,ini.c_str());
   wsprintfW(buf,L"%d",(int)(g_volume*100.f+0.5f));
   WritePrivateProfileStringW(L"Settings",L"Volume",buf,ini.c_str());
   wsprintfW(buf,L"%d",(int)g_freeColMode);
   WritePrivateProfileStringW(L"Settings",L"FreeColMode",buf,ini.c_str());
   wsprintfW(buf,L"%d",g_searchDepthFree);
   WritePrivateProfileStringW(L"Settings",L"SearchDepthFree",buf,ini.c_str());
   wsprintfW(buf,L"%d",g_searchDepthKing);
   WritePrivateProfileStringW(L"Settings",L"SearchDepthKing",buf,ini.c_str());
   wsprintfW(buf,L"%d",g_moveHighlightMode);
   WritePrivateProfileStringW(L"Settings",L"MoveHighlightMode",buf,ini.c_str());
   wsprintfW(buf,L"%d",g_animSpeedStep);
   WritePrivateProfileStringW(L"Settings",L"AnimSpeedStep",buf,ini.c_str());
   WritePrivateProfileStringW(L"Settings",L"CheckUpdatesOnStart",g_checkUpdatesOnStart?L"1":L"0",ini.c_str());
   // Save move label position (only if user has manually placed it)
   if(g_moveLabelCustom){
      wsprintfW(buf,L"%.1f,%.1f",g_moveLabelX,g_moveLabelY);
      WritePrivateProfileStringW(L"Settings",L"MoveLabelPos",buf,ini.c_str());
   } else {
      WritePrivateProfileStringW(L"Settings",L"MoveLabelPos",L"",ini.c_str());
   }
   // Save custom sound paths / mute flags
   for(int i=0;i<SOUND_COUNT;i++){
      const std::wstring& p=SoundSystem::instance().customPath(i);
      std::wstring key=std::wstring(L"Sound_")+std::to_wstring(i);
      WritePrivateProfileStringW(L"Sounds",key.c_str(),p.c_str(),ini.c_str());
      std::wstring mkey=key+L"_Muted";
      WritePrivateProfileStringW(L"Sounds",mkey.c_str(),
         SoundSystem::instance().isMuted(i)?L"1":L"0",ini.c_str());
   }
   // Save key bindings
   saveKeyBindings(ini);
   // Save window placement
   if(g_hwnd){
      WINDOWPLACEMENT wp={sizeof(wp)};
      GetWindowPlacement(g_hwnd,&wp);
      RECT& r=wp.rcNormalPosition;
      wsprintfW(buf,L"%d,%d,%d,%d,%d",r.left,r.top,r.right,r.bottom,(int)wp.showCmd);
      WritePrivateProfileStringW(L"Window",L"Placement",buf,ini.c_str());
   }
}

static void saveWindowPlacement(){
   saveSettings();  // reuse saveSettings to avoid duplicating INI path logic
}

static void loadSettings(){
   std::wstring ini=getIniPath();
   g_bgIndex=(int)GetPrivateProfileIntW(L"Settings",L"BgIndex",0,ini.c_str());
   if(g_bgIndex<0||g_bgIndex>=BG_COUNT) g_bgIndex=0;
   int vol=(int)GetPrivateProfileIntW(L"Settings",L"Volume",100,ini.c_str());
   if(vol<0) vol=0; if(vol>100) vol=100;
   g_volume=vol/100.f;
   g_freeColMode=(GetPrivateProfileIntW(L"Settings",L"FreeColMode",0,ini.c_str())!=0);
   g_searchDepthFree=(int)GetPrivateProfileIntW(L"Settings",L"SearchDepthFree",4,ini.c_str());
   if(g_searchDepthFree<1) g_searchDepthFree=4; else if(g_searchDepthFree>MAX_SEARCH_DEPTH) g_searchDepthFree=MAX_SEARCH_DEPTH;
   g_searchDepthKing=(int)GetPrivateProfileIntW(L"Settings",L"SearchDepthKing",5,ini.c_str());
   if(g_searchDepthKing<1) g_searchDepthKing=5; else if(g_searchDepthKing>MAX_SEARCH_DEPTH) g_searchDepthKing=MAX_SEARCH_DEPTH;
   g_moveHighlightMode=(int)GetPrivateProfileIntW(L"Settings",L"MoveHighlightMode",0,ini.c_str());
   if(g_moveHighlightMode<0||g_moveHighlightMode>2) g_moveHighlightMode=0;
   applyAnimSpeedStep((int)GetPrivateProfileIntW(L"Settings",L"AnimSpeedStep",0,ini.c_str()));
   g_checkUpdatesOnStart=(GetPrivateProfileIntW(L"Settings",L"CheckUpdatesOnStart",1,ini.c_str())!=0);
   // Load move label position
   wchar_t posBuf[64]={};
   GetPrivateProfileStringW(L"Settings",L"MoveLabelPos",L"",posBuf,64,ini.c_str());
   if(posBuf[0]){
      float lx=0,ly=0;
      if(swscanf_s(posBuf,L"%f,%f",&lx,&ly)==2){
         g_moveLabelX=lx; g_moveLabelY=ly; g_moveLabelCustom=true;
      }
   }
   // Load custom sound paths / mute flags
   for(int i=0;i<SOUND_COUNT;i++){
      std::wstring key=std::wstring(L"Sound_")+std::to_wstring(i);
      wchar_t spath[MAX_PATH]={};
      GetPrivateProfileStringW(L"Sounds",key.c_str(),L"",spath,MAX_PATH,ini.c_str());
      if(spath[0]) SoundSystem::instance().setCustomPath(i,std::wstring(spath));
      std::wstring mkey=key+L"_Muted";
      bool muted=(GetPrivateProfileIntW(L"Sounds",mkey.c_str(),0,ini.c_str())!=0);
      SoundSystem::instance().setMuted(i,muted);
   }
}

static void loadWindowPlacement(HWND hwnd){
   std::wstring ini=getIniPath();
   wchar_t buf[128]={};
   GetPrivateProfileStringW(L"Window",L"Placement",L"",buf,128,ini.c_str());
   if(buf[0]==0) return;
   int l,t,r,b,cmd;
   if(swscanf_s(buf,L"%d,%d,%d,%d,%d",&l,&t,&r,&b,&cmd)==5){
      WINDOWPLACEMENT wp={sizeof(wp)};
      wp.rcNormalPosition={l,t,r,b};
      wp.showCmd=(cmd==SW_MAXIMIZE)?SW_MAXIMIZE:SW_SHOWNORMAL;
      SetWindowPlacement(hwnd,&wp);
   }
}


// ── Game state persistence ──────────────────────────────────────────────────
static std::wstring getSavePath(){
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(nullptr,path,MAX_PATH);
   wchar_t* last=wcsrchr(path,L'\\');
   if(last) *(last+1)=0;
   return std::wstring(path)+L"pasjans_save.dat";
}

// Manual save/load (Zapisz grę jako.../Wczytaj grę z pliku...) always propose
// the "Saves" subfolder — no longer remembers whatever directory was used
// last (the old LastSaveDir ini key): a fixed, predictable location is what
// "zawsze proponuj w podkatalogu Saves" asked for. The user can still browse
// elsewhere by hand; only the dialog's own starting point is fixed.
static std::wstring getLastSaveDir(){ return exeDirSubfolder(L"Saves"); }

// Write/read a single vector of cards (shared by board state and undo/redo snapshots)
static void writeCardVec(FILE* f, const std::vector<Card>& v){
   BYTE n=(BYTE)std::min((int)v.size(),255);
   fwrite(&n,1,1,f);
   for(int i=0;i<n;i++){
      BYTE s=(BYTE)v[i].suit, r=(BYTE)v[i].rank;
      fwrite(&s,1,1,f); fwrite(&r,1,1,f);
   }
}
static bool readCardVec(FILE* f, std::vector<Card>& v){
   BYTE n=0; if(fread(&n,1,1,f)!=1) return false;
   v.clear(); v.reserve(n);
   for(int i=0;i<n;i++){
      BYTE s=0,r=0;
      if(fread(&s,1,1,f)!=1||fread(&r,1,1,f)!=1) return false;
      if(s>3||r<1||r>13) return false;
      v.push_back({(Suit)s,(Rank)r});
   }
   return true;
}
static void writeSnapshot(FILE* f, const Snapshot& s){
   for(int i=0;i<NUM_COLS;i++)  writeCardVec(f,s.cols[i]);
   for(int i=0;i<NUM_FOUND;i++) writeCardVec(f,s.found[i]);
   writeCardVec(f,s.reserve);
   BYTE db=(BYTE)(s.isDealBoundary?1:0);
   fwrite(&db,1,1,f);
}
static bool readSnapshot(FILE* f, Snapshot& s){
   for(int i=0;i<NUM_COLS;i++)  if(!readCardVec(f,s.cols[i]))  return false;
   for(int i=0;i<NUM_FOUND;i++) if(!readCardVec(f,s.found[i])) return false;
   if(!readCardVec(f,s.reserve)) return false;
   BYTE db=0;
   if(fread(&db,1,1,f)!=1) return false;
   s.isDealBoundary=(db!=0);
   return true;
}
// Same as readSnapshot but for files written before v9 (no isDealBoundary byte)
static bool readSnapshotNoTag(FILE* f, Snapshot& s){
   for(int i=0;i<NUM_COLS;i++)  if(!readCardVec(f,s.cols[i]))  return false;
   for(int i=0;i<NUM_FOUND;i++) if(!readCardVec(f,s.found[i])) return false;
   if(!readCardVec(f,s.reserve)) return false;
   s.isDealBoundary=false;
   return true;
}

// Write game data to an already-open FILE* (shared by auto-save and manual save)
static void writeGameToFile(FILE* f){
   const char magic[]="PASJ";
   fwrite(magic,1,4,f);
   BYTE ver=12; fwrite(&ver,1,1,f);  // v11 adds current game number; v12 stores the undo/redo counts as 16-bit (solver replays are long)
   auto writeVec=[&](const std::vector<Card>& v){
      BYTE n=(BYTE)std::min((int)v.size(),255);
      fwrite(&n,1,1,f);
      for(int i=0;i<n;i++){
         BYTE s=(BYTE)v[i].suit, r=(BYTE)v[i].rank;
         fwrite(&s,1,1,f); fwrite(&r,1,1,f);
      }
   };
   for(int i=0;i<NUM_COLS; i++) writeVec(g_game.cols[i]);
   for(int i=0;i<NUM_FOUND;i++) writeVec(g_game.found[i]);
   writeVec(g_game.reserve);
   DWORD mc=(DWORD)g_moveCount;
   fwrite(&mc,4,1,f);
   BYTE freeMode=(BYTE)(g_freeColMode?1:0);
   fwrite(&freeMode,1,1,f);
   BYTE winCounted=(BYTE)(g_winCounted?1:0);
   fwrite(&winCounted,1,1,f);
   BYTE noMovesReached=(BYTE)(g_noMovesReached?1:0);
   fwrite(&noMovesReached,1,1,f);
   BYTE noMovesDialogShown=(BYTE)(g_noMovesDialogShown?1:0);
   fwrite(&noMovesDialogShown,1,1,f);
   BYTE statsExcluded=(BYTE)(g_statsExcluded?1:0);
   fwrite(&statsExcluded,1,1,f);
   BYTE outcomeMode=(BYTE)g_outcomeMode;
   fwrite(&outcomeMode,1,1,f);
   DWORD outcomeMoves=(DWORD)g_outcomeMoveCount;
   fwrite(&outcomeMoves,4,1,f);
   DWORD outcomeSecs=(DWORD)g_outcomeSeconds;
   fwrite(&outcomeSecs,4,1,f);
   writeSnapshot(f,g_game.initialDeal);
   DWORD gameNum=(DWORD)(g_currentGameNumber>=0?g_currentGameNumber:0xFFFFFFFFu);
   fwrite(&gameNum,4,1,f);
   DWORD secs=(DWORD)std::max(0,g_gameSeconds);
   fwrite(&secs,4,1,f);
   WORD un=(WORD)std::min((int)g_game.undoStack.size(),(int)GameState::MAX_UNDO_HISTORY);
   fwrite(&un,2,1,f);
   { int i=0; for(auto it=g_game.undoStack.begin(); i<un; ++it,++i) writeSnapshot(f,*it); }
   WORD rn=(WORD)std::min((int)g_game.redoStack.size(),(int)GameState::MAX_UNDO_HISTORY);
   fwrite(&rn,2,1,f);
   { int i=0; for(auto it=g_game.redoStack.begin(); i<rn; ++it,++i) writeSnapshot(f,*it); }
}

// Read game data from an already-open FILE* — returns true on success
static bool readGameFromFile(FILE* f){
   char magic[4]={}; fread(magic,1,4,f);
   if(magic[0]!='P'||magic[1]!='A'||magic[2]!='S'||magic[3]!='J') return false;
   BYTE ver=0; fread(&ver,1,1,f);
   if(ver<1 || ver>12) return false;
   auto readVec=[&](std::vector<Card>& v)->bool{
      BYTE n=0; if(fread(&n,1,1,f)!=1) return false;
      v.clear(); v.reserve(n);
      for(int i=0;i<n;i++){
         BYTE s=0,r=0;
         if(fread(&s,1,1,f)!=1||fread(&r,1,1,f)!=1) return false;
         if(s>3||r<1||r>13) return false;
         v.push_back({(Suit)s,(Rank)r});
      }
      return true;
   };
   for(int i=0;i<NUM_COLS; i++) if(!readVec(g_game.cols[i]))  return false;
   for(int i=0;i<NUM_FOUND;i++) if(!readVec(g_game.found[i])) return false;
   if(!readVec(g_game.reserve)) return false;
   if(ver>=2){
      DWORD mc=0;
      if(fread(&mc,4,1,f)==1) g_moveCount=(int)mc;
   } else {
      g_moveCount=0;
   }
   if(ver>=3){
      BYTE freeMode=0;
      if(fread(&freeMode,1,1,f)==1) g_freeColMode=(freeMode!=0);
   }
   if(ver>=4){
      BYTE wc=0;
      if(fread(&wc,1,1,f)==1) g_winCounted=(wc!=0);
   } else {
      g_winCounted=false;
   }
   if(ver>=7){
      BYTE nr=0;
      if(fread(&nr,1,1,f)==1) g_noMovesReached=(nr!=0);
   } else {
      g_noMovesReached=false;
   }
   if(ver>=8){
      BYTE nds=0;
      if(fread(&nds,1,1,f)==1) g_noMovesDialogShown=(nds!=0);
   } else {
      g_noMovesDialogShown=false;
   }
   if(ver>=9){
      BYTE se=0;
      if(fread(&se,1,1,f)==1) g_statsExcluded=(se!=0);
   } else {
      g_statsExcluded=false;
   }
   if(ver>=10){
      BYTE om=0; if(fread(&om,1,1,f)==1) g_outcomeMode=om;
      DWORD omv=0; if(fread(&omv,4,1,f)==1) g_outcomeMoveCount=(int)omv;
      DWORD osec=0; if(fread(&osec,4,1,f)==1) g_outcomeSeconds=(int)osec;
   }
   if(ver>=9){
      if(!readSnapshot(f,g_game.initialDeal)) return false;
   } else {
      // No remembered deal in older saves — "try again" won't be available
      // until the next fresh deal, so just clear it out.
      g_game.initialDeal=Snapshot{};
   }
   if(ver>=11){
      DWORD gameNum=0;
      if(fread(&gameNum,4,1,f)==1)
         g_currentGameNumber=(gameNum==0xFFFFFFFFu)?-1:(long long)gameNum;
   } else {
      g_currentGameNumber=-1; // unknown for older saves
   }
   if(ver>=5){
      DWORD secs=0;
      if(fread(&secs,4,1,f)==1) g_gameSeconds=(int)secs;
      if(g_gameSeconds<0 || g_gameSeconds>24*3600) g_gameSeconds=0; // sanity clamp: no game runs 24h+
   } else {
      g_gameSeconds=0;
   }
   if(ver<10){
      // Older saves never captured this — fall back to the plain move/time
      // counters (now freshly loaded above) so a pending win/loss from an
      // old save still gets credited reasonably when the next new game starts.
      g_outcomeMode=(int)g_freeColMode;
      g_outcomeMoveCount=g_moveCount;
      g_outcomeSeconds=g_gameSeconds;
   }
   if(ver>=6){
      g_game.undoStack.clear();
      g_game.redoStack.clear();
      int un=0;
      if(ver>=12){ WORD w=0; if(fread(&w,2,1,f)!=1) return false; un=w; }
      else       { BYTE b=0; if(fread(&b,1,1,f)!=1) return false; un=b; }
      for(int k=0;k<un;k++){
         Snapshot s;
         bool ok = (ver>=9) ? readSnapshot(f,s) : readSnapshotNoTag(f,s);
         if(!ok) return false;
         g_game.undoStack.push_back(s);
      }
      int rn=0;
      if(ver>=12){ WORD w=0; if(fread(&w,2,1,f)!=1) return false; rn=w; }
      else       { BYTE b=0; if(fread(&b,1,1,f)!=1) return false; rn=b; }
      for(int k=0;k<rn;k++){
         Snapshot s;
         bool ok = (ver>=9) ? readSnapshot(f,s) : readSnapshotNoTag(f,s);
         if(!ok) return false;
         g_game.redoStack.push_back(s);
      }
   } else {
      g_game.undoStack.clear();
      g_game.redoStack.clear();
   }
   // ver 1/2: freeColMode not stored — leave current setting unchanged
   return true;
}

// Show Save As dialog and save game to chosen file
static void saveGameAs(HWND hwnd){
   std::wstring dir=getLastSaveDir();
   wchar_t filePath[MAX_PATH]=L"pasjans.dat";
   OPENFILENAMEW ofn={};
   ofn.lStructSize    =sizeof(ofn);
   ofn.hwndOwner      =hwnd;
   ofn.lpstrFilter    =L"Zapis gry (*.dat)\0*.dat\0Wszystkie pliki (*.*)\0*.*\0";
   ofn.lpstrFile      =filePath;
   ofn.nMaxFile       =MAX_PATH;
   ofn.lpstrInitialDir=dir.c_str();
   ofn.lpstrTitle     =L"Zapisz grę";
   ofn.lpstrDefExt    =L"dat";
   ofn.Flags          =OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
   if(!GetSaveFileNameW(&ofn)) return; // user cancelled
   FILE* f=_wfopen(filePath,L"wb");
   if(!f){
      MessageBoxW(hwnd,L"Nie można zapisać pliku.",L"Błąd zapisu",MB_OK|MB_ICONERROR);
      return;
   }
   writeGameToFile(f);
   fclose(f);
}

// Snapshot of the outcome-relevant globals for whatever deal is currently on
// screen, taken just before it's about to be replaced by something else (a
// loaded save). newGame() credits the deal it's replacing using these same
// globals live, at the moment it's called — but loadGameFrom() overwrites
// them via readGameFromFile() before it knows whether it even has a valid
// file, so it captures this snapshot first and credits it afterward instead.
struct PendingOutcomeSnapshot {
   bool statsExcluded, winCounted, noMovesReached;
   int  moveCount, outcomeMode, outcomeMoveCount, outcomeSeconds, gameSeconds;
   bool freeColMode;
   long long gameNumber;
};

static PendingOutcomeSnapshot capturePendingOutcome(){
   return { g_statsExcluded, g_winCounted, g_noMovesReached,
            g_moveCount, g_outcomeMode, g_outcomeMoveCount, g_outcomeSeconds, g_gameSeconds,
            g_freeColMode, g_currentGameNumber };
}

// Mirrors the "credit the outcome of the deal being replaced" logic in
// newGame() (see there for the reasoning) — pulled out so loadGameFrom() can
// apply it to the deal that was actually on screen before the load, using a
// snapshot taken before the load overwrote the live globals.
static void creditOutcome(const PendingOutcomeSnapshot& s){
   if(s.statsExcluded) return;
   if(s.winCounted){
      int m=s.outcomeMode;
      g_statsGames[m]++;
      g_statsWins[m]++;
      g_statsMoveSum[m][1]+=s.outcomeMoveCount;
      g_statsTimeSum[m][1]+=s.outcomeSeconds;
      if(g_statsRecordMoves[m]<0 || s.outcomeMoveCount<g_statsRecordMoves[m])
         g_statsRecordMoves[m]=s.outcomeMoveCount;
      if(g_statsRecordTime[m]<0 || s.outcomeSeconds<g_statsRecordTime[m])
         g_statsRecordTime[m]=s.outcomeSeconds;
      saveStats();
      appendWonNumber(s.gameNumber,m);
   } else if(s.noMovesReached){
      int m=s.outcomeMode;
      g_statsGames[m]++;
      g_statsMoveSum[m][0]+=s.outcomeMoveCount;
      g_statsTimeSum[m][0]+=s.outcomeSeconds;
      saveStats();
   } else if(s.moveCount>0){
      int m=(int)s.freeColMode;
      g_statsGames[m]++;
      g_statsMoveSum[m][0]+=s.moveCount;
      g_statsTimeSum[m][0]+=s.gameSeconds;
      saveStats();
   }
   // else: abandoned with zero moves made — not counted
}

// Show Open dialog and load game from chosen file
static void loadGameFrom(HWND hwnd){
   std::wstring dir=getLastSaveDir();
   wchar_t filePath[MAX_PATH]={};
   OPENFILENAMEW ofn={};
   ofn.lStructSize    =sizeof(ofn);
   ofn.hwndOwner      =hwnd;
   ofn.lpstrFilter    =L"Zapis gry (*.dat)\0*.dat\0Wszystkie pliki (*.*)\0*.*\0";
   ofn.lpstrFile      =filePath;
   ofn.nMaxFile       =MAX_PATH;
   ofn.lpstrInitialDir=dir.c_str();
   ofn.lpstrTitle     =L"Wczytaj grę";
   ofn.Flags          =OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
   if(!GetOpenFileNameW(&ofn)) return; // user cancelled

   // Capture the outcome of whatever deal is currently on screen — it's
   // about to be replaced by the loaded save, so it needs crediting to
   // stats now, exactly as if a new game had been started instead (the
   // read below overwrites the live globals this snapshot is taken from).
   PendingOutcomeSnapshot prevOutcome=capturePendingOutcome();

   FILE* f=_wfopen(filePath,L"rb");
   if(!f){
      MessageBoxW(hwnd,L"Nie można otworzyć pliku.",L"Błąd odczytu",MB_OK|MB_ICONERROR);
      return;
   }
   bool ok=readGameFromFile(f);
   fclose(f);
   if(!ok){
      MessageBoxW(hwnd,L"Plik nie jest prawidłowym zapisem gry.",L"Błąd odczytu",MB_OK|MB_ICONERROR);
      return;
   }
   // A finished save (won, or stuck with no moves left) used to be discarded
   // outright here — useful as a guard against accidentally reloading into a
   // dead end, but it also meant a WON save could never be reopened to trace
   // back through how it was won (e.g. a solver replay, or a game someone
   // else played and sent over) — pressing Cofnij repeatedly is exactly how
   // to step back through it move by move, since the full undo history rides
   // along in the save (see writeGameToFile). So: still load it fully, same
   // as any other save — only the credit-then-bail early return is gone.
   bool finished = g_game.checkWin() || !g_game.hasAnyMove();
   if(finished){
      MessageBoxW(hwnd,
         g_game.checkWin()
            ? L"Wczytany zapis jest już wygraną grą. Wczytuję go mimo to — Cofnij pozwoli prześledzić rozegrane ruchy."
            : L"Wczytany zapis jest już zakończoną grą (brak ruchów). Wczytuję go mimo to — Cofnij pozwoli prześledzić rozegrane ruchy.",
         L"Informacja",MB_OK|MB_ICONINFORMATION);
   }
   creditOutcome(prevOutcome);
   // A game loaded from a file is practice/review, not a fresh attempt —
   // never counted towards statistics. This rides along in the save file
   // (see writeGameToFile), so it survives closing and reopening the app.
   g_statsExcluded=true;
   // Apply loaded state
   g_cardAnims.clear(); g_animating=false; g_previewAnimating=false;
   g_hideCol=-1; g_hideFromIdx=-1; g_hideDstCol=-1; g_hideDstFromIdx=-1; g_hideDstFound=-1; for(int _ci=0;_ci<NUM_COLS;_ci++) g_hideDealCol[_ci]=-1;
   g_dealing=false; g_status.clear();
   // Reflect a finished save's true state (won / stuck) so the table shows it
   // correctly — but WITHOUT onStateChanged()'s fanfare (fireworks, "nowy
   // rekord" popup, the "brak ruchów" dialog timer): those are for a win/loss
   // that just happened live, not for reopening an old save to look at it.
   // Setting g_won/g_noMoves alone doesn't achieve that — onStateChanged()
   // re-derives the same state from g_game itself on the very next call (e.g.
   // the first click anywhere on the loaded board) and, seeing g_winCounted/
   // g_noMovesReached/g_noMovesDialogShown all still false, treats it exactly
   // like a live win/loss that just happened: re-logs it to LostNumbers.csv
   // and pops the modal "Brak ruchów" dialog, whose "Nowa gra" immediately
   // discards the loaded save. Marking these as "already handled" up front
   // is what actually suppresses that.
   g_won = g_game.checkWin();
   g_noMoves = !g_won && !g_game.hasAnyMove();
   g_winCounted = g_won;
   g_noMovesReached = g_noMovesDialogShown = g_noMoves;
   g_gameReady=true; g_needsFirstLayout=true;
   // Restart timer continuing from saved elapsed seconds
   g_gameStartTick=timeGetTime()-(DWORD)(g_gameSeconds*1000);
   KillTimer(g_hwnd,8); // TIMER_SECOND=8, defined later
   SetTimer(g_hwnd,8,250,nullptr);
   PostMessageW(hwnd,WM_SIZE,SIZE_RESTORED,0);
}

// Auto-save to default location (next to exe)
static void saveGameState(){
   FILE* f=_wfopen(getSavePath().c_str(),L"wb");
   if(!f) return;
   writeGameToFile(f);
   fclose(f);
}

static bool loadGameState(){
   FILE* f=_wfopen(getSavePath().c_str(),L"rb");
   if(!f) return false;
   bool ok=readGameFromFile(f);
   fclose(f);
   return ok;
}

static void deleteSaveFile(){
   std::wstring path=getSavePath();
   DeleteFileW(path.c_str());
}



// Per-column overlap animation arrays
static float g_colDispOv[NUM_COLS];   // currently rendered overlap per column
static float g_colTgtOv[NUM_COLS];    // target overlap per column
static float g_colSrcOv[NUM_COLS];    // overlap at animation start (for easing)
static DWORD g_colAnimStart[NUM_COLS];// GetTickCount() when animation started
static const DWORD ANIM_DURATION_MS = 220; // total animation duration

static void initColOverlaps(float v){
   for(int i=0;i<NUM_COLS;i++){
      g_colDispOv[i]=g_colTgtOv[i]=g_colSrcOv[i]=v;
      g_colAnimStart[i]=0;
   }
}

static void snapColOverlaps(){
   // Snap display and src to current TARGET (not the other way around)
   for(int i=0;i<NUM_COLS;i++){
      g_colDispOv[i]=g_colSrcOv[i]=g_colTgtOv[i];
      g_colAnimStart[i]=0;
   }
}

// Snap all column overlaps to their targets immediately (no animation)


// Variable-overlap info for a column
struct ColOverlapInfo {
   float ov;        // uniform overlap that fits all cards
   float seqOv;     // overlap for bottom sequence cards (more space)
   float topOv;     // overlap for cards above sequence (less space)
   int   seqStart;  // index where bottom sequence starts
};

static ColOverlapInfo calcColOverlap(int col, int avH){
   ColOverlapInfo r; r.seqStart=0;
   if(!g_hwnd){r.ov=r.seqOv=r.topOv=26.f;return r;}
   const auto& cards=g_game.cols[col];
   int n=(int)cards.size();
   int cardH=g_layout.cardH;

   float natOv=(float)Layout::naturalOverlap(cardH);

   if(n<=1){r.ov=r.seqOv=r.topOv=natOv;return r;}

   // avHeff: we want (n-1)*ov + cardH + 5 <= panelH - tableY
   // avH = panelH - tableY - MARGIN_BOT, so avH + MARGIN_BOT = panelH - tableY
   // => (n-1)*ov + cardH <= avH + MARGIN_BOT - 5
   int avHeff = avH + Layout::MARGIN_BOT - 5;
   if(avHeff < cardH + 12) avHeff = cardH + 12;

   float uniformOv=natOv;
   if((float)(n-1)*natOv+(float)cardH>(float)avHeff){
      uniformOv=(float)(avHeff-cardH)/(float)(n-1);
      uniformOv=std::max(12.f,uniformOv);
   }
   r.ov=uniformOv;

   int sl=g_game.seqLenFromBottom(col);
   r.seqStart=n-sl;

   // NOTE: this used to give the bottom "movable sequence" extra breathing
   // room at the expense of the cards above it (smaller overlap there). That
   // consistently caused problems: overflow past the bottom of the window in
   // some cases, and — when the "top" portion had several cards — squeezing
   // them down to an unreadable, near-overlapping grey clump in others
   // (worse still when combined with the "dim non-movable cards" highlight
   // mode). A single uniform overlap, identical to what the card-size
   // selection pass already guarantees fits, is simpler and can't produce
   // either failure mode. r.seqStart is still reported for the outline/dim
   // move-highlight modes, which only need to know WHERE the sequence
   // starts, not a different spacing for it.
   r.seqOv=r.topOv=uniformOv;
   return r;
}

// Compute the correct landing Y for card index dstIdx in column tc,
// Get Y position of card ci in column col using uniform animated overlap
static int colCardY(int col,int ci){
   return g_layout.tableY+(int)(ci*g_colDispOv[col]);
}

// forward declaration — defined after colCardYExact
static float colCardYExact(int col, int ci, const ColOverlapInfo& oi);

static float landingY(int tc, int dstIdx){
   int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
   ColOverlapInfo oi=calcColOverlap(tc,avH);
   return colCardYExact(tc,dstIdx,oi);
}
static float landingOv(int tc){
   int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
   ColOverlapInfo oi=calcColOverlap(tc,avH);
   return oi.seqOv;
}

// Compute the exact Y position that renderer would use for card index ci in column col.
// Matches the drawing loop's split-overlap logic (uses oi.seqOv/topOv directly —
// these are already guaranteed to fit within avHeff by calcColOverlap's own
// safety net, so no further rescaling/clamping is needed or safe to redo here).
// Pass the ColOverlapInfo already computed for this column to avoid recomputing.
static float colCardYExact(int col, int ci, const ColOverlapInfo& oi){
   int n=(int)g_game.cols[col].size();
   float seqOv=oi.seqOv, topOv=oi.topOv;

   float y=(float)g_layout.tableY;
   for(int k=0;k<ci&&k<n-1;k++){
      bool nextInSeq=(k+1)>=oi.seqStart;
      float ov=nextInSeq?seqOv:topOv;
      ov=std::max(12.f,ov);
      y+=ov;
   }
   return y;
}


// Per-column overlap animation — driven by PeekMessage loop, not WM_TIMER
static const UINT TIMER_SMOOTH = 4;  // kept for KillTimer calls (no-op when not set)
static const UINT TIMER_HINT_OFF = 5;
static const UINT TIMER_NOMOVES  = 6;
static const UINT TIMER_PULSE    = 7;   // reserve card white-pulse hint animation
static const UINT TIMER_SECOND   = 8;   // 1-second game timer tick
static bool  g_reservePulsing   = false;
static int   g_reservePulseStep = 0;    // 0..23: 3 pulses x 8 steps (50ms each)
static float g_reservePulseAlpha= 0.f;  // 0..1 white overlay intensity
static bool  g_emptyColPulsing  = false; // pulse all empty columns as "move here" hint
static float g_emptyColPulseAlpha=0.f;
static bool g_smoothActive = false;  // true while overlap animation is running

// Board cache
// During deal: starts with felt only, cards are painted onto it as they complete pop-in
// After deal: contains all cards + UI elements
// Board rendering: DIB-direct approach.
// g_boardDIB is a DIB section; GDI+ wraps it via Bitmap::FromHBITMAP and draws
// directly into its pixel buffer — no GDI+→GDI copy needed.
static HBITMAP  g_boardDIB   = nullptr;  // DIB section (pixel buffer)
static HDC      g_boardDC    = nullptr;  // DC with g_boardDIB selected
static BYTE*    g_boardBits  = nullptr;  // raw pixel pointer (BGRA, bottom-up)
static int      g_boardW=0, g_boardH=0;
static Bitmap*  g_boardBmp   = nullptr;  // GDI+ wrapper around g_boardDIB pixels
static bool     g_boardDirty = true;     // background (felt+stosy+rezerwa) needs rebuild

// Per-column DIB buffers for dirty-column optimization.
// Each column bitmap covers [colPos(col).x .. colPos(col).x+cardW] x [0..panelH].
// Only rebuilt when that column's overlap or cards change.
static HBITMAP  g_colDIB [NUM_COLS] = {};
static HDC      g_colDC  [NUM_COLS] = {};
static Bitmap*  g_colBmp [NUM_COLS] = {};  // GDI+ wrappers
static bool     g_colDirty[NUM_COLS];      // needs repaint
static int      g_colDIBW=0, g_colDIBH=0; // size of each col DIB (same for all)

// Drag
struct Drag {
   bool  active=false, moved=false;
   std::vector<Card> cards;
   int   fromCol=-1, fromFound=-1, startIdx=0;
   int   offX=0, offY=0, mx=0, my=0, downX=0, downY=0;
} g_drag;

// ============================================================================
// Helpers
// ============================================================================
static bool hitCard(int mx,int my,int x,int y){
   return mx>=x&&mx<=x+g_layout.cardW&&my>=y&&my<=y+g_layout.cardH;}

static bool ensureD2DRT(HWND hwnd){
   if(g_d2dRT)return true; if(!g_d2dFactory)return false;
   RECT rc;GetClientRect(hwnd,&rc);if(rc.right<=0||rc.bottom<=0)return false;
   HRESULT hr=g_d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU((UINT32)rc.right,(UINT32)rc.bottom)),&g_d2dRT);
   if(FAILED(hr)){g_d2dRT=nullptr;return false;}
   g_d2dRT->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
   g_renderer.setRT(g_d2dRT,g_dwFactory);CardImagesD2D::instance().invalidate();return true;}
static void discardD2DRT(){
   g_renderer.setRT(nullptr,nullptr);CardImagesD2D::instance().invalidate();
   if(g_d2dRT){g_d2dRT->Release();g_d2dRT=nullptr;}}

// Deck (0 = red back, 1 = blue back) of the next card to be dealt from the
// reserve — what the face-down pile on screen should show. Derived from the
// deal's number, cached per number (see reserveDeckTagsForSeed in game.h). A
// save with no known number (very old files) just alternates, which is only
// cosmetic.
static int reserveTopDeck(){
   static long long cachedFor=-2; static std::vector<uint8_t> tags;
   if(g_currentGameNumber!=cachedFor){
      cachedFor=g_currentGameNumber;
      tags.clear();
      if(g_currentGameNumber>=0) tags=reserveDeckTagsForSeed((unsigned int)g_currentGameNumber);
   }
   size_t n=g_game.reserve.size();
   if(tags.empty()||n>tags.size()||n==0) return (int)(n&1);
   return tags[tags.size()-n];
}

static void drawScene(){
   if(!g_d2dRT)return;
   float fw=g_d2dRT->GetSize().width,fh=g_d2dRT->GetSize().height;int w=(int)fw,h=(int)fh;
   // Child window Y=0 corresponds to main window Y=TOOLBAR_H
   const float OY=(float)Layout::TOOLBAR_H;
   auto& bg=BG_COLORS[g_bgIndex];ID2D1SolidColorBrush* brFelt=nullptr;
   g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(bg.r/255.f,bg.g/255.f,bg.b/255.f),&brFelt);
   if(brFelt){g_d2dRT->FillRectangle(D2D1::RectF(0,0,fw,fh),brFelt);brFelt->Release();}
   // Slightly lighter shade of the felt, used for the "sequence outline" move-highlight mode
   BYTE seqOutR=(BYTE)std::min(255,(int)(bg.r+(255-bg.r)*0.55f));
   BYTE seqOutG=(BYTE)std::min(255,(int)(bg.g+(255-bg.g)*0.55f));
   BYTE seqOutB=(BYTE)std::min(255,(int)(bg.b+(255-bg.b)*0.55f));
   bool hintOn=g_hintActive||g_hintBlinking,showSrc=g_hintActive||g_hintShowSrc,showDst=g_hintActive||g_hintShowDst;
   for(int i=0;i<NUM_FOUND;i++){
      POINT fp=g_layout.foundPos(i);float fx=(float)fp.x,fy=(float)fp.y-OY;
      bool hSrc=hintOn&&g_hint.valid&&showSrc&&g_hint.fromType==LOC_FOUNDATION&&g_hint.fromIdx==i;
      bool hDst=hintOn&&g_hint.valid&&showDst&&g_hint.toType==LOC_FOUNDATION&&g_hint.toIdx==i;
      bool hideTop=(g_hideDstFound==i&&!g_game.found[i].empty());
      if(g_game.found[i].empty()||hideTop){
         if(hideTop&&g_game.found[i].size()>1)g_renderer.drawCard(fx,fy,g_game.found[i][g_game.found[i].size()-2],false,false);
         else g_renderer.drawEmptyFound(fx,fy,i);
      }else g_renderer.drawCard(fx,fy,g_game.found[i].back(),hSrc||hDst,false);
      if(hSrc||hDst)g_renderer.drawHighlight(fx,fy);}
   POINT rp=g_layout.reservePos();float rx=(float)rp.x,ry=(float)rp.y-OY;
   bool hRes=(g_hintActive||(g_hintBlinking&&g_hintShowSrc))&&g_hint.valid&&g_hint.fromIdx==-1&&!g_game.reserve.empty();
   if(!g_game.reserve.empty()){g_renderer.drawBack(rx,ry,reserveTopDeck());g_renderer.drawReserveCount(rx,ry,(float)g_layout.cardW,(float)g_layout.cardH,std::to_wstring(g_game.reserve.size()));}
   if(hRes)g_renderer.drawHighlight(rx,ry);
   // White-pulse hint overlay on reserve card
   if(g_reservePulsing && g_reservePulseAlpha>0.f && !g_game.reserve.empty())
      g_renderer.drawWhiteOverlay(rx,ry,(float)g_layout.cardW,(float)g_layout.cardH,g_reservePulseAlpha*0.75f);
   int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
   for(int col=0;col<NUM_COLS;col++){
      POINT cp=g_layout.colPos(col);float cpx=(float)cp.x;
      bool hDst2=hintOn&&g_hint.valid&&showDst&&g_hint.fromIdx>=0&&g_hint.toType==LOC_COLUMN&&g_hint.toIdx==col;
      bool previewEmptiesCol=(g_hideCol==col&&g_hideFromIdx==0&&!g_game.cols[col].empty());
      if(g_game.cols[col].empty()||previewEmptiesCol){
         // Use pulsing variant when hint targets empty cols, else normal
         float pulseA=(g_emptyColPulsing&&g_emptyColPulseAlpha>0.f)?g_emptyColPulseAlpha:0.f;
         if(pulseA>0.f) g_renderer.drawEmptyPulsing(cpx,(float)cp.y-OY,pulseA);
         else           g_renderer.drawEmpty(cpx,(float)cp.y-OY,nullptr);
         if(hDst2) g_renderer.drawHighlight(cpx,(float)cp.y-OY);
         continue;
      }
      ColOverlapInfo oi=calcColOverlap(col,avH);
      // Use the already-safety-checked seqOv/topOv directly — calcColOverlap
      // guarantees these fit within avHeff. (Previously these were
      // re-derived by scaling the animated baseOv by the oi.seqOv/oi.ov
      // ratio, with its own separate min-12px-clamped "shrink if overflow"
      // step — but that clamp could itself fail to shrink enough in tight
      // columns, since both values were already floored at 12. That's what
      // let cards get clipped off the bottom of the window.)
      float seqOv=oi.seqOv, topOv=oi.topOv;
      int n=(int)g_game.cols[col].size();
      float y=(float)g_layout.tableY-OY;
      auto& cards=g_game.cols[col];
      float seqOutlineY=-1.f; // y of the first card in the movable sequence (mode 1)
      for(int ci=0;ci<n;ci++){
         if(g_drag.active&&g_drag.fromCol==col&&ci>=g_drag.startIdx)continue;
         if(g_hideCol==col&&g_hideFromIdx>=0&&ci>=g_hideFromIdx)continue;
         if(g_hideDstCol==col&&g_hideDstFromIdx>=0&&ci>=g_hideDstFromIdx)continue;
         if(g_hideDealCol[col]>=0&&ci>=g_hideDealCol[col])continue;
         bool hS2=hintOn&&g_hint.valid&&showSrc&&g_hint.fromType==LOC_COLUMN&&g_hint.fromIdx==col&&g_hint.fromIdx>=0&&ci>=g_hint.fromCard;
         g_renderer.drawCard(cpx,y,cards[ci],hS2,false);
         if(g_moveHighlightMode==2 && ci<oi.seqStart) g_renderer.drawDimOverlay(cpx,y);
         if(g_moveHighlightMode==1 && ci==oi.seqStart) seqOutlineY=y;
         if(ci+1<n){y+=std::max(12.f,(ci+1)>=oi.seqStart?seqOv:topOv);}}
      // Outline the movable sequence (2+ cards) — bounding box from its first
      // card down to the bottom (last) card of the column.
      if(g_moveHighlightMode==1 && seqOutlineY>=0.f && n-oi.seqStart>=2)
         g_renderer.drawSeqOutline(cpx,seqOutlineY,(float)g_layout.cardW,
            y+(float)g_layout.cardH-seqOutlineY, seqOutR,seqOutG,seqOutB);
      if(hDst2){int vc=(g_drag.active&&g_drag.fromCol==col)?g_drag.startIdx:n;g_renderer.drawHighlight(cpx,(float)(vc==0?cp.y:colCardY(col,vc-1))-OY);}
      // Fading empty slot: shown during animation when card is flying to this col
      // g_hideDstCol hides the card – draw fading slot beneath the flying card
      if(g_hideDstCol==col && !cards.empty()){
         // Find the flying animation targeting this column and get its progress
         float fadeAlpha=1.f;
         for(auto& _a:g_cardAnims){
            if(!_a.isPreview && !_a.done()){
               float t=_a.ease(); // 0=start,1=landed
               fadeAlpha=1.f-t; // slot fades out as card arrives
               break;
            }
         }
         if(fadeAlpha>0.01f)
            g_renderer.drawEmptyFading(cpx,(float)cp.y-OY,fadeAlpha);
      }
   }
   if(g_dealing){const PopIn* cur=g_deal.current();if(cur){int cx=g_layout.colPos(cur->col).x,cy=colCardY(cur->col,cur->cardIdx);float sc=cur->scale(),cw=(float)g_layout.cardW*sc,ch=(float)g_layout.cardH*sc;ID2D1Bitmap* bmp=GetCardD2D(cur->card.imgKey(),g_d2dRT);if(bmp)g_d2dRT->DrawBitmap(bmp,D2D1::RectF((float)cx+(g_layout.cardW-cw)/2.f,(float)cy+(g_layout.cardH-ch)/2.f-OY,(float)cx+(g_layout.cardW+cw)/2.f,(float)cy+(g_layout.cardH+ch)/2.f-OY));}}
   if(g_drag.active&&!g_drag.cards.empty()){int bx=g_drag.mx-g_drag.offX,by=g_drag.my-g_drag.offY;float dov=(g_drag.fromCol>=0)?g_colDispOv[g_drag.fromCol]:g_colDispOv[0];for(int i=0;i<(int)g_drag.cards.size();i++)g_renderer.drawCard((float)bx,(float)by+i*dov-OY,g_drag.cards[i],true,false);}
   for(auto& a:g_cardAnims){if(a.done())continue;float cx=a.cx(),cy=a.cy()-OY,ov=a.renderOv();for(int i=0;i<(int)a.cards.size();i++)g_renderer.drawCard(cx,cy+i*ov,a.cards[i],false,false);}
   // ── Fireworks ─────────────────────────────────────────────────────────────
   for(auto& fwk:g_fw.fireworks()){
      if(!fwk.exploded){
         // Rocket trail line
         g_renderer.drawLine(fwk.px,fwk.py-OY,fwk.x,fwk.y-OY,
            1.8f,fwk.cr,fwk.cg,fwk.cb,120);
         // Bright tip
         g_renderer.drawEllipse(fwk.x,fwk.y-OY,2.5f,255,255,220,220);
      } else {
         // Bloom flash: radial gradient — white centre fading to transparent edge
         if(fwk.flashLife>0){
            float bf=(float)fwk.flashLife/7.f;  // 1→0
            float radius=70.f*bf+30.f;           // shrinks as it fades
            // Build radial gradient: centre=white opaque, edge=white transparent
            ID2D1GradientStopCollection* stops=nullptr;
            D2D1_GRADIENT_STOP gs[3];
            gs[0].position=0.0f; gs[0].color=D2D1::ColorF(1,1,1, bf*0.9f);
            gs[1].position=0.4f; gs[1].color=D2D1::ColorF(1,0.95f,0.8f, bf*0.55f);
            gs[2].position=1.0f; gs[2].color=D2D1::ColorF(1,0.8f,0.4f, 0.f);
            if(SUCCEEDED(g_d2dRT->CreateGradientStopCollection(gs,3,&stops))){
               ID2D1RadialGradientBrush* br=nullptr;
               D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp=
                  D2D1::RadialGradientBrushProperties(
                     D2D1::Point2F(fwk.x,fwk.y-OY),
                     D2D1::Point2F(0,0), radius, radius);
               if(SUCCEEDED(g_d2dRT->CreateRadialGradientBrush(rp,stops,&br))){
                  g_d2dRT->FillEllipse(
                     D2D1::Ellipse(D2D1::Point2F(fwk.x,fwk.y-OY),radius,radius),br);
                  br->Release();
               }
               stops->Release();
            }
         }
         // Particles
         for(auto& p:fwk.parts){
            float t=(float)p.life/(float)p.maxLife; // 0=new,1=dying
            float af=1.f-t*t;                        // quadratic fade
            BYTE al=(BYTE)(af*255.f);
            if(al<5) continue;
            // Strobe: skip every other tick
            if(p.strobe && (p.life%2)==1) continue;
            // Colour lerp from start to ember
            BYTE r=(BYTE)(p.r*(1.f-t)+p.er*t);
            BYTE g=(BYTE)(p.g*(1.f-t)+p.eg*t);
            BYTE b=(BYTE)(p.b*(1.f-t)+p.eb*t);
            // Trail line (skip for sparks and sub-particles — they're tiny)
            if(!p.spark && !p.sub){
               float dx=p.x-p.px,dy=p.y-p.py;
               float tlen=sqrtf(dx*dx+dy*dy);
               if(tlen>0.4f){
                  float lw=std::max(0.6f,p.size*0.55f*(1.f-t*0.5f));
                  g_renderer.drawLine(p.px,p.py-OY,p.x,p.y-OY,lw,r,g,b,
                     BYTE(al*0.5f));
               }
            }
            // Head dot
            float sz=p.size*(1.f-t*0.55f);
            if(sz>0.25f) g_renderer.drawEllipse(p.x,p.y-OY,sz,r,g,b,al);
         }
      }
   }
   // Compute move label position
   // Default: horizontally centred between rightmost foundation and reserve,
   // vertically aligned with top of foundations (foundY - OY)
   float mlx, mly;
   {
      // rightmost foundation right edge
      float foundRight=0.f;
      for(int i=0;i<NUM_FOUND;i++){
         POINT fp=g_layout.foundPos(i);
         float fr=(float)(fp.x+g_layout.cardW);
         if(fr>foundRight) foundRight=fr;
      }
      // reserve left edge
      POINT rpp=g_layout.reservePos();
      float resLeft=(float)rpp.x;
      float foundTop=(float)g_layout.foundPos(0).y-OY;
      if(!g_moveLabelCustom||g_moveLabelX<0){
         mlx=foundRight+10.f;
         mly=foundTop;
      } else {
         mlx=g_moveLabelX;
         mly=g_moveLabelY;
      }
   }
   D2D1_RECT_F moveLabelRect={};
   // "Myślę" pulse: triangle wave 0→1→0 over one 500ms cycle, scaled to the
   // normal status-text alpha (0.47) so the peak matches "font color" and
   // the trough fades into the felt, as if the text breathes in and out.
   float thinkAlpha=0.f;
   if(g_thinking){
      float phase=(float)(timeGetTime()%500)/500.f;
      float t=phase<0.5f?phase*2.f:(1.f-phase)*2.f;
      thinkAlpha=t*0.47f;
   }
   g_renderer.drawStatusText(g_status,(float)w,g_won,g_noMoves,g_moveCount,g_gameSeconds,
                              g_thinking,thinkAlpha,
                              mlx,mly,g_currentGameNumber,&moveLabelRect);
   // Store for hit-testing (mouse drag)
   g_moveLabelDrawnRect=moveLabelRect;
}

static void invalidateGame(); // forward declaration
static void markDirty(){}     // no-op: D2D redraws scene every frame
static void markAllColsDirty(){}
static void markColDirty(int){}
static void invalidateGame(){
   if(g_gameHwnd) InvalidateRect(g_gameHwnd,nullptr,FALSE);
}

void commitCardToBoard(const Card&,int,int){invalidateGame();}
void initDealBoard(int,int){}

void doPaint(HWND /*hwnd*/){
   if(!g_gameHwnd)return;
   if(!ensureD2DRT(g_gameHwnd))return;
   g_d2dRT->BeginDraw();
   drawScene();
   HRESULT hr=g_d2dRT->EndDraw();
   if(hr==D2DERR_RECREATE_TARGET){discardD2DRT();InvalidateRect(g_gameHwnd,nullptr,FALSE);}
}


// Invalidate only the game area below toolbar to avoid button flicker

// Invalidate only the game area (below toolbar) to avoid button flicker

void recalcLayout(bool noAnim=false){
   RECT rc; GetClientRect(g_hwnd,&rc);
   int mx=1;
   for(int c=0;c<NUM_COLS;c++) mx=std::max(mx,(int)g_game.cols[c].size());
   g_layout.recalc(rc.right,rc.bottom,std::max(mx,13));
   g_renderer.setLayout(&g_layout);

   // Compute per-column target overlaps using naturalOverlap logic
   int avH = g_layout.panelH - g_layout.tableY - Layout::MARGIN_BOT;
   DWORD now = timeGetTime();
   bool anyNew = false;
   for(int col=0;col<NUM_COLS;col++){
      ColOverlapInfo info=calcColOverlap(col,avH);
      float newTgt=info.ov;
      if(std::abs(newTgt-g_colTgtOv[col])>0.5f){
         g_colSrcOv[col]=g_colDispOv[col];
         g_colAnimStart[col]=now;
         g_colTgtOv[col]=newTgt;
         anyNew=true;
      }
   }
   if(anyNew && !noAnim){
      g_smoothActive = true;
   }
}

// ============================================================================
// Draw one card onto a Graphics (helper used by deal animation)


// ============================================================================
// Board bitmap management
// ============================================================================


// Rebuild board fully (non-deal scenario)
static void drawStatusText(Graphics& g, int w){
   // Move counter: 10px below toolbar, horizontally centred between last foundation and reserve
   {
      POINT fp7=g_layout.foundPos(NUM_FOUND-1);
      POINT rp2=g_layout.reservePos();
      int gapStart=fp7.x+g_layout.cardW;
      int gapEnd  =rp2.x;
      if(gapEnd>gapStart+20){
         float fs=std::max(7.f,8.5f*g_layout.cardW/71.f);
         FontFamily ff(L"Segoe UI"); Font f(&ff,fs,FontStyleBold,UnitPoint);
         SolidBrush br(Color(200,200,200,200));
         std::wstring mtxt=L"Ruchy: "+std::to_wstring(g_moveCount);
         RectF mb; g.MeasureString(mtxt.c_str(),-1,&f,PointF(0,0),&mb);
         float mx=(float)gapStart+(gapEnd-gapStart-mb.Width)/2.f;
         float my=(float)(Layout::TOOLBAR_H+10);
         g.DrawString(mtxt.c_str(),-1,&f,PointF(mx,my),&br);
      }
   }
   // Only show win/no-moves overlay (no hint text, no reserve count)
   if(!g_status.empty()){
      float fs=10.f;FontFamily ff(L"Segoe UI");Font f(&ff,fs,FontStyleBold,UnitPoint);
      Color sc=g_won?Color(255,255,220,50):g_noMoves?Color(255,255,110,110):Color(255,255,255,120);
      SolidBrush br(sc);
      RectF bounds;g.MeasureString(g_status.c_str(),-1,&f,PointF(0,0),&bounds);
      g.DrawString(g_status.c_str(),-1,&f,PointF(w-bounds.Width-12.f,12.f),&br);}
}
// ============================================================================
// Board rendering: GDI+ Bitmap + per-column dirty tracking
// ============================================================================


// Board DIB: GDI-compatible copy of g_boardBmp, used for fast BitBlt.


// Create (or recreate) per-column GDI+ bitmaps and matching DIBs.

// Mark all column DIBs dirty (e.g. after resize or full state change)

// Draw a single column's cards onto an existing Graphics context.


// Rebuild the background layer: felt, foundations, reserve, status text.

// Rebuild board: all drawing into g_boardBmp (single GDI+ bitmap).
// Per-column dirty tracking: if only columns changed, redraw only those

// Paint a single completed card directly onto the board bitmap

// NOTE: drawStatusText needs to be forward-declared or defined before rebuildBoardFull
// Let's define it here and use it above (we'll fix order):


// ============================================================================
// State changed
// ============================================================================
void onStateChanged(){
   g_hintActive=false;
   g_hintBlinking=false;
   g_gameStarted=true;
   g_hintList.clear();
   g_hintIndex=0;
   // NOTE: deliberately does NOT touch g_samogranoActive — this runs after
   // every move, including moves Samograj itself just made.
   KillTimer(g_hwnd,TIMER_BLINK); KillTimer(g_hwnd,TIMER_HINT_OFF); KillTimer(g_hwnd,TIMER_SMOOTH); KillTimer(g_hwnd,TIMER_PULSE); g_reservePulsing=false; g_reservePulseAlpha=0.f; g_emptyColPulsing=false; g_emptyColPulseAlpha=0.f;
   recalcLayout();
   markDirty();
   if(g_game.checkWin()){
      g_status=L""; g_won=true;
      if(!g_winCounted){
         g_winCounted=true;
         if(g_currentGameNumber>=0){ // a deal that is won is no longer "lost" (or "unsolvable")
            removeNumberEntry(getLostNumbersPath(),g_currentGameNumber);
            removeNumberEntry(getUnsolvablePath(),g_currentGameNumber);
         }
         int m=(int)g_freeColMode;
         // Capture the outcome now — actually crediting stats/records happens
         // later, when a new deal starts (see newGame()). This way, undoing
         // out of this win and continuing to play doesn't leave a stray
         // "win" on the books if something else happens afterward, and —
         // symmetrically with the no-moves case below — nothing is counted
         // until the player is truly done with this deal.
         g_outcomeMode=m; g_outcomeMoveCount=g_moveCount; g_outcomeSeconds=g_gameSeconds;
         bool newMoves=false, newTime=false;
         if(!g_statsExcluded){
            // Preview whether this would be a new record, for the
            // congratulations message below — the actual record values
            // aren't updated until newGame() commits this outcome.
            newMoves = (g_statsRecordMoves[m]<0 || g_moveCount<g_statsRecordMoves[m]);
            newTime  = (g_statsRecordTime[m]<0  || g_gameSeconds<g_statsRecordTime[m]);
         }
         // Sound and fireworks (always — even for excluded "try again" replays) —
         // except during the Samograj-bez-końca marathon, where a win just means
         // "deal the next one", so none of this end-of-game fanfare should show.
         if(!g_samogranoMarathon){
            playSound("sukces",g_volume);
            RECT rc;
            if(g_gameHwnd) GetClientRect(g_gameHwnd,&rc);
            else { GetClientRect(g_hwnd,&rc); rc.top=Layout::TOOLBAR_H; }
            g_fw.start(rc.right,rc.bottom-(rc.top>0?rc.top:0));
            SetTimer(g_hwnd,TIMER_FW,28,nullptr);
            // Show record-breaking congratulations after a short delay
            if(newMoves || newTime){
               // Build message
               std::wstring msg=L"🏆 Nowy rekord";
               if(newMoves && newTime)
                  msg+=L"!\n\nPobito oba rekordy:\n"
                       L"  ✦ Najmniej ruchów: "+std::to_wstring(g_moveCount)+L"\n"
                       L"  ✦ Najkrótszy czas: "+fmtTime(g_gameSeconds);
               else if(newMoves)
                  msg+=L"!\n\nPobito rekord liczby ruchów:\n"
                       L"  ✦ Najmniej ruchów: "+std::to_wstring(g_moveCount);
               else
                  msg+=L"!\n\nPobito rekord czasu:\n"
                       L"  ✦ Najkrótszy czas: "+fmtTime(g_gameSeconds);
               msg+=L"\n\nTryb: ";
               msg+=(m==0?L"Tylko król":L"Dowolna karta");
               g_congratsMsg=msg; // show after animation completes
            }
         }
      }
   } else if(!g_won && !g_winCounted && !g_game.hasAnyMove() && !g_dealing){
      // No moves at all (table + reserve exhausted) — only check outside deal anim
      // Also skip if game was already won (g_winCounted) to avoid false loss after win
      g_status=L"Brak możliwych ruchów!"; g_noMoves=true;
      if(!g_noMovesReached){
         // Game genuinely played through to the end (not just abandoned
         // early via "New game"). Capture the outcome — it's only actually
         // credited to stats once a new deal starts (see newGame()), so
         // that undoing out of this state and continuing to play (possibly
         // all the way to a win) doesn't leave a stray loss on the books.
         g_noMovesReached=true;
         recordLostDeal(g_currentGameNumber,(int)g_freeColMode);
         g_outcomeMode=(int)g_freeColMode;
         g_outcomeMoveCount=g_moveCount;
         g_outcomeSeconds=g_gameSeconds;
      }
      if(!g_samogranoMarathon && !g_noMovesDialogShown) SetTimer(g_hwnd,TIMER_NOMOVES,600,nullptr);
   } else {
      g_status.clear(); g_won=g_noMoves=false;
   }
   invalidateBestMoveCache();
   invalidateGame();
}

// ============================================================================
// Deal animation
// ============================================================================
void startDealAnim(){
   g_deal.clear();
   float stagger=0.f;
   // Same g_animDurationMul used for card flights — larger gaps between
   // cards popping in means the whole initial deal-out takes proportionally
   // longer (or shorter), matching the "Prędkość animacji" setting.
   for(int row=0;row<NUM_COLS;row++){
      for(int col=0;col<NUM_COLS-row;col++){
         g_deal.queue(g_game.cols[col][row],col,row,stagger);
         stagger+=0.12f*effectiveAnimMul();
      }
      stagger+=0.3f*effectiveAnimMul();
   }
   g_dealing=true;
   RECT rc;GetClientRect(g_hwnd,&rc);
   recalcLayout(true);   // noAnim — size may be 0×0 at startup; WM_SIZE will fix it
   initDealBoard(rc.right,rc.bottom);
   SetTimer(g_hwnd,TIMER_DEAL,16,nullptr);
}

// ============================================================================
// New game
// ============================================================================
void newGame(bool sameDeal, long long gameNumber, bool skipCredit, bool silent){
   // Credit the OUTCOME of the deal being replaced — but only now, as it
   // actually starts being replaced, not back when win/no-moves was first
   // reached. That's what lets the player undo out of a "no moves" (or even
   // a win, in principle) and keep playing without it being locked in.
   // Credit the OUTCOME of the deal being replaced — but only now, as it
   // actually starts being replaced, not back when win/no-moves was first
   // reached. That's what lets the player undo out of a "no moves" (or even
   // a win, in principle) and keep playing without it being locked in.
   // NOTE: deliberately does NOT check g_gameStarted here — that flag is
   // never persisted to the save file, so it's always false right after an
   // app restart, which used to make a pending win/loss from a closed
   // session silently go uncounted. g_winCounted/g_noMovesReached ARE
   // persisted, and are exactly what's needed here anyway.
   // skipCredit: "Zacznij od poczatku te gre" - the attempt being thrown away is
   // never counted, whatever state it is in.
   if(!g_statsExcluded && !skipCredit){
      if(g_winCounted){
         int m=g_outcomeMode;
         g_statsGames[m]++;
         g_statsWins[m]++;
         g_statsMoveSum[m][1]+=g_outcomeMoveCount;
         g_statsTimeSum[m][1]+=g_outcomeSeconds;
         if(g_statsRecordMoves[m]<0 || g_outcomeMoveCount<g_statsRecordMoves[m])
            g_statsRecordMoves[m]=g_outcomeMoveCount;
         if(g_statsRecordTime[m]<0 || g_outcomeSeconds<g_statsRecordTime[m])
            g_statsRecordTime[m]=g_outcomeSeconds;
         saveStats();
         appendWonNumber(g_currentGameNumber,m);
      } else if(g_noMovesReached){
         int m=g_outcomeMode;
         g_statsGames[m]++;
         g_statsMoveSum[m][0]+=g_outcomeMoveCount;
         g_statsTimeSum[m][0]+=g_outcomeSeconds;
         saveStats();
      } else if(g_moveCount>0){
         // Abandoned mid-play (moves were still available, so the genuine
         // no-moves detection never fired) — but the player made at least
         // one move, so this still counts as a loss. No "outcome" was ever
         // captured for this deal (it never actually concluded), so use the
         // live counters — they still hold this deal's values at this
         // point, since they're only reset further below.
         int m=(int)g_freeColMode;
         g_statsGames[m]++;
         g_statsMoveSum[m][0]+=g_moveCount;
         g_statsTimeSum[m][0]+=g_gameSeconds;
         saveStats();
      }
      // else: abandoned with zero moves made — not counted
   }

   deleteSaveFile();
   g_gameStartTick=timeGetTime(); g_gameSeconds=0; g_congratsMsg.clear();
   g_moveCount=0;
   g_gameStarted=false;
   g_winCounted=false;
   g_autoDealCounted=false;
   g_autoDealStuck=false;
   g_noMovesReached=false;
   g_noMovesDialogShown=false;
   g_statsExcluded=sameDeal; // "try again" replays are not counted as a separate game
   clearAutoHistory();
   SoundSystem::instance().fadeOutAll(); // fade out win sound if playing
   g_fw.stop(); KillTimer(g_hwnd,TIMER_FW);
   KillTimer(g_hwnd,TIMER_DEAL);
   g_drag.active=false; g_drag.cards.clear();
   g_deal.clear();
   g_hint={}; g_hintActive=false;
   g_hintBlinking=false; g_hintList.clear(); g_hintIndex=0;
   g_thinking=false; g_pendingAction=PA_NONE;
   // Marathon mode deals its own next hand from samogranoStep() and wants to
   // keep going - every OTHER caller of newGame() has already cancelled
   // Samograj itself before getting here (see the WM_COMMAND/WM_KEYDOWN
   // handlers), so this only ever skips anything while the marathon is
   // driving.
   if(!g_samogranoMarathon) cancelSamograj();
   KillTimer(g_hwnd,TIMER_BLINK); KillTimer(g_hwnd,TIMER_HINT_OFF); KillTimer(g_hwnd,TIMER_SMOOTH); KillTimer(g_hwnd,TIMER_PULSE); KillTimer(g_hwnd,TIMER_SECOND); g_reservePulsing=false; g_reservePulseAlpha=0.f; g_emptyColPulsing=false; g_emptyColPulseAlpha=0.f;
   KillTimer(g_hwnd,TIMER_NOMOVES);
   g_cardAnims.clear(); g_animating=false; g_previewAnimating=false;
   g_hideCol=-1; g_hideFromIdx=-1; g_hideDstCol=-1; g_hideDstFromIdx=-1; g_hideDstFound=-1; for(int _ci=0;_ci<NUM_COLS;_ci++) g_hideDealCol[_ci]=-1;
   g_dealing=false;
   g_won=g_noMoves=false; g_status.clear();
   if(sameDeal){
      g_game.replayDeal(); // g_currentGameNumber stays whatever it was
   } else {
      unsigned int seed;
      if(gameNumber>=0){
         seed=(unsigned int)gameNumber;
         g_currentGameNumber=gameNumber;
      } else {
         std::random_device rd;
         seed=rd();
         g_currentGameNumber=(long long)seed;
      }
      g_game.newGame(seed);
   }
   recalcLayout(true);   // noAnim — size may be 0×0 at startup
   snapColOverlaps();    // snap immediately — no animation at game start
   g_smoothActive=false;
   g_gameReady=true;
   if(!silent) playSound("nowa",g_volume);
   startDealAnim();
   SetTimer(g_hwnd,TIMER_SECOND,250,nullptr); // 250ms for smooth display
   invalidateBestMoveCache();
   invalidateGame();
}

void newGameRetry(){ newGame(true); }

// Menu: "Zacznij od poczatku te gre" - back to the initial deal, counters and
// timer reset, current attempt NOT credited to the statistics. The restarted
// attempt is a replay, so it is excluded from statistics (same as "try again").
void restartCurrentGame(){
   if(g_game.initialDeal.cols[0].empty()){
      // Saves older than v9 don't remember the initial deal.
      g_status=L"Brak zapamiętanego rozdania - nie można zacząć od początku";
      markDirty(); invalidateGame();
      return;
   }
   newGame(true,-1,true);
}

// ============================================================================
// Actions
// ============================================================================
// Is column tc a king-topped valid sequence on a free slot?
static bool isKingSequenceCol(int tc){
   if(g_game.cols[tc].empty()) return false;
   if(g_game.cols[tc][0].rank!=King) return false;
   return g_game.validSeqFrom(tc,0);
}

// ============================================================================
// Card animation helpers
// ============================================================================

// Pixel position of card ci in column col (using current display overlap)
static POINT cardPixelPos(int col, int ci){
   POINT cp=g_layout.colPos(col);
   return {cp.x, (int)(g_layout.tableY + ci*g_colDispOv[col])};
}

// Start a flight animation for a sequence of cards.
// sx,sy = source pixel of head card (captured BEFORE commit).
// ex,ey = destination pixel of head card.
// ov    = overlap to use between cards during flight.
static void startCardAnim(
      const std::vector<Card>& moving,
      float sx, float sy,
      float ex, float ey,
      float ov,
      bool toFound,
      bool isPreview=false)
{
   CardAnim a;
   a.cards=moving;
   a.sx=sx; a.sy=sy;
   a.ex=ex; a.ey=ey;
   a.srcOv=ov;
   a.dstOv=ov;
   a.arcH=0.f;
   a.dur=0;
   a.startTime=timeGetTime();
   a.toFound=toFound;
   a.isPreview=isPreview;
   a.phase=0;
   g_cardAnims.push_back(a);
   if(isPreview) g_previewAnimating=true;
   else {
      g_animating=true;
      // Clear any stale deal-hide from a previous deal that may not have cleaned up
      for(int _i=0;_i<NUM_COLS;_i++) g_hideDealCol[_i]=-1;
   }
}

bool doClickMove(int col, int ci){
   if(!g_game.validSeqFrom(col,ci)) return false;
   auto& cards=g_game.cols[col];
   std::vector<Card> moving(cards.begin()+ci,cards.end());
   const Card& head=moving[0];

   // Capture source pixel position BEFORE commit
   POINT srcPt=cardPixelPos(col,ci);
   float ov=g_colDispOv[col];

   auto commitToFound=[&](int f)->bool{
      if(f<0) return false;
      // Defense in depth: a foundation move can only ever take the single
      // bottom-most card of a column. Every call site is already supposed
      // to guarantee this (moving.size()==1) before calling in, but
      // checking it again here — right where the erase actually happens —
      // means a mistake anywhere upstream can never again turn into
      // silently deleting the rest of the column instead of just moving
      // one card.
      if(ci!=(int)cards.size()-1) return false;
      POINT dst=g_layout.foundPos(f);
      g_game.saveUndo();
      g_game.found[f].push_back(head);
      cards.erase(cards.begin()+ci,cards.end());
      if(!g_winCounted){g_moveCount++;} clearAutoHistory(); if(!g_winCounted) playSound("click",g_volume);
      g_hideDstFound=f;  // hide newly-added card on foundation during flight
      markDirty(); onStateChanged();
      startCardAnim(moving,(float)srcPt.x,(float)srcPt.y,
                    (float)dst.x,(float)dst.y, ov, true);
      return true;
   };
   auto commitToCol=[&](int tc)->bool{
      if(tc<0) return false;
      int dstIdx=(int)g_game.cols[tc].size();
      g_game.saveUndo();
      for(auto& c:moving) g_game.cols[tc].push_back(c);
      cards.erase(cards.begin()+ci,cards.end());
      if(!g_winCounted){g_moveCount++;} clearAutoHistory(); if(!g_winCounted) playSound("click",g_volume);
      g_hideDstCol=tc; g_hideDstFromIdx=dstIdx;
      markDirty(); onStateChanged();
      // Snap dest column overlap to new target so board shows correct state immediately
      g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc];
      g_colAnimStart[tc]=0;
      // Compute exact landing position using split-overlap geometry
      float dstY=landingY(tc,dstIdx);
      float dstOv=landingOv(tc);
      float dstX=(float)g_layout.colPos(tc).x;
      startCardAnim(moving,(float)srcPt.x,(float)srcPt.y, dstX,dstY, ov,false);
      // Override srcOv on the just-added anim to match destination spacing
      if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv;
      return true;
   };

   // If an active hint exists and points to this exact card, honour it
   if((g_hintActive || g_hintBlinking) && g_hint.valid
      && g_hint.fromType==LOC_COLUMN
      && g_hint.fromIdx==col && g_hint.fromCard==ci
      && g_hint.toType==LOC_COLUMN && g_hint.toIdx>=0){
      int htc=g_hint.toIdx;
      if(g_game.canDropOnCol(head,htc) && !g_game.isStrictlyPointless(col,ci,htc))
         if(commitToCol(htc)) return true;
   }
   if((g_hintActive || g_hintBlinking) && g_hint.valid
      && g_hint.fromType==LOC_COLUMN
      && g_hint.fromIdx==col && g_hint.fromCard==ci
      && g_hint.toType==LOC_FOUNDATION
      && moving.size()==1){
      if(commitToFound(g_hint.toIdx)) return true;
   }

   // P0: Ace always to foundation
   if(head.rank==Ace && (int)moving.size()==1)
      if(commitToFound(g_game.bestFoundSlot(head))) return true;

   // Find all valid table destinations
   int bestKingCol=-1;    // king-sequence col (top priority among table moves)
   int bestNearCol=-1;    // nearest non-pointless col
   int bestDist=999;
   bool alreadyCorrect=g_game.alreadySittingCorrectly(col,ci);

   int bestFreeCol=-1;   // empty col (lower priority in free mode)
   for(int tc=0;tc<NUM_COLS;tc++){
      if(tc==col) continue;
      if(!g_game.canDropOnCol(head,tc)) continue;
      // For user clicks: only block the truly pointless move (identical card).
      // Do NOT block moves where card "already sits correctly" — user chose to move it.
      if(g_game.isStrictlyPointless(col,ci,tc)) continue;
      bool isEmpty=g_game.cols[tc].empty();
      if(g_freeColMode && isEmpty){
         if(bestFreeCol<0) bestFreeCol=tc;
      } else if(isKingSequenceCol(tc)){
         if(bestKingCol<0) bestKingCol=tc;
      } else {
         int d=std::abs(tc-col);
         if(d<bestDist||(d==bestDist&&tc>bestNearCol)){
            bestDist=d; bestNearCol=tc;
         }
      }
   }

   // P1: foundation — highest priority (same as getHint)
   if((int)moving.size()==1){
      int f=g_game.bestFoundSlot(head);
      if(f>=0 && g_game.canDropOnFound(head,f))
         if(commitToFound(f)) return true;
   }

   // P2: king-sequence column (high priority)
   if(bestKingCol>=0) return commitToCol(bestKingCol);

   // P3: nearest valid column
   if(bestNearCol>=0) return commitToCol(bestNearCol);

   // P4: empty column (lowest priority in free mode)
   if(bestFreeCol>=0) return commitToCol(bestFreeCol);

   return false;
}

void doDeal(){
   if(g_game.reserve.empty()){
      g_status=L"Rezerwa jest pusta!";
      markDirty(); invalidateGame(); return;}

   g_game.saveUndo(true); // tag: the move right after this is a reserve deal
   auto placed=g_game.dealReserve(); // {col, card} for each dealt card
   playSound("rozloz",g_volume);

   // Snap all column overlaps to new targets immediately
   recalcLayout();
   for(int col=0;col<NUM_COLS;col++){
      g_colDispOv[col]=g_colSrcOv[col]=g_colTgtOv[col];
      g_colAnimStart[col]=0;
   }
   onStateChanged();

   if(placed.empty()){ markDirty(); return; }

   // Hide dealt cards on board during flight (overlay shows them instead)
   for(int i=0;i<NUM_COLS;i++) g_hideDealCol[i]=-1;
   for(auto& pc:placed)
      g_hideDealCol[pc.first]=(int)g_game.cols[pc.first].size()-1;

   POINT rp=g_layout.reservePos();
   float srcX=(float)rp.x;
   float srcY=(float)rp.y;

   const DWORD DEAL_DUR = CARD_ANIM_MS*2; // 2× slower = 440ms
   const DWORD STAGGER  = 40;             // ms between card launches
   const float ARC_H    = 60.f;           // arc peak height in px

   DWORD now=timeGetTime();
   for(int i=0;i<(int)placed.size();i++){
      int tc=placed[i].first;
      Card card=placed[i].second;
      int dstIdx=(int)g_game.cols[tc].size()-1;
      float dstX=(float)g_layout.colPos(tc).x;
      float dstY=landingY(tc,dstIdx);
      float dstOv=landingOv(tc);

      CardAnim a;
      a.cards   ={card};
      a.sx=srcX; a.sy=srcY;
      a.ex=dstX; a.ey=dstY;
      a.srcOv=0.f; a.dstOv=dstOv;
      a.arcH    =ARC_H;
      a.dur     =DEAL_DUR;
      a.startTime=now+(DWORD)(i*STAGGER);
      a.toFound =false;
      a.isPreview=false;
      a.phase   =0;
      g_cardAnims.push_back(a);
      g_animating=true;
   }
   markDirty();
}

// Find what changed between two snapshots and start card animation
static void animateStateDiff(const Snapshot& before, const Snapshot& after){
   // Find columns that gained cards (destination) and lost cards (source)
   // Simple heuristic: find the col/found that lost cards as src, gained as dst
   // For single-seq moves this is straightforward
   // Detect reserve deal (redo): reserve shrank, multiple cols gained one card each
   {
      int reserveLost=(int)before.reserve.size()-(int)after.reserve.size();
      if(reserveLost>1){
         // Likely a deal: find which columns gained a card
         POINT rp=g_layout.reservePos();
         float srcX=(float)rp.x, srcY=(float)rp.y;
         const DWORD DEAL_DUR=CARD_ANIM_MS*2;
         const DWORD STAGGER=40;
         DWORD now=timeGetTime();
         int idx=0;
         for(int tc=0;tc<NUM_COLS;tc++){
            int gained=(int)after.cols[tc].size()-(int)before.cols[tc].size();
            if(gained!=1) continue;
            Card card=after.cols[tc].back();
            int dstIdx=(int)after.cols[tc].size()-1;
            g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
            float dstX=(float)g_layout.colPos(tc).x;
            float dstY=landingY(tc,dstIdx);
            float dstOv=landingOv(tc);
            g_hideDealCol[tc]=dstIdx;
            CardAnim a;
            a.cards={card}; a.sx=srcX; a.sy=srcY; a.ex=dstX; a.ey=dstY;
            a.srcOv=0.f; a.dstOv=dstOv; a.arcH=60.f; a.dur=DEAL_DUR;
            a.startTime=now+(DWORD)(idx*STAGGER);
            a.toFound=false; a.isPreview=false; a.phase=0;
            g_cardAnims.push_back(a); g_animating=true;
            idx++;
         }
         if(idx>0){ markDirty(); return; }
      }
   }
   // Detect undo of deal: reserve grew, multiple cols lost one card each (reverse flight)
   {
      int reserveGained=(int)after.reserve.size()-(int)before.reserve.size();
      if(reserveGained>1){
         // Clear any stale deal hide first
         for(int _i=0;_i<NUM_COLS;_i++) g_hideDealCol[_i]=-1;
         POINT rp=g_layout.reservePos();
         float dstX=(float)rp.x, dstY=(float)rp.y;
         const DWORD DEAL_DUR=CARD_ANIM_MS*2;
         const DWORD STAGGER=40;
         DWORD now=timeGetTime();
         int idx=0;
         for(int tc=0;tc<NUM_COLS;tc++){
            int lost=(int)before.cols[tc].size()-(int)after.cols[tc].size();
            if(lost!=1) continue;
            Card card=before.cols[tc].back();
            int srcIdx=(int)before.cols[tc].size()-1;
            // Snap src col to its current (after-undo) overlap for correct Y
            g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
            int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
            // Src position: where the card WAS, computed with after-col geometry
            // (col has one fewer card after undo, use before-col size for Y)
            // Approximate: use uniform overlap * srcIdx
            float srcX=(float)g_layout.colPos(tc).x;
            float srcY=(float)(g_layout.tableY+srcIdx*g_colDispOv[tc]);
            CardAnim a;
            a.cards={card}; a.sx=srcX; a.sy=srcY; a.ex=dstX; a.ey=dstY;
            a.srcOv=0.f; a.dstOv=0.f; a.arcH=60.f; a.dur=DEAL_DUR;
            a.startTime=now+(DWORD)(idx*STAGGER);
            a.toFound=false; a.isPreview=false; a.phase=0;
            g_cardAnims.push_back(a); g_animating=true;
            idx++;
         }
         if(idx>0){ markDirty(); return; }
      }
   }

   for(int fc=0;fc<NUM_COLS;fc++){
      int bsz=(int)before.cols[fc].size();
      int asz=(int)after.cols[fc].size();
      if(asz>=bsz) continue; // source lost cards
      // cards before[fc][asz..bsz] were removed
      std::vector<Card> moved(before.cols[fc].begin()+asz,before.cols[fc].end());
      if(moved.empty()) continue;
      float srcX=(float)g_layout.colPos(fc).x;
      float srcY=(float)(g_layout.tableY+asz*g_colDispOv[fc]);
      float ov=g_colDispOv[fc];
      // Find where they went in 'after'
      for(int tc=0;tc<NUM_COLS;tc++){
         if(tc==fc) continue;
         int absz=(int)after.cols[tc].size();
         int bbsz=(int)before.cols[tc].size();
         if(absz<=bbsz) continue;
         // Check suffix matches moved
         if(absz-bbsz!=(int)moved.size()) continue;
         bool match=true;
         for(int k=0;k<(int)moved.size();k++)
            if(!(after.cols[tc][bbsz+k]==moved[k])){match=false;break;}
         if(!match) continue;
         float dstX=(float)g_layout.colPos(tc).x;
         g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
         float dstY=landingY(tc,bbsz);
         float dstOv=landingOv(tc);
         g_hideDstCol=tc; g_hideDstFromIdx=bbsz;
         startCardAnim(moved,srcX,srcY,dstX,dstY,ov,false);
         if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv;
         return;
      }
      // Check foundations
      for(int f=0;f<NUM_FOUND;f++){
         int absz=(int)after.found[f].size();
         int bbsz=(int)before.found[f].size();
         if(absz-bbsz!=(int)moved.size()) continue;
         POINT dp=g_layout.foundPos(f);
         g_hideDstFound=f;
         startCardAnim(moved,srcX,srcY,(float)dp.x,(float)dp.y,ov,true);
         return;
      }
   }
   // Also check foundation → col (undo of placing on foundation)
   for(int f=0;f<NUM_FOUND;f++){
      int bsz=(int)before.found[f].size();
      int asz=(int)after.found[f].size();
      if(asz>=bsz) continue;
      std::vector<Card> moved(before.found[f].begin()+asz,before.found[f].end());
      if(moved.empty()) continue;
      POINT fp=g_layout.foundPos(f);
      float srcX=(float)fp.x, srcY=(float)fp.y;
      for(int tc=0;tc<NUM_COLS;tc++){
         int absz=(int)after.cols[tc].size();
         int bbsz=(int)before.cols[tc].size();
         if(absz<=bbsz) continue;
         float dstX=(float)g_layout.colPos(tc).x;
         g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
         float dstY=landingY(tc,bbsz);
         float dstOv=landingOv(tc);
         g_hideDstCol=tc; g_hideDstFromIdx=bbsz;
         startCardAnim(moved,srcX,srcY,dstX,dstY,g_colDispOv[tc],false);
         if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv;
         return;
      }
   }
}

void doUndo(){
   if(g_dealing || g_animating) return;
   Snapshot before;
   for(int i=0;i<NUM_COLS;i++) before.cols[i]=g_game.cols[i];
   for(int i=0;i<NUM_FOUND;i++) before.found[i]=g_game.found[i];
   before.reserve=g_game.reserve;
   if(g_game.undo()){
      if(!g_winCounted) g_moveCount++;
      KillTimer(g_hwnd,TIMER_NOMOVES);
      g_fw.stop(); KillTimer(g_hwnd,TIMER_FW);
      g_won=g_noMoves=false; g_status=L"Cofnięto ruch";
      // The board just changed under it, so any cached hint list (built for
      // the pre-undo board) is stale — its (fromIdx,fromCard,toIdx) indices
      // could now point at completely different cards, which is how "hint"
      // could end up suggesting a nonsensical move after an undo.
      g_hintActive=false; g_hintBlinking=false; g_hintList.clear(); g_hintIndex=0;
      invalidateBestMoveCache();
      // Undo rewrites what "the last 20 real moves" even means — without
      // this, the anti-loop history could still list the very move just
      // undone, so redoing it (or asking for a fresh automatic move on this
      // now-restored board) could get vetoed and a worse alternative played
      // instead, breaking the "undo+redo replays the identical move"
      // guarantee even though the search itself is perfectly deterministic.
      clearAutoHistory();
      playSound("cofnij",g_volume);
      recalcLayout(); markDirty(); invalidateGame();
      Snapshot after;
      for(int i=0;i<NUM_COLS;i++) after.cols[i]=g_game.cols[i];
      for(int i=0;i<NUM_FOUND;i++) after.found[i]=g_game.found[i];
      after.reserve=g_game.reserve;
      animateStateDiff(before,after);
   }
}

void doRedo(){
   if(g_dealing || g_animating) return;
   Snapshot before;
   for(int i=0;i<NUM_COLS;i++) before.cols[i]=g_game.cols[i];
   for(int i=0;i<NUM_FOUND;i++) before.found[i]=g_game.found[i];
   before.reserve=g_game.reserve;
   if(g_game.redo()){
      if(!g_winCounted) g_moveCount++;
      KillTimer(g_hwnd,TIMER_NOMOVES);
      g_won=g_noMoves=false; g_status=L"Ponowiono ruch";
      g_hintActive=false; g_hintBlinking=false; g_hintList.clear(); g_hintIndex=0;
      invalidateBestMoveCache();
      clearAutoHistory(); // see doUndo()'s comment — redo changes recent-move context too
      playSound("ponow",g_volume);
      recalcLayout(); markDirty(); invalidateGame();
      Snapshot after;
      for(int i=0;i<NUM_COLS;i++) after.cols[i]=g_game.cols[i];
      for(int i=0;i<NUM_FOUND;i++) after.found[i]=g_game.found[i];
      after.reserve=g_game.reserve;
      animateStateDiff(before,after);
   }
}


void doUndoDeal(){
   if(g_dealing || g_animating) return;
   if(g_game.undoToDeal()){
      KillTimer(g_hwnd,TIMER_NOMOVES);
      g_fw.stop(); KillTimer(g_hwnd,TIMER_FW);
      g_won=g_noMoves=false; g_status=L"Cofnięto do poprzedniego rozdania";
      g_hintActive=false; g_hintBlinking=false; g_hintList.clear(); g_hintIndex=0;
      invalidateBestMoveCache();
      clearAutoHistory(); // see doUndo()'s comment
      playSound("cofnij",g_volume);
      recalcLayout(); markDirty(); invalidateGame();
   }
}

void doRedoDeal(){
   if(g_dealing || g_animating) return;
   if(g_game.redoToDeal()){
      KillTimer(g_hwnd,TIMER_NOMOVES);
      g_won=g_noMoves=false; g_status=L"Ponowiono do następnego rozdania";
      g_hintActive=false; g_hintBlinking=false; g_hintList.clear(); g_hintIndex=0;
      invalidateBestMoveCache();
      clearAutoHistory(); // see doUndo()'s comment
      playSound("ponow",g_volume);
      recalcLayout(); markDirty(); invalidateGame();
   }
}

// Build the "Akcje" menu bar — called on creation and after fullscreen exit
static HMENU buildAkcjeMenu(){
   HMENU hBar  = CreateMenu();
   HMENU hPop  = CreatePopupMenu();
   AppendMenuW(hPop, MF_STRING,    ID_NEW,       L"🃏  Nowa gra  (F2)");
   AppendMenuW(hPop, MF_STRING,    ID_RESTART,   L"↺  Zacznij od początku tę grę");
   AppendMenuW(hPop, MF_STRING,    ID_PLAY_NUM,  L"#  Zagraj pasjans numer\u2026");
   AppendMenuW(hPop, MF_STRING,    ID_PLAY_WIN,  L"🏆  Zagraj wygrywaj\u0105cy");
   AppendMenuW(hPop, MF_STRING,    ID_PLAY_LOST, L"💔  Zagraj nieudany\u2026");
   AppendMenuW(hPop, MF_SEPARATOR, 0,            nullptr);
   AppendMenuW(hPop, MF_STRING,    ID_FS,        L"⛶  Pełny ekran  (↑ / F11)");
   AppendMenuW(hPop, MF_SEPARATOR, 0,            nullptr);
   AppendMenuW(hPop, MF_STRING,    ID_SAVE_AS,   L"💾  Zapisz grę jako...");
   AppendMenuW(hPop, MF_STRING,    ID_LOAD_FROM, L"📂  Wczytaj grę z pliku...");
   AppendMenuW(hPop, MF_SEPARATOR, 0,            nullptr);
   AppendMenuW(hPop, MF_STRING,    ID_HELP,      L"?  Pomoc  (H)");
   AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hPop,   L"Akcje");
   return hBar;
}
void toggleFS(){
   if(!g_isFS){
      GetWindowRect(g_hwnd,&g_winRect);
      g_winStyle=GetWindowLong(g_hwnd,GWL_STYLE);
      HMONITOR mon=MonitorFromWindow(g_hwnd,MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi={sizeof(mi)};GetMonitorInfo(mon,&mi);
      SetWindowLong(g_hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);
      SetMenu(g_hwnd,nullptr);  // hide menu bar
      SetWindowPos(g_hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
         mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,
         SWP_FRAMECHANGED);
      g_isFS=true;
   } else {
      SetWindowLong(g_hwnd,GWL_STYLE,g_winStyle);
      SetMenu(g_hwnd,buildAkcjeMenu());  // restore menu bar
      SetWindowPos(g_hwnd,nullptr,g_winRect.left,g_winRect.top,
         g_winRect.right-g_winRect.left,g_winRect.bottom-g_winRect.top,
         SWP_FRAMECHANGED|SWP_NOZORDER);
      g_isFS=false;
   }
}

// ============================================================================
// Paint
// ============================================================================
// Persistent composite DC for drag/firework overlay (reused across frames)
static HDC    g_compDC  = nullptr;
static HBITMAP g_compBmp = nullptr;
static int    g_compW   = 0, g_compH = 0;


// ============================================================================
// Mouse
// ============================================================================
void onLDown(int mx, int my){
   if(g_dealing || g_animating) return;
   cancelSamograj(); // manual click on the board — hand control back from self-play
   // ── Move label hit test ──────────────────────────────────────────────────
   // my is in main-window coords; label rect is in game-window coords (OY subtracted)
   const float OY=(float)Layout::TOOLBAR_H;
   float gmx=(float)mx, gmy=(float)my-OY;
   auto& lr=g_moveLabelDrawnRect;
   if(gmx>=lr.left&&gmx<=lr.right&&gmy>=lr.top&&gmy<=lr.bottom){
      if(!g_moveLabelCustom){
         // First click on auto-placed label: just start dragging, no reset
      } else {
         // If already custom and click without move → reset to auto on next LUp
         // (handled in onLUp); for now just start drag
      }
      g_moveLabelDragging=true;
      g_moveLabelDragOffX=gmx-lr.left;
      g_moveLabelDragOffY=gmy-lr.top;
      if(g_gameHwnd) SetCapture(g_gameHwnd);
      return;
   }
   // Cancel any hint preview on click
   if(g_previewAnimating){
      g_cardAnims.erase(
         std::remove_if(g_cardAnims.begin(),g_cardAnims.end(),
            [](const CardAnim& a){return a.isPreview;}),
         g_cardAnims.end());
      g_previewAnimating=false;
      if(g_cardAnims.empty()) g_animating=false;
      // Restore hidden source cards
      g_hideCol=-1; g_hideFromIdx=-1;
      markDirty();
   }
   g_hintActive=false;
   // NOTE: do NOT call markDirty here.
   // The board bitmap is not changed on mousedown.
   // We just record the potential drag start.

   POINT rp=g_layout.reservePos();
   if(hitCard(mx,my,rp.x,rp.y)&&!g_game.reserve.empty()){doDeal();return;}

   for(int i=0;i<NUM_FOUND;i++){
      POINT fp=g_layout.foundPos(i);
      if(hitCard(mx,my,fp.x,fp.y)&&!g_game.found[i].empty()){
         g_game.saveUndo();        // save before removing so undo restores correctly
         Card c=g_game.found[i].back();
         g_game.found[i].pop_back();
         g_drag.active=true; g_drag.moved=false;
         g_drag.fromCol=-1; g_drag.fromFound=i; g_drag.startIdx=0;
         g_drag.cards={c};
         g_drag.offX=mx-fp.x; g_drag.offY=my-fp.y;
         g_drag.mx=mx; g_drag.my=my; g_drag.downX=mx; g_drag.downY=my;
         // Board bitmap: card is still drawn there — that's fine,
         // ghost will be drawn on top, so it appears as if it lifted.
         // We only need a proper markDirty when the drag ends.
         invalidateGame(); return;
      }
   }

   for(int col=0;col<NUM_COLS;col++){
      POINT cp=g_layout.colPos(col);
      auto& cards=g_game.cols[col];
      if(cards.empty()) continue;
      int cx=cp.x;
      if(mx<cx||mx>cx+g_layout.cardW) continue;

      // Compute actual rendered Y positions using same variable-overlap logic as drawing
      int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
      ColOverlapInfo oi=calcColOverlap(col,avH);

      // Build actual Y array
      std::vector<int> cardY((int)cards.size());
      float y=(float)g_layout.tableY;
      for(int ci=0;ci<(int)cards.size();ci++){
         cardY[ci]=(int)y;
         if(ci+1<(int)cards.size()){
            bool nextInSeq=(ci+1)>=oi.seqStart;
            float ov=nextInSeq?oi.seqOv:oi.topOv;
            ov=std::max(12.f,ov);
            y+=ov;
         }
      }

      // Hit test from bottom card upward using actual positions
      for(int ci=(int)cards.size()-1;ci>=0;ci--){
         int cy=cardY[ci];
         // Hit region: from cy to next card's Y (or full card height for bottom card)
         int cyNext=(ci==(int)cards.size()-1)
            ? cy+g_layout.cardH
            : cardY[ci+1];
         if(my<cy||my>=cyNext) continue;
         if(!g_game.validSeqFrom(col,ci)) continue;
         g_drag.active=true; g_drag.moved=false;
         g_drag.fromCol=col; g_drag.fromFound=-1; g_drag.startIdx=ci;
         g_drag.cards.assign(cards.begin()+ci,cards.end());
         cards.erase(cards.begin()+ci,cards.end());
         g_drag.offX=mx-cx; g_drag.offY=my-cy;
         g_drag.mx=mx; g_drag.my=my; g_drag.downX=mx; g_drag.downY=my;
         markDirty();
         invalidateGame(); return;
      }
   }
}

void onMouseMove(int mx, int my){
   if(g_moveLabelDragging){
      const float OY=(float)Layout::TOOLBAR_H;
      g_moveLabelX=(float)mx-OY-g_moveLabelDragOffX; // note: mx already adjusted by GameWndProc+TH offset
      // Actually mx,my are main-window coords; game-window Y = my-OY
      g_moveLabelX=(float)mx-g_moveLabelDragOffX;
      g_moveLabelY=(float)my-OY-g_moveLabelDragOffY;
      g_moveLabelCustom=true;
      invalidateGame();
      return;
   }
   if(!g_drag.active) return;
   g_drag.mx=mx; g_drag.my=my;
   if(!g_drag.moved){
      int dx=mx-g_drag.downX,dy=my-g_drag.downY;
      if(dx*dx+dy*dy>DRAG_THR*DRAG_THR) g_drag.moved=true;
   }
   invalidateGame();
}

// Save an undo snapshot for a drag-and-drop move.
// While a card/sequence is being dragged it has been REMOVED from g_game, so a
// plain saveUndo() at drop time would record a board with those cards missing,
// and undoing that move would delete them from the game for good. Put them back
// where they came from, snapshot, then lift them out again.
// (Foundation drags already saved a complete snapshot at mouse-down.)
static void saveUndoForDrag(){
   if(g_drag.fromCol>=0){
      auto& fc=g_game.cols[g_drag.fromCol];
      fc.insert(fc.begin()+g_drag.startIdx,g_drag.cards.begin(),g_drag.cards.end());
      g_game.saveUndo();
      fc.erase(fc.begin()+g_drag.startIdx,fc.end());
   }
}

void onLUp(int mx, int my){
   if(g_moveLabelDragging){
      const float OY=(float)Layout::TOOLBAR_H;
      // Compute final position after drag
      float finalX=(float)mx-g_moveLabelDragOffX;
      float finalY=(float)my-OY-g_moveLabelDragOffY;
      // Check if mouse actually moved (vs. simple click)
      float ddx=finalX-g_moveLabelX, ddy=finalY-g_moveLabelY;
      bool didMove=(ddx*ddx+ddy*ddy>25.f); // 5px threshold
      if(didMove){
         // Drag completed: save new position
         g_moveLabelX=finalX;
         g_moveLabelY=finalY;
         g_moveLabelCustom=true;
         saveSettings();
      }
      // Click without drag: do nothing, position unchanged
      g_moveLabelDragging=false;
      ReleaseCapture();
      invalidateGame();
      return;
   }
   if(!g_drag.active) return;

   if(!g_drag.moved){
      // Click: restore cards, try auto-move
      if(g_drag.fromCol>=0){
         int col=g_drag.fromCol, ci=g_drag.startIdx;
         auto& fc=g_game.cols[col];
         fc.insert(fc.begin()+ci,g_drag.cards.begin(),g_drag.cards.end());
         g_drag.active=false; g_drag.cards.clear(); g_drag.fromCol=-1;
         markDirty();
         if(!doClickMove(col,ci)){ playSound("nono",g_volume); invalidateGame(); }
      } else if(g_drag.fromFound>=0){
         // Click on foundation (no drag): try to move card to a column
         int fi=g_drag.fromFound;
         Card c=g_drag.cards[0];
         bool moved=false;
         // Try each column: prefer non-empty cols first, then empty
         for(int pass=0;pass<2&&!moved;pass++){
            for(int tc=0;tc<NUM_COLS&&!moved;tc++){
               bool empty=g_game.cols[tc].empty();
               if(pass==0&&empty) continue;   // pass 0: skip empty
               if(pass==1&&!empty) continue;  // pass 1: only empty
               if(!g_game.canDropOnCol(c,tc)) continue;
               // Perform move
               g_game.cols[tc].push_back(c);
               // saveUndo was already called at drag-start
               g_hideDstCol=tc; g_hideDstFromIdx=(int)g_game.cols[tc].size()-1;
               if(!g_winCounted) g_moveCount++;
               clearAutoHistory(); playSound("click",g_volume);
               markDirty(); onStateChanged();
               g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
               float dstY=landingY(tc,(int)g_game.cols[tc].size()-1);
               float dstOv=landingOv(tc);
               POINT fp=g_layout.foundPos(fi);
               startCardAnim({c},(float)fp.x,(float)fp.y,
                             (float)g_layout.colPos(tc).x,dstY,
                             (float)g_layout.cardH*0.3f,false);
               if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv;
               moved=true;
            }
         }
         if(!moved){
            // No valid column — put card back on foundation
            g_game.found[fi].push_back(c);
            g_game.undoStack.pop_back(); // discard the saveUndo from drag start
            playSound("nono",g_volume);
         }
         g_drag.active=false; g_drag.cards.clear(); g_drag.fromFound=-1;
         markDirty(); invalidateGame();
      }
      return;
   }

   bool placed=false;
   if((int)g_drag.cards.size()==1){
      for(int i=0;i<NUM_FOUND;i++){
         POINT fp=g_layout.foundPos(i);
         if(hitCard(mx,my,fp.x,fp.y)&&g_game.canDropOnFound(g_drag.cards[0],i)){
            saveUndoForDrag();
            g_game.found[i].push_back(g_drag.cards[0]);
            if(!g_winCounted){g_moveCount++;} clearAutoHistory(); placed=true; break;}
      }
   }
   if(!placed){
      int bc=-1,bd=999999;
      for(int col=0;col<NUM_COLS;col++){
         if(col==g_drag.fromCol&&g_drag.fromFound<0) continue;
         POINT cp=g_layout.colPos(col);
         auto& cards=g_game.cols[col];
         int bot=cards.empty()?cp.y+g_layout.cardH
                 :colCardY(col,(int)cards.size()-1)+g_layout.cardH;
         if(mx<cp.x||mx>cp.x+g_layout.cardW||my<cp.y||my>bot) continue;
         if(!g_game.canDropOnCol(g_drag.cards[0],col)) continue;
         int d=abs(mx-(cp.x+g_layout.cardW/2));
         if(d<bd){bd=d;bc=col;}
      }
      if(bc>=0){
         saveUndoForDrag();
         for(auto& c:g_drag.cards) g_game.cols[bc].push_back(c);
         if(!g_winCounted){g_moveCount++;} clearAutoHistory(); placed=true;
      }
   }
   if(!placed){
      if(g_drag.fromCol>=0){
         auto& fc=g_game.cols[g_drag.fromCol];
         fc.insert(fc.begin()+g_drag.startIdx,g_drag.cards.begin(),g_drag.cards.end());
      } else if(g_drag.fromFound>=0){
         g_game.found[g_drag.fromFound].push_back(g_drag.cards[0]);
         // Discard the saveUndo we called at drag start — move was cancelled
         if(!g_game.undoStack.empty()) g_game.undoStack.pop_back();
      }
   } else {
      if(!g_winCounted) playSound("click",g_volume);
   }
   g_drag.active=false; g_drag.cards.clear();
   g_drag.fromCol=-1; g_drag.fromFound=-1;
   markDirty();
   if(placed) onStateChanged();
   else invalidateGame();
}

// ============================================================================
// WndProc
// ============================================================================
// Help dialog
// ============================================================================

// ── Statistics ──────────────────────────────────────────────────────────────
// stats IDs and globals moved to top

static void saveStats(){
   std::wstring ini=getIniPath();
   wchar_t buf[32];
   auto wr=[&](const wchar_t* k,int v){ wsprintfW(buf,L"%d",v); WritePrivateProfileStringW(L"Stats",k,buf,ini.c_str()); };
   wr(L"GamesKing",   g_statsGames[0]);
   wr(L"WinsKing",    g_statsWins[0]);
   wr(L"GamesFree",   g_statsGames[1]);
   wr(L"WinsFree",    g_statsWins[1]);
   wr(L"MoveLossKing",g_statsMoveSum[0][0]);
   wr(L"MoveWinKing", g_statsMoveSum[0][1]);
   wr(L"MoveLossFree",g_statsMoveSum[1][0]);
   wr(L"MoveWinFree", g_statsMoveSum[1][1]);
   wr(L"TimeLossKing",g_statsTimeSum[0][0]);
   wr(L"TimeWinKing", g_statsTimeSum[0][1]);
   wr(L"TimeLossFree",g_statsTimeSum[1][0]);
   wr(L"TimeWinFree", g_statsTimeSum[1][1]);
   wr(L"RecMovesKing",g_statsRecordMoves[0]);
   wr(L"RecMovesFree",g_statsRecordMoves[1]);
   wr(L"RecTimeKing", g_statsRecordTime[0]);
   wr(L"RecTimeFree", g_statsRecordTime[1]);
}

static void loadStats(){
   std::wstring ini=getIniPath();
   auto rd=[&](const wchar_t* k,int def=0)->int{
      return (int)GetPrivateProfileIntW(L"Stats",k,def,ini.c_str());};
   g_statsGames[0]      =rd(L"GamesKing");
   g_statsWins[0]       =rd(L"WinsKing");
   g_statsGames[1]      =rd(L"GamesFree");
   g_statsWins[1]       =rd(L"WinsFree");
   g_statsMoveSum[0][0] =rd(L"MoveLossKing");
   g_statsMoveSum[0][1] =rd(L"MoveWinKing");
   g_statsMoveSum[1][0] =rd(L"MoveLossFree");
   g_statsMoveSum[1][1] =rd(L"MoveWinFree");
   g_statsTimeSum[0][0] =rd(L"TimeLossKing");
   g_statsTimeSum[0][1] =rd(L"TimeWinKing");
   g_statsTimeSum[1][0] =rd(L"TimeLossFree");
   g_statsTimeSum[1][1] =rd(L"TimeWinFree");
   g_statsRecordMoves[0]=rd(L"RecMovesKing",-1);
   g_statsRecordMoves[1]=rd(L"RecMovesFree",-1);
   g_statsRecordTime[0] =rd(L"RecTimeKing", -1);
   g_statsRecordTime[1] =rd(L"RecTimeFree", -1);
   for(int m=0;m<2;m++){
      if(g_statsGames[m]<0) g_statsGames[m]=0;
      if(g_statsWins[m]<0||g_statsWins[m]>g_statsGames[m]) g_statsWins[m]=0;
      for(int o=0;o<2;o++){
         if(g_statsMoveSum[m][o]<0) g_statsMoveSum[m][o]=0;
         if(g_statsTimeSum[m][o]<0) g_statsTimeSum[m][o]=0;
      }
   }
}
// Stats window WndProc
static LRESULT CALLBACK StatsWndProc(HWND dlg,UINT msg,WPARAM wp,LPARAM lp){
   switch(msg){
   case WM_KEYDOWN:
      if(wp==VK_ESCAPE){DestroyWindow(dlg);return 0;}
      break;
   case WM_NOTIFY:{
      NMHDR* hdr=(NMHDR*)lp;
      if(hdr->code==TCN_SELCHANGE){
         HWND hTab=GetDlgItem(dlg,900);
         int sel=(int)SendMessageW(hTab,TCM_GETCURSEL,0,0);
         // Show/hide stat pages
         ShowWindow(GetDlgItem(dlg,801),sel==0?SW_SHOW:SW_HIDE);
         ShowWindow(GetDlgItem(dlg,802),sel==1?SW_SHOW:SW_HIDE);
      }
      return 0;}
   case WM_COMMAND:
      if(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL){DestroyWindow(dlg);return 0;}
      if(LOWORD(wp)==1001){
         for(int m=0;m<2;m++){
            g_statsGames[m]=g_statsWins[m]=0;
            g_statsMoveSum[m][0]=g_statsMoveSum[m][1]=0;
            g_statsTimeSum[m][0]=g_statsTimeSum[m][1]=0;
            g_statsRecordMoves[m]=-1; g_statsRecordTime[m]=-1;
         }
         g_gameStarted=false; g_moveCount=0; g_gameSeconds=0;
         saveStats(); DestroyWindow(dlg); newGame(); return 0;
      }
      return 0;
   case WM_DESTROY: PostMessageW(dlg,WM_NULL,0,0); return 0;
   case WM_CLOSE:   DestroyWindow(dlg); return 0;
   }
   return DefWindowProcW(dlg,msg,wp,lp);
}

static void showStats(HWND parent){
   HINSTANCE hInst=(HINSTANCE)GetWindowLongPtrW(parent,GWLP_HINSTANCE);
   INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_TAB_CLASSES};
   InitCommonControlsEx(&icc);
   static bool reg=false;
   if(!reg){
      WNDCLASSEXW wc={sizeof(wc)};
      wc.lpfnWndProc=StatsWndProc; wc.hInstance=hInst;
      wc.lpszClassName=L"PasjansStats";
      wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
      RegisterClassExW(&wc); reg=true;
   }

   const int DW=310, DH=350; // client area size (+10 for buttons)
   RECT adjR={0,0,DW,DH};
   AdjustWindowRectEx(&adjR,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
   HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME,L"PasjansStats",L"Statystyki",
      WS_POPUP|WS_CAPTION|WS_SYSMENU,0,0,adjR.right-adjR.left,adjR.bottom-adjR.top,parent,nullptr,hInst,nullptr);
   if(!dlg) return;

   HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
   HFONT hfB=CreateFontW(-MulDiv(9,GetDeviceCaps(GetDC(nullptr),LOGPIXELSY),72),
      0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");

   auto mk=[&](HWND par,const wchar_t* cls,const wchar_t* txt,DWORD sty,
               int x,int y,int w,int h,int id,bool bold=false)->HWND{
      HWND hw=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|sty,
         x,y,w,h,par,(HMENU)(INT_PTR)id,hInst,nullptr);
      SendMessageW(hw,WM_SETFONT,(WPARAM)(bold?hfB:hf),TRUE);
      return hw;};

   // Helper: format average
   auto avgMoves=[](int sum,int cnt,wchar_t* buf){
      if(cnt<=0){wcscpy(buf,L"-");return;}
      int t=(int)((double)sum/cnt*10+0.5);
      wsprintfW(buf,L"%d.%d",t/10,t%10);};
   auto avgTime=[](int sum,int cnt,wchar_t* buf){
      if(cnt<=0){wcscpy(buf,L"-");return;}
      int s=(int)((double)sum/cnt+0.5);
      wcscpy(buf,fmtTime(s).c_str());};
   auto recMoves=[](int v,wchar_t* buf){
      if(v<0)wcscpy(buf,L"-"); else wsprintfW(buf,L"%d",v);};
   auto recTime=[](int v,wchar_t* buf){
      if(v<0)wcscpy(buf,L"-"); else wcscpy(buf,fmtTime(v).c_str());};

   // Tab control
   HWND hTab=mk(dlg,WC_TABCONTROLW,L"",WS_CLIPSIBLINGS,5,5,DW-15,DH-50,900);
   TCITEMW ti={TCIF_TEXT};
   ti.pszText=(LPWSTR)L"Tylko kr\u00f3l"; SendMessageW(hTab,TCM_INSERTITEMW,0,(LPARAM)&ti);
   ti.pszText=(LPWSTR)L"Dowolna karta"; SendMessageW(hTab,TCM_INSERTITEMW,1,(LPARAM)&ti);

   // Page panels
   const int PX=10,PY=32,PW=DW-28,PH=DH-90;
   HWND page[2];
   page[0]=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,PX,PY,PW,PH,dlg,(HMENU)801,hInst,nullptr);
   page[1]=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,PX,PY,PW,PH,dlg,(HMENU)802,hInst,nullptr);

   wchar_t buf[32];
   const int LW=130, VW=60, VC=140, RH=20, R0=4;

   for(int m=0;m<2;m++){
      HWND pg=page[m];
      int wins=g_statsWins[m], games=g_statsGames[m], losses=games-wins;
      auto row=[&](int r,const wchar_t* lbl,const wchar_t* val){
         mk(pg,L"STATIC",lbl,SS_LEFT,4,R0+r*RH,LW,18,0);
         mk(pg,L"STATIC",val,SS_CENTER,VC,R0+r*RH,VW,18,0);};

      wsprintfW(buf,L"%d",games);             row(0,L"Rozegrane:",buf);
      wsprintfW(buf,L"%d",wins);              row(1,L"Wygrane:",buf);
      int pct10=games>0?(int)(wins*1000.0/games+0.5):0; // percent × 10, rounded
      wsprintfW(buf,L"%d.%d%%",pct10/10,pct10%10);  row(2,L"% wygranych:",buf);
      avgMoves(g_statsMoveSum[m][1],wins,buf); row(3,L"Śr. ruchów wyg.:",buf);
      avgMoves(g_statsMoveSum[m][0],losses,buf);row(4,L"Śr. ruchów prz.:",buf);
      avgTime(g_statsTimeSum[m][1],wins,buf);  row(5,L"Śr. czas wyg.:",buf);
      avgTime(g_statsTimeSum[m][0],losses,buf);row(6,L"Śr. czas prz.:",buf);

      // Records
      CreateWindowExW(0,L"STATIC",L"",SS_ETCHEDHORZ|WS_CHILD|WS_VISIBLE,
         4,R0+7*RH+2,PW-8,2,pg,nullptr,hInst,nullptr);
      mk(pg,L"STATIC",L"Rekordy wygranych:",SS_LEFT,4,R0+7*RH+8,PW-8,18,0,true);
      recMoves(g_statsRecordMoves[m],buf); row(8+1,L"Najmniej ruchów:",buf);
      recTime(g_statsRecordTime[m],buf);   row(9+1,L"Najkrótszy czas:",buf);
   }

   // Buttons
   mk(dlg,L"BUTTON",L"Resetuj",           BS_PUSHBUTTON,   15,DH-37,100,26,1001);
   mk(dlg,L"BUTTON",L"Zamknij",           BS_DEFPUSHBUTTON,DW-115,DH-37,100,26,IDOK);

   // Centre
   RECT pr; GetWindowRect(parent,&pr);
   SetWindowPos(dlg,HWND_TOP,
      pr.left+(pr.right-pr.left-(adjR.right-adjR.left))/2,
      pr.top +(pr.bottom-pr.top-(adjR.bottom-adjR.top))/2,
      0,0,SWP_NOSIZE|SWP_SHOWWINDOW);

   // Open on current game mode tab
   int initTab=(int)g_freeColMode;
   SendMessageW(hTab,TCM_SETCURSEL,initTab,0);
   ShowWindow(page[0],initTab==0?SW_SHOW:SW_HIDE);
   ShowWindow(page[1],initTab==1?SW_SHOW:SW_HIDE);

   EnableWindow(parent,FALSE);
   MSG m;
   while(IsWindow(dlg)&&GetMessage(&m,nullptr,0,0)){
      if(m.message==WM_QUIT){PostQuitMessage((int)m.wParam);break;}
      if(!IsDialogMessage(dlg,&m)){TranslateMessage(&m);DispatchMessage(&m);}
   }
   EnableWindow(parent,TRUE); SetFocus(parent);
}


static void showHelp(HWND parent){
   const wchar_t* text =
      L"PASJANS DZIADKOWY\n"
      L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\n\n"
      L"CEL GRY\n"
      L"Umie\u015b\u0107 wszystkich 8 kr\u00f3l\u00f3w na kolumnach (z prawidłowymi sekwensami)\n"
      L"lub na stosach. Gr\u0119 rozgrywa si\u0119 dwiema taliami (104 karty).\n\n"
      L"ZASADY\n"
      L"• Na stole 10 kolumn. Mo\u017cna przenie\u015b\u0107 kart\u0119 lub sekwens (naprzemienne\n"
      L"  kolory, rosn\u0105ca warto\u015b\u0107 ku g\u00f3rze) na kart\u0119 o 1 wy\u017cszej, przeciwnego koloru.\n"
      L"• Pusta kolumna przyjmuje tylko kr\u00f3la (lub sekwens z kr\u00f3lem na g\u00f3rze).\n"
      L"• Stosy (g\u00f3ra): As \u2192 2 \u2192 ... \u2192 Kr\u00f3l, ten sam kolor (suit).\n"
      L"• Ze stosu mo\u017cna wr\u00f3ci\u0107 kart\u0119 na st\u00f3\u0142.\n"
      L"• Rezerwa: kliknij stos nierozłożonych kart, aby dołożyć po 1 karcie\n"
      L"  do ka\u017cdej kolumny (z wyj\u0105tkiem kolumn z Kr\u00f3lem na szczycie sekwensu).\n\n"
      L"STEROWANIE\n"
      L"  Lewy klik         Przeci\u0105gnij kart\u0119/sekwens lub kliknij dla autu\n"
      L"  Lewy klik (krótki) Auto-ruch: najbli\u017csza mo\u017cliwa kolumna\n"
      L"  \u2190 / Ctrl+Z    Cofnij ruch\n"
      L"  \u2192 / Ctrl+Y    Ponów ruch\n"
      L"  P                Podpowied\u017a\n"
      L"  S                Statystyki\n"
      L"\n"
      L"TRYB GRY (Ustawienia)\n"
      L"  Wolne miejsce: Kr\u00f3l  – tylko kr\u00f3l lub sekwens z kr\u00f3lem mo\u017ce\n"
      L"                              trafić na puste miejsce\n"
      L"  Wolne miejsce: Dowolny  – ka\u017cda karta lub sekwens mo\u017ce\n"
      L"                              trafić na puste miejsce\n"
      L"  H                Pomoc (ten ekran)\n"
      L"  \u2191 / F11      Pe\u0142ny ekran\n"
      L"  Escape           Wyj\u015bcie z pe\u0142nego ekranu\n"
      L"  A                Automatyczny ruch (wykonuje podpowied\u017a)\n"
      L"  F2               Nowa gra\n\n"
      L"D\u017bWI\u0118KI\n"
      L"  Pliki .wav w katalogu gry (click, nono, sukces, nowa, rozloz, koniec)\n";

   MessageBoxW(parent, text, L"Pomoc – Pasjans Dziadkowy", MB_OK|MB_ICONINFORMATION);
}

// ============================================================================
// Settings dialog
// ============================================================================
// Settings dialog state
struct SettingsState {
   // General tab
   HWND  hVolLbl, hSlider, hMoveHiRadio[3];
   HWND  hBgSwatch[16];      // owner-drawn color squares (BG_COUNT of these used)
   int   bgSel;              // currently selected swatch index (live, before OK)
   HWND  hFreeColK, hFreeColAny; // radio buttons
   HWND  hDepthFreeEdit, hDepthKingEdit; // AI search depth per mode
   int   tmpBg;
   float tmpVol;
   bool  tmpFreeCol;
   int   tmpDepthFree, tmpDepthKing;
   bool  accepted;
   HWND  hAnimSlider, hAnimLbl; // "Prędkość animacji" — dlg children, like hSlider/hVolLbl (see there for why)
   int   tmpAnimStep;
   HWND  hCheckUpdates; // "Sprawdzaj aktualizacje przy starcie" checkbox
   // Sounds tab
   HWND  hTab;
   HWND  hSoundEdit[SOUND_COUNT];
   HWND  hSoundBrowse[SOUND_COUNT];
   HWND  hSoundPlay[SOUND_COUNT];
   HWND  hSoundMute[SOUND_COUNT];   // "✕" — no sound for this event
   HWND  hSoundReset[SOUND_COUNT];  // "↺" — restore default sound
   bool  tmpMuted[SOUND_COUNT];
   HWND  hGenPage, hSndPage, hKeyPage;
   HWND  hTooltip;
   std::wstring tmpPaths[SOUND_COUNT];
   int   previewIdx=-1; // which sound (if any) is currently being previewed
   // Keys tab
   KeyBinding tmpKeys[KA_COUNT];
   HWND  hKeyBtn[KA_COUNT][2]; // [action][key1/key2] — shows current key name
   int   captureAction=-1;     // which action is being captured (-1=none)
   int   captureSlot=-1;       // which slot (0 or 1)
};
static SettingsState s_st;

static void switchTab(int idx){
   ShowWindow(s_st.hGenPage, idx==0?SW_SHOW:SW_HIDE);
   ShowWindow(s_st.hSndPage, idx==1?SW_SHOW:SW_HIDE);
   ShowWindow(s_st.hKeyPage, idx==2?SW_SHOW:SW_HIDE);
   // Slider and vol label are children of dlg — show/hide with sounds tab
   if(s_st.hSlider)  ShowWindow(s_st.hSlider,  idx==1?SW_SHOW:SW_HIDE);
   if(s_st.hVolLbl)  ShowWindow(s_st.hVolLbl,  idx==1?SW_SHOW:SW_HIDE);
   // Animation-speed slider/label are also dlg children (same reason) — show with General tab
   if(s_st.hAnimSlider) ShowWindow(s_st.hAnimSlider, idx==0?SW_SHOW:SW_HIDE);
   if(s_st.hAnimLbl)    ShowWindow(s_st.hAnimLbl,    idx==0?SW_SHOW:SW_HIDE);
   // Leaving the Sounds tab (or entering it) should cut short any sample
   // preview that's currently playing, with a quick 0.3s fade.
   SoundSystem::instance().fadeOutAll(300);
   s_st.previewIdx=-1;
}

// Subclass proc for hSndPage: forwards WM_COMMAND to the parent dialog
// so sound buttons (which are children of hSndPage) are handled by SettingsWndProc
// Custom tooltip popup — used instead of the comctl32 tooltip control, which
// (for reasons not fully pinned down — possibly a manifest/version issue)
// would not display inside the Settings dialog despite window creation,
// tool registration, and even forced TTM_TRACKACTIVATE all reporting success.
// This is a plain self-drawn popup window we show/hide/position ourselves.
static LRESULT CALLBACK CustomTipProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
   if(msg==WM_PAINT){
      PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
      RECT rc; GetClientRect(hwnd,&rc);
      HBRUSH bg=CreateSolidBrush(RGB(255,255,225));
      FillRect(dc,&rc,bg); DeleteObject(bg);
      FrameRect(dc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
      wchar_t buf[256]; GetWindowTextW(hwnd,buf,256);
      RECT trc=rc; InflateRect(&trc,-5,-3);
      SetBkMode(dc,TRANSPARENT);
      HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
      HFONT old=(HFONT)SelectObject(dc,f);
      DrawTextW(dc,buf,-1,&trc,DT_LEFT|DT_TOP|DT_WORDBREAK);
      SelectObject(dc,old);
      EndPaint(hwnd,&ps);
      return 0;
   }
   return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ensureCustomTipClass(HINSTANCE hInst){
   static bool done=false; if(done) return; done=true;
   WNDCLASSW wc={};
   wc.lpfnWndProc=CustomTipProc; wc.hInstance=hInst;
   wc.lpszClassName=L"PsjCustomTip";
   wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
   RegisterClassW(&wc);
}
static LRESULT CALLBACK TipRelaySubclassProc(HWND hwnd, UINT msg,
      WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data){
   static const UINT_PTR TIP_TIMER_ID = 4242;
   HWND hTip=(HWND)data;
   if(msg==WM_MOUSEMOVE){
      TRACKMOUSEEVENT tme{sizeof(tme)}; tme.dwFlags=TME_LEAVE; tme.hwndTrack=hwnd;
      TrackMouseEvent(&tme);
      if(!IsWindowVisible(hTip)) SetTimer(hwnd,TIP_TIMER_ID,500,nullptr);
   } else if(msg==WM_MOUSELEAVE){
      KillTimer(hwnd,TIP_TIMER_ID);
      ShowWindow(hTip,SW_HIDE);
   } else if(msg==WM_TIMER && wp==TIP_TIMER_ID){
      KillTimer(hwnd,TIP_TIMER_ID);
      const wchar_t* text=(const wchar_t*)GetPropW(hwnd,L"TipText");
      if(text){
         RECT rc; GetWindowRect(hwnd,&rc);
         SetWindowTextW(hTip,text);
         RECT calc={0,0,260,1000};
         HDC dc=GetDC(hTip);
         HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
         HFONT old=(HFONT)SelectObject(dc,f);
         DrawTextW(dc,text,-1,&calc,DT_CALCRECT|DT_WORDBREAK|DT_LEFT);
         SelectObject(dc,old); ReleaseDC(hTip,dc);
         int w=calc.right-calc.left+12, h=calc.bottom-calc.top+8;
         // Show ABOVE the control (not below) so the mouse cursor doesn't
         // sit on top of the tooltip text and obscure it.
         SetWindowPos(hTip,HWND_TOPMOST,rc.left,rc.top-h-4,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
         InvalidateRect(hTip,nullptr,TRUE);
      }
      return 0;
   }
   return DefSubclassProc(hwnd,msg,wp,lp);
}

static LRESULT CALLBACK SndPageSubclassProc(HWND hwnd, UINT msg,
      WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR){
   if(msg==WM_COMMAND || msg==WM_DRAWITEM){
      // Forward to parent (the settings dialog)
      return SendMessageW(GetParent(hwnd),msg,wp,lp);
   }
   return DefSubclassProc(hwnd,msg,wp,lp);
}


static LRESULT CALLBACK SettingsWndProc(HWND dlg,UINT msg,WPARAM wp,LPARAM lp){
   switch(msg){
   case WM_DRAWITEM:{
      DRAWITEMSTRUCT* dis=(DRAWITEMSTRUCT*)lp;
      for(int i=0;i<BG_COUNT;i++){
         if(dis->hwndItem==s_st.hBgSwatch[i]){
            HBRUSH br=CreateSolidBrush(RGB(BG_COLORS[i].r,BG_COLORS[i].g,BG_COLORS[i].b));
            FillRect(dis->hDC,&dis->rcItem,br);
            DeleteObject(br);
            RECT rc=dis->rcItem;
            HPEN pen=CreatePen(PS_SOLID, (i==s_st.bgSel)?3:1,
               (i==s_st.bgSel)?RGB(255,220,0):RGB(90,90,90));
            HPEN old=(HPEN)SelectObject(dis->hDC,pen);
            HBRUSH nullBr=(HBRUSH)SelectObject(dis->hDC,GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC,rc.left,rc.top,rc.right,rc.bottom);
            SelectObject(dis->hDC,nullBr);
            SelectObject(dis->hDC,old);
            DeleteObject(pen);
            return TRUE;
         }
      }
      return FALSE;}
   case WM_KEYDOWN:{
      // Key capture mode: assign pressed key to the waiting slot
      if(s_st.captureAction>=0){
         DWORD vk=(DWORD)wp;
         // Ignore modifier keys alone
         if(vk==VK_SHIFT||vk==VK_CONTROL||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN)
            return 0;
         if(vk==VK_ESCAPE){
            // Escape = clear binding
            vk=0;
         }
         int a=s_st.captureAction, sl=s_st.captureSlot;
         bool shiftHeld=(GetKeyState(VK_SHIFT)&0x8000)!=0;
         if(sl==0) { s_st.tmpKeys[a].key1=vk; }
         else      { s_st.tmpKeys[a].key2=vk; }
         if(vk!=0) s_st.tmpKeys[a].shift=shiftHeld; // Escape (clear) leaves shift as-is
         // Update button text
         std::wstring nm=bindingName(vk, s_st.tmpKeys[a].shift);
         SetWindowTextW(s_st.hKeyBtn[a][sl],nm.c_str());
         s_st.captureAction=-1; s_st.captureSlot=-1;
         // Restore normal cursor
         SetCursor(LoadCursor(nullptr,IDC_ARROW));
         return 0;
      }
      if(wp==VK_ESCAPE){DestroyWindow(dlg);return 0;}
      return 0;}
   case WM_HSCROLL:{
      if((HWND)lp==s_st.hSlider){
         SCROLLINFO si={sizeof(si),SIF_TRACKPOS|SIF_POS|SIF_RANGE};
         GetScrollInfo(s_st.hSlider,SB_CTL,&si);
         int pos=si.nPos;
         switch(LOWORD(wp)){
            case SB_LINELEFT:       pos=std::max(si.nMin,pos-1);  break;
            case SB_LINERIGHT:      pos=std::min(si.nMax,pos+1);  break;
            case SB_PAGELEFT:       pos=std::max(si.nMin,pos-10); break;
            case SB_PAGERIGHT:      pos=std::min(si.nMax,pos+10); break;
            case SB_THUMBTRACK:     pos=si.nTrackPos; break;
            case SB_THUMBPOSITION:  pos=si.nTrackPos; break;
            case SB_TOP:            pos=si.nMin; break;
            case SB_BOTTOM:         pos=si.nMax; break;
         }
         si.fMask=SIF_POS; si.nPos=pos;
         SetScrollInfo(s_st.hSlider,SB_CTL,&si,TRUE);
         g_volume=pos/100.f;
         wchar_t buf[16]; wsprintfW(buf,L"%d%%",pos);
         SetWindowTextW(s_st.hVolLbl,buf);
      } else if((HWND)lp==s_st.hAnimSlider){
         SCROLLINFO si={sizeof(si),SIF_TRACKPOS|SIF_POS|SIF_RANGE};
         GetScrollInfo(s_st.hAnimSlider,SB_CTL,&si);
         int pos=si.nPos;
         switch(LOWORD(wp)){
            case SB_LINELEFT:       pos=std::max(si.nMin,pos-1); break;
            case SB_LINERIGHT:      pos=std::min(si.nMax,pos+1); break;
            case SB_PAGELEFT:       pos=std::max(si.nMin,pos-1); break;
            case SB_PAGERIGHT:      pos=std::min(si.nMax,pos+1); break;
            case SB_THUMBTRACK:     pos=si.nTrackPos; break;
            case SB_THUMBPOSITION:  pos=si.nTrackPos; break;
            case SB_TOP:            pos=si.nMin; break;
            case SB_BOTTOM:         pos=si.nMax; break;
         }
         si.fMask=SIF_POS; si.nPos=pos;
         SetScrollInfo(s_st.hAnimSlider,SB_CTL,&si,TRUE);
         applyAnimSpeedStep(pos-2);
         SetWindowTextW(s_st.hAnimLbl,animSpeedLabel(g_animSpeedStep).c_str());
      }
      return 0;}
   case WM_NOTIFY:{
      NMHDR* hdr=(NMHDR*)lp;
      if(hdr->hwndFrom==s_st.hTab && hdr->code==TCN_SELCHANGE){
         int sel=(int)SendMessageW(s_st.hTab,TCM_GETCURSEL,0,0);
         switchTab(sel);
      }
      return 0;}
   case WM_COMMAND:{
      int id=LOWORD(wp);
      if(id==IDOK){
         s_st.accepted=true;
         g_bgIndex=s_st.bgSel;
         if(g_bgIndex<0||g_bgIndex>=BG_COUNT) g_bgIndex=0;
         bool newFreeMode=(IsDlgButtonChecked(s_st.hGenPage,1007)==BST_CHECKED);
         if(newFreeMode!=g_freeColMode){
            g_freeColMode=newFreeMode;
            if(g_gameStarted && !g_won && g_moveCount>0) newGame();
         }
         g_moveHighlightMode=0;
         for(int i=0;i<3;i++) if(IsDlgButtonChecked(s_st.hGenPage,1020+i)==BST_CHECKED) g_moveHighlightMode=i;
         g_checkUpdatesOnStart=(IsDlgButtonChecked(s_st.hGenPage,1050)==BST_CHECKED);
         {
            wchar_t db[8];
            GetWindowTextW(s_st.hDepthFreeEdit,db,8);
            int nd=_wtoi(db);
            if(nd<1) nd=4; else if(nd>MAX_SEARCH_DEPTH) nd=MAX_SEARCH_DEPTH; // too large: use the maximum, not the default
            if(nd!=g_searchDepthFree){ g_searchDepthFree=nd; invalidateBestMoveCache(); }
            GetWindowTextW(s_st.hDepthKingEdit,db,8);
            nd=_wtoi(db);
            if(nd<1) nd=5; else if(nd>MAX_SEARCH_DEPTH) nd=MAX_SEARCH_DEPTH;
            if(nd!=g_searchDepthKing){ g_searchDepthKing=nd; invalidateBestMoveCache(); }
         }
         // Apply key bindings
         for(int i=0;i<KA_COUNT;i++) g_keys[i]=s_st.tmpKeys[i];
         saveKeyBindings(getIniPath());
         // Apply sound paths / mute state
         for(int i=0;i<SOUND_COUNT;i++){
            SoundSystem::instance().setMuted(i,s_st.tmpMuted[i]);
            if(!s_st.tmpMuted[i]){
               wchar_t buf[MAX_PATH]={};
               GetWindowTextW(s_st.hSoundEdit[i],buf,MAX_PATH);
               std::wstring p(buf);
               s_st.tmpPaths[i]=p;
               SoundSystem::instance().setCustomPath(i,p);
            }
         }
         saveSettings();
         SoundSystem::instance().fadeOutAll(300);
         DestroyWindow(dlg);
      } else if(id==IDCANCEL){
         s_st.accepted=false;
         g_bgIndex    =s_st.tmpBg;
         g_volume     =s_st.tmpVol;
         g_freeColMode=s_st.tmpFreeCol;
         applyAnimSpeedStep(s_st.tmpAnimStep);
         SoundSystem::instance().fadeOutAll(300);
         DestroyWindow(dlg);
      } else if(id>=1010 && id<1010+BG_COUNT){
         // Background color swatch clicked
         s_st.bgSel=id-1010;
         for(int i=0;i<BG_COUNT;i++) InvalidateRect(s_st.hBgSwatch[i],nullptr,TRUE);
      } else {
         // Check Browse buttons (2000..2007) and Play buttons (3000..3007)
         if(id>=2000 && id<2000+SOUND_COUNT){
            int idx=id-2000;
            wchar_t path[MAX_PATH]={};
            GetWindowTextW(s_st.hSoundEdit[idx],path,MAX_PATH);
            OPENFILENAMEW ofn={};
            ofn.lStructSize=sizeof(ofn);
            ofn.hwndOwner=dlg;
            ofn.lpstrFilter=L"Pliki dźwiękowe (*.wav;*.mp3) *.wav;*.mp3 WAV (*.wav) *.wav MP3 (*.mp3) *.mp3 Wszystkie *.* ";
            ofn.lpstrFile=path;
            ofn.nMaxFile=MAX_PATH;
            ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
            // lpstrTitle needs non-const buffer on some Windows versions
            wchar_t titleBuf[64]={};
            wcsncpy_s(titleBuf,64,SOUND_LABELS[idx],_TRUNCATE);
            ofn.lpstrTitle=titleBuf;
            if(GetOpenFileNameW(&ofn)){
               SetWindowTextW(s_st.hSoundEdit[idx],path);
               s_st.tmpMuted[idx]=false;
               EnableWindow(s_st.hSoundEdit[idx],TRUE);
               EnableWindow(s_st.hSoundPlay[idx],TRUE);
            }
         } else if(id>=4000 && id<4000+SOUND_COUNT){
            // "✕" — no sound for this event
            int idx=id-4000;
            s_st.tmpMuted[idx]=true;
            SetWindowTextW(s_st.hSoundEdit[idx],L"(brak d\u017awi\u0119ku)");
            EnableWindow(s_st.hSoundEdit[idx],FALSE);
            EnableWindow(s_st.hSoundPlay[idx],FALSE);
            SoundSystem::instance().fadeOutAll(300);
         } else if(id>=6000 && id<6000+SOUND_COUNT){
            // "↺" — restore default sound
            int idx=id-6000;
            s_st.tmpMuted[idx]=false;
            SetWindowTextW(s_st.hSoundEdit[idx],L"");
            EnableWindow(s_st.hSoundEdit[idx],TRUE);
            EnableWindow(s_st.hSoundPlay[idx],TRUE);
         } else if(id>=3000 && id<3000+SOUND_COUNT){
            int idx=id-3000;
            if(s_st.tmpMuted[idx]) { /* muted — nothing to preview */ }
            else {
            // Cut off any sample already previewing with a quick 0.3s fade
            // before starting the newly-selected one.
            SoundSystem::instance().fadeOutAll(300);
            wchar_t path[MAX_PATH]={};
            GetWindowTextW(s_st.hSoundEdit[idx],path,MAX_PATH);
            SoundSystem::instance().setCustomPath(idx, path[0] ? std::wstring(path) : L"");
            SoundSystem::instance().playIdx(idx,g_volume);
            s_st.previewIdx=idx;
            }
         }
      } // end else
      // Key binding buttons: 5000..5013 (action*2+slot)
      if(id>=5000 && id<5000+KA_COUNT*2){
         int a=(id-5000)/2, sl=(id-5000)%2;
         s_st.captureAction=a; s_st.captureSlot=sl;
         // Change button text to prompt and set focus to dialog for WM_KEYDOWN
         SetWindowTextW(s_st.hKeyBtn[a][sl],L"[naciśnij...]");
         SetFocus(dlg);
      }
      return 0;}
   case WM_DESTROY:{
      PostMessageW(dlg,WM_NULL,0,0);
      return 0;}
   case WM_CLOSE:{
      s_st.accepted=false;
      g_bgIndex=s_st.tmpBg;
      g_volume =s_st.tmpVol;
      applyAnimSpeedStep(s_st.tmpAnimStep);
      SoundSystem::instance().fadeOutAll(300);
      DestroyWindow(dlg);
      return 0;}
   }
   return DefWindowProcW(dlg,msg,wp,lp);
}

static void showSettings(HWND parent){
   HINSTANCE hInst=(HINSTANCE)GetWindowLongPtrW(parent,GWLP_HINSTANCE);
   INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_TAB_CLASSES|ICC_WIN95_CLASSES};
   InitCommonControlsEx(&icc);

   static bool registered=false;
   if(!registered){
      WNDCLASSEXW wc={sizeof(wc)};
      wc.lpfnWndProc  =SettingsWndProc;
      wc.hInstance    =hInst;
      wc.lpszClassName=L"PasjansSettings";
      wc.hCursor      =LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
      wc.style        =CS_HREDRAW|CS_VREDRAW;
      RegisterClassExW(&wc);
      registered=true;
   }

   s_st.tmpBg=g_bgIndex; s_st.tmpVol=g_volume;
   s_st.tmpFreeCol=g_freeColMode; s_st.accepted=false;
   s_st.tmpDepthFree=g_searchDepthFree; s_st.tmpDepthKing=g_searchDepthKing;
   s_st.captureAction=-1; s_st.captureSlot=-1;
   for(int i=0;i<SOUND_COUNT;i++)
      s_st.tmpPaths[i]=SoundSystem::instance().customPath(i);

   // Dialog size: wide enough for sound paths, tall enough for all rows + buttons
   // (+24 vs. before to comfortably fit the Samograj row on the Keys page)
   const int DW=520, DH=535; // client area size (+5 for buttons)
   RECT adjS={0,0,DW,DH};
   AdjustWindowRectEx(&adjS,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
   HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME,L"PasjansSettings",L"Ustawienia",
      WS_POPUP|WS_CAPTION|WS_SYSMENU,0,0,adjS.right-adjS.left,adjS.bottom-adjS.top,parent,nullptr,hInst,nullptr);
   if(!dlg) return;
   // Store dlg in s_st so WM_COMMAND Browse handler can use it
   // (we access it via s_st.hTab's parent)

   HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
   auto mk=[&](HWND par,const wchar_t* cls,const wchar_t* txt,DWORD sty,
               int x,int y,int w,int h,int id)->HWND{
      HWND hw=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|sty,
         x,y,w,h,par,(HMENU)(INT_PTR)id,hInst,nullptr);
      SendMessageW(hw,WM_SETFONT,(WPARAM)hf,TRUE); return hw;};

   // Tab control — leave 40px at bottom for OK/Cancel
   s_st.hTab=mk(dlg,WC_TABCONTROLW,L"",WS_CLIPSIBLINGS,5,5,DW-15,DH-55,900);
   TCITEMW ti={TCIF_TEXT};
   ti.pszText=(LPWSTR)L"Og\u00f3lne";  SendMessageW(s_st.hTab,TCM_INSERTITEMW,0,(LPARAM)&ti);
   ti.pszText=(LPWSTR)L"D\u017awi\u0119ki"; SendMessageW(s_st.hTab,TCM_INSERTITEMW,1,(LPARAM)&ti);
   ti.pszText=(LPWSTR)L"Klawisze"; SendMessageW(s_st.hTab,TCM_INSERTITEMW,2,(LPARAM)&ti);

   const int PX=10,PY=32,PW=DW-30,PH=DH-100;
   s_st.hGenPage=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,PX,PY,PW,PH,dlg,nullptr,hInst,nullptr);
   SetWindowSubclass(s_st.hGenPage,SndPageSubclassProc,1,0);
   s_st.hSndPage=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,PX,PY,PW,PH,dlg,nullptr,hInst,nullptr);
   SetWindowSubclass(s_st.hSndPage,SndPageSubclassProc,1,0);
   s_st.hKeyPage=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,PX,PY,PW,PH,dlg,nullptr,hInst,nullptr);

   // ── General page ────────────────────────────────────────────────────────
   auto mkg=[&](const wchar_t* cls,const wchar_t* txt,DWORD sty,int x,int y,int w,int h,int id)->HWND{
      return mk(s_st.hGenPage,cls,txt,sty,x,y,w,h,id);};
   mkg(L"STATIC",L"Kolor t\u0142a:",SS_LEFT,5,8,95,18,0);
   {
      const int SW=52,SGAP=8,SX=105,SY=6,PER_ROW=5;
      for(int i=0;i<BG_COUNT;i++){
         int row=i/PER_ROW, col=i%PER_ROW;
         s_st.hBgSwatch[i]=mkg(L"BUTTON",L"",BS_OWNERDRAW,
            SX+col*(SW+SGAP),SY+row*(SW+SGAP),SW,SW,1010+i);
      }
   }
   s_st.bgSel=g_bgIndex;
   mkg(L"STATIC",L"Wolne miejsce:",SS_LEFT,5,132,PW-15,18,0);
   s_st.hFreeColK  =mkg(L"BUTTON",L"Tylko kr\u00f3l (standardowe)",BS_AUTORADIOBUTTON|WS_GROUP,15,152,PW-30,18,1006);
   s_st.hFreeColAny=mkg(L"BUTTON",L"Dowolna karta",BS_AUTORADIOBUTTON,15,174,PW-30,18,1007);
   CheckDlgButton(s_st.hGenPage, g_freeColMode?1007:1006, BST_CHECKED);
   mkg(L"STATIC",L"Zaznaczanie sekwencji:",SS_LEFT,5,202,PW-15,18,0);
   const wchar_t* moveHiLabels[3]={
      L"Nie zaznaczaj",
      L"Obrys wok\u00f3\u0142 sekwencji",
      L"Przyciemnij karty, kt\u00f3rych nie mo\u017cna ruszy\u0107"
   };
   for(int i=0;i<3;i++){
      DWORD sty=BS_AUTORADIOBUTTON|(i==0?WS_GROUP:0);
      s_st.hMoveHiRadio[i]=mkg(L"BUTTON",moveHiLabels[i],sty,15,222+i*22,PW-30,18,1020+i);
   }
   CheckDlgButton(s_st.hGenPage, 1020+g_moveHighlightMode, BST_CHECKED);

   // AI look-ahead depth, per mode (base ply count fed into rankCandidates();
   // free mode always adds +4 more on top of this when an empty column is
   // present, king-only mode does not — see game.h). Plain numeric edit boxes,
   // clamped to [1,MAX_SEARCH_DEPTH] on OK to match game.h's own defensive clamp.
   mkg(L"STATIC",L"Głębokość przewidywań SI (dowolna karta):",SS_LEFT,5,296,PW-70-15,18,0);
   s_st.hDepthFreeEdit=mkg(L"EDIT",L"",WS_BORDER|ES_NUMBER,PW-70,294,50,22,1040);
   SendMessageW(s_st.hDepthFreeEdit,EM_SETLIMITTEXT,2,0);
   {wchar_t b[8]; wsprintfW(b,L"%d",s_st.tmpDepthFree); SetWindowTextW(s_st.hDepthFreeEdit,b);}
   mkg(L"STATIC",L"Głębokość przewidywań SI (tylko król):",SS_LEFT,5,322,PW-70-15,18,0);
   s_st.hDepthKingEdit=mkg(L"EDIT",L"",WS_BORDER|ES_NUMBER,PW-70,320,50,22,1041);
   SendMessageW(s_st.hDepthKingEdit,EM_SETLIMITTEXT,2,0);
   {wchar_t b[8]; wsprintfW(b,L"%d",s_st.tmpDepthKing); SetWindowTextW(s_st.hDepthKingEdit,b);}

   // Animation speed slider — the visible label is a child of hGenPage (shown
   // /hidden automatically with the page), but the slider control itself and
   // its live-value label must be children of dlg, not hGenPage, for the same
   // reason the volume slider is (see its comment): a SCROLLBAR's WM_HSCROLL
   // notification goes to its immediate parent, and hGenPage only forwards
   // WM_COMMAND/WM_DRAWITEM, not WM_HSCROLL. So they're shown/hidden manually
   // in switchTab() instead of riding along with hGenPage's own visibility.
   mkg(L"STATIC",L"Prędkość animacji:",SS_LEFT,5,356,PW-15,18,0);
   {
      const int AY=PY+376;
      s_st.tmpAnimStep=g_animSpeedStep;
      // Slider shifted left and its live-value label widened — the label's
      // longest text ("Szybciej (x2,25)") was clipping/wrapping inside the
      // old 70px-wide box (see label position below: old width doubled to
      // 150 and given its own dedicated span instead of overlapping the
      // slider's trailing edge).
      const int animLblW=150;
      const int animLblX=PX+PW-animLblW;
      const int animSliderX=PX+120;
      const int animSliderW=animLblX-10-animSliderX;
      s_st.hAnimSlider=mk(dlg,L"SCROLLBAR",L"",SBS_HORZ,animSliderX,AY,animSliderW,20,1030);
      SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_POS,0,4,0,g_animSpeedStep+2,0};
      SetScrollInfo(s_st.hAnimSlider,SB_CTL,&si,TRUE);
      s_st.hAnimLbl=mk(dlg,L"STATIC",animSpeedLabel(g_animSpeedStep).c_str(),SS_LEFT,animLblX,AY,animLblW,18,1031);
   }
   s_st.hCheckUpdates=mkg(L"BUTTON",L"Sprawdzaj aktualizacje przy starcie",BS_AUTOCHECKBOX,5,398,PW-15,18,1050);
   CheckDlgButton(s_st.hGenPage,1050,g_checkUpdatesOnStart?BST_CHECKED:BST_UNCHECKED);

   // ── Sound page (volume + custom files) ──────────────────────────────────
   auto mks=[&](const wchar_t* cls,const wchar_t* txt,DWORD sty,int x,int y,int w,int h,int id)->HWND{
      return mk(s_st.hSndPage,cls,txt,sty,x,y,w,h,id);};

   // Volume row — slider must be child of dlg (not hSndPage) so WM_HSCROLL reaches SettingsWndProc
   // It's shown/hidden together with hSndPage via switchTab
   const int SVY=PY+4; // y in dlg coords: page top (PY) + row offset (4)
   mk(s_st.hSndPage,L"STATIC",L"G\u0142o\u015bno\u015b\u0107:",SS_LEFT,5,4,90,18,0);
   s_st.hSlider=mk(dlg,L"SCROLLBAR",L"",SBS_HORZ,PX+100,SVY,PW-175,20,1002);
   {SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_POS,0,100,0,(int)(g_volume*100.f),0};
    SetScrollInfo(s_st.hSlider,SB_CTL,&si,TRUE);}
   wchar_t vb[16]; wsprintfW(vb,L"%d%%",(int)(g_volume*100.f));
   s_st.hVolLbl=mk(dlg,L"STATIC",vb,SS_LEFT,PX+PW-70,SVY,60,18,1003);
   // Slider/volLbl are dlg children — hide until Sounds tab is shown
   ShowWindow(s_st.hSlider,SW_HIDE);
   ShowWindow(s_st.hVolLbl,SW_HIDE);

   // Separator
   CreateWindowExW(0,L"STATIC",L"",SS_ETCHEDHORZ|WS_CHILD|WS_VISIBLE,
      4,28,PW-8,2,s_st.hSndPage,nullptr,hInst,nullptr);

   // Sound rows
   mks(L"STATIC",L"Akcja",SS_LEFT,5,36,125,16,0);
   mks(L"STATIC",L"Plik d\u017awi\u0119kowy",SS_LEFT,135,36,200,16,0);

   for(int i=0;i<SOUND_COUNT;i++){
      int row=56+i*29;
      s_st.tmpMuted[i]=SoundSystem::instance().isMuted(i);
      mks(L"STATIC",SOUND_LABELS[i],SS_LEFT|SS_ENDELLIPSIS,5,row+5,129,16,0);
      s_st.hSoundEdit[i]=mks(L"EDIT",
         s_st.tmpMuted[i]?L"(brak d\u017awi\u0119ku)":s_st.tmpPaths[i].c_str(),
         WS_BORDER|ES_AUTOHSCROLL|(s_st.tmpMuted[i]?WS_DISABLED:0),135,row+3,PW-255,20,1100+i);
      s_st.hSoundBrowse[i]=mks(L"BUTTON",L"\u2026",BS_PUSHBUTTON,PW-114,row+2,24,22,2000+i);
      s_st.hSoundPlay[i]  =mks(L"BUTTON",L"\u25b6",BS_PUSHBUTTON|(s_st.tmpMuted[i]?WS_DISABLED:0),PW-86,row+2,24,22,3000+i);
      s_st.hSoundMute[i]  =mks(L"BUTTON",L"\u2715",BS_PUSHBUTTON,PW-58,row+2,24,22,4000+i);
      s_st.hSoundReset[i] =mks(L"BUTTON",L"\u21ba",BS_PUSHBUTTON,PW-30,row+2,24,22,6000+i);
   }

   // ── Tooltips for the "...", "▶" and "✕" buttons on each sound row ────────
   // NOTE: deliberately not using the SDK TOOLINFOW struct here — mirrors the
   // main window's toolbar tooltips, which use a hand-rolled struct because
   // of a cbSize/layout mismatch with this mingw header's TOOLINFOW.
   ensureCustomTipClass(hInst);
   s_st.hTooltip=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
      L"PsjCustomTip",L"",WS_POPUP|WS_BORDER,
      0,0,220,40,dlg,nullptr,hInst,nullptr);
   auto addTip=[&](HWND ctrl,const wchar_t* text){
      SetPropW(ctrl,L"TipText",(HANDLE)text);
      static UINT_PTR relayId=9000;
      SetWindowSubclass(ctrl,TipRelaySubclassProc,relayId++,(DWORD_PTR)s_st.hTooltip);
   };
   for(int i=0;i<SOUND_COUNT;i++){
      addTip(s_st.hSoundBrowse[i],L"Wybierz w\u0142asny plik d\u017awi\u0119kowy (WAV lub MP3)");
      addTip(s_st.hSoundPlay[i],  L"Odtw\u00f3rz pr\u00f3bk\u0119 tego d\u017awi\u0119ku");
      addTip(s_st.hSoundMute[i],  L"Usu\u0144 d\u017awi\u0119k dla tego zdarzenia");
      addTip(s_st.hSoundReset[i], L"Przywr\u00f3\u0107 domy\u015blny d\u017awi\u0119k");
   }

   // ── Keys page ────────────────────────────────────────────────────────────
   auto mkk=[&](const wchar_t* cls,const wchar_t* txt,DWORD sty,int x,int y,int w,int h,int id)->HWND{
      return mk(s_st.hKeyPage,cls,txt,sty,x,y,w,h,id);};
   // Init tmpKeys from current g_keys
   for(int i=0;i<KA_COUNT;i++) s_st.tmpKeys[i]=g_keys[i];
   mkk(L"STATIC",L"Funkcja",SS_LEFT,5,5,140,16,0);
   mkk(L"STATIC",L"Klawisz 1",SS_CENTER,148,5,80,16,0);
   mkk(L"STATIC",L"Klawisz 2",SS_CENTER,232,5,80,16,0);
   for(int i=0;i<KA_COUNT;i++){
      int row=26+i*30;
      mkk(L"STATIC",KA_LABELS[i],SS_LEFT|SS_ENDELLIPSIS,5,row+6,140,18,0);
      std::wstring n1=bindingName(s_st.tmpKeys[i].key1, s_st.tmpKeys[i].shift);
      std::wstring n2=bindingName(s_st.tmpKeys[i].key2, s_st.tmpKeys[i].shift);
      s_st.hKeyBtn[i][0]=mkk(L"BUTTON",n1.c_str(),BS_PUSHBUTTON,148,row+2,80,24,5000+i*2);
      s_st.hKeyBtn[i][1]=mkk(L"BUTTON",n2.c_str(),BS_PUSHBUTTON,232,row+2,80,24,5001+i*2);
   }
   // Subclass hKeyPage too so WM_COMMAND from its buttons reaches dlg
   SetWindowSubclass(s_st.hKeyPage,SndPageSubclassProc,1,0);

   // ── OK / Cancel — anchored to dialog bottom ──────────────────────────────
   mk(dlg,L"BUTTON",L"OK",     BS_DEFPUSHBUTTON,DW/2-105,DH-40,95,28,IDOK);
   mk(dlg,L"BUTTON",L"Anuluj", BS_PUSHBUTTON,   DW/2+15, DH-40,95,28,IDCANCEL);

   switchTab(0);

   // Centre on parent
   RECT pr; GetWindowRect(parent,&pr);
   SetWindowPos(dlg,HWND_TOP,
      pr.left+(pr.right-pr.left-(adjS.right-adjS.left))/2,
      pr.top +(pr.bottom-pr.top-(adjS.bottom-adjS.top))/2,
      0,0,SWP_NOSIZE|SWP_SHOWWINDOW);

   EnableWindow(parent,FALSE);
   MSG m;
   while(IsWindow(dlg)&&GetMessage(&m,nullptr,0,0)){
      if(m.message==WM_QUIT){PostQuitMessage((int)m.wParam);break;}
      if(!IsDialogMessage(dlg,&m)){TranslateMessage(&m);DispatchMessage(&m);}
   }
   EnableWindow(parent,TRUE);
   SetForegroundWindow(parent);
   markDirty(); invalidateGame();
}


// Execute the currently suggested hint move automatically
static void rebuildBoardFull(int,int){}

// Outcome of a doAutoMove() call — lets callers (in particular the
// "Samograj" self-play loop) tell a real move apart from "still thinking"
// or "nothing left to do", instead of just a bare success/failure bit.
enum AutoMoveOutcome {
   AM_MOVED,    // a move (or reserve deal) was performed
   AM_PENDING,  // no cached best move yet — now waiting on the background search
   AM_BLOCKED,  // a deal/animation is already in flight; try again once it settles
   AM_NONE      // no valid move found — the deal is finished (win or stuck)
};

// ── AI plan continuity ("buffer move" follow-through) ───────────────────────
// Per spec: once the automatic move plays a "move to a free column" or a
// "move a foundation card back to the table" move, the very NEXT automatic
// move must NOT recompute from scratch — it must play whatever move the
// engine had already planned as the follow-up when it originally searched
// this far ahead (getBestMove()'s outPlan). g_aiPlan holds that remaining
// planned line (not including whatever move was just played); it is only
// ever trusted immediately after a qualifying move, and any manual action
// (cancelSamograj() is called from all of them — see its own comment)
// invalidates it so a stale plan can never be replayed into an unrelated
// board.
static std::vector<MoveHint> g_aiPlan;
static bool                  g_aiPlanValid = false;
static void invalidateAiPlan(){ g_aiPlan.clear(); g_aiPlanValid=false; }

// A "buffer" move per spec: relocating cards to a free column, or moving a
// card from a foundation back onto the table. Must be evaluated against the
// board as it stood BEFORE the move (destination emptiness in particular).
// The "free column" half only counts in free-column mode (user-requested):
// in king-only mode, a card landing on an empty column is always a King
// completing one specific, deliberate relocation — not free mode's
// general-purpose "stash it somewhere free" tactic — so there's no basis
// for assuming the engine's old plan for what comes next still holds; the
// next automatic move just recomputes fresh instead.
static bool isBufferMove(const MoveHint& h){
   if(h.fromType==LOC_FOUNDATION && h.toType==LOC_COLUMN) return true;
   if(g_freeColMode && h.fromType==LOC_COLUMN && h.toType==LOC_COLUMN &&
      h.toIdx>=0 && h.toIdx<NUM_COLS && g_game.cols[h.toIdx].empty()) return true;
   return false;
}

AutoMoveOutcome doAutoMove(){
   if(g_dealing || g_deal.busy() || g_animating) return AM_BLOCKED;

   if(g_movesSinceFoundationGain >= FOUNDATION_STALL_LIMIT){
      playSound("nono",g_volume);
      clearAutoHistory();
      invalidateAiPlan();
      return AM_NONE;
   }

   MoveHint h;
   bool usedPlan = (g_aiPlanValid && !g_aiPlan.empty());
   if(usedPlan){
      h = g_aiPlan.front();
      bool ok = h.valid;
      if(ok){
         if(h.fromType==LOC_COLUMN)
            ok = h.fromIdx>=0&&h.fromIdx<NUM_COLS&&h.fromCard>=0&&h.fromCard<(int)g_game.cols[h.fromIdx].size();
         else
            ok = h.fromIdx>=0&&h.fromIdx<NUM_FOUND&&!g_game.found[h.fromIdx].empty();
      }
      if(ok && h.toType==LOC_COLUMN) ok = h.toIdx>=0&&h.toIdx<NUM_COLS;
      // A stale/invalid stored plan (should not normally happen — anything
      // that could invalidate it also cancels Samograj and clears it) falls
      // back to a fresh search this same call rather than reporting failure.
      if(!ok){ invalidateAiPlan(); usedPlan=false; }
   }

   if(!usedPlan){
      if(!isBestMoveReady()){ startThinking(PA_AUTOMOVE); return AM_PENDING; }

      // Encode source/destination uniformly: real columns as 0..9, foundations
      // as -10-f (same scheme used elsewhere for foundation-sourced
      // candidates), so a move and its exact reverse always produce matching
      // keys regardless of which side is a column and which is a foundation.
      // A foundation→column "buffer" move CAN be undone by an ordinary
      // column→foundation move right back, so this check must never be
      // skipped just because a foundation is involved.
      auto isFreshMove = [&](const MoveHint& cand) -> bool {
         Card movedCard; int encFrom, encTo;
         if(cand.fromType==LOC_COLUMN){
            if(cand.fromIdx<0||cand.fromIdx>=NUM_COLS||cand.fromCard<0||cand.fromCard>=(int)g_game.cols[cand.fromIdx].size()) return false;
            movedCard=g_game.cols[cand.fromIdx][cand.fromCard];
            encFrom=cand.fromIdx;
         } else {
            if(cand.fromIdx<0||cand.fromIdx>=NUM_FOUND||g_game.found[cand.fromIdx].empty()) return false;
            movedCard=g_game.found[cand.fromIdx].back();
            encFrom=-10-cand.fromIdx;
         }
         encTo=(cand.toType==LOC_COLUMN)?cand.toIdx:-10-cand.toIdx;

         int cid=cardCode(movedCard);
         int key=encodeAutoMove(cid,encFrom,encTo);
         int revKey=encodeAutoMove(cid,encTo,encFrom);
         if(autoMoveInHistory(key) || autoMoveInHistory(revKey)) return false;
         // Also reject a move whose RESULTING board — not just the move
         // itself — repeats something reached within the last 20 automatic
         // moves. Catches longer cycles the single-move check can't (see
         // boardStateInHistory()'s comment).
         GameState sim = g_game.boardOnly();
         sim.applyHint(cand);
         return !boardStateInHistory(boardCanonicalHash(sim));
      };

      // One full search for the current position (cached whenever the
      // background precompute thread already finished it), then a cheap walk
      // down the pre-ranked, pre-searched beam applying the history checks —
      // instead of paying for a brand-new full search on every excluded
      // candidate the way a naive retry loop would.
      std::vector<RankedMove> ranked = getRankedMovesFast();
      int chosen = -1;
      for(size_t i=0;i<ranked.size();i++){
         const MoveHint& cand = ranked[i].move;
         if(cand.fromIdx==-1){ chosen=(int)i; break; } // reserve deal — always fine, no reversal concept applies
         if(isFreshMove(cand)){ chosen=(int)i; break; }
      }

      if(chosen>=0){
         h = ranked[chosen].move;
         g_aiPlan = ranked[chosen].pv;
      } else {
         // Rare: every ranked candidate conflicts with recent history. Fall
         // back to the exhaustive engine-level exclusion path — this pays for
         // fresh searches again, but only in a case the fast path's (already
         // generous) beam genuinely couldn't cover.
         std::set<std::pair<int,int>> triedMoves;
         for(auto& rm : ranked) triedMoves.insert(moveExclusionKey(rm.move));
         h.valid=false;
         for(int attempt=0;attempt<30;attempt++){
            int score=0; std::vector<MoveHint> plan;
            MoveHint cand = g_game.getBestMove(triedMoves,&score,&plan);
            if(!cand.valid) break;
            // getBestMove's outPlan includes `cand` itself as its first
            // element — drop it so g_aiPlan keeps meaning "continuation not
            // including the move just chosen", same as everywhere else here.
            if(!plan.empty()) plan.erase(plan.begin());
            if(cand.fromIdx==-1){ h=cand; g_aiPlan=plan; break; }
            if(isFreshMove(cand)){ h=cand; g_aiPlan=plan; break; }
            triedMoves.insert(moveExclusionKey(cand));
         }
      }

      if(!h.valid){
         // Last resort before giving up: every candidate was refused by the
         // anti-loop rules - most often because the ONLY remaining move is the
         // exact reverse of a recent one (typically putting a card back on a
         // foundation after a buffer move that did not pay off). The move-level
         // "reverse" rule is only a shortcut for the whole-board check, so when
         // nothing else is left accept any candidate whose RESULTING board has
         // not occurred in the last BOARD_HISTORY_SIZE states. (A longer cycle
         // is still caught by the foundation stall limit.)
         auto boardFresh=[&](const MoveHint& cand)->bool{
            GameState sim=g_game.boardOnly(); sim.applyHint(cand);
            return !boardStateInHistory(boardCanonicalHash(sim));
         };
         for(size_t i=0;i<ranked.size() && !h.valid;i++){
            const MoveHint& cand=ranked[i].move;
            if(cand.fromIdx==-1) continue;
            if(boardFresh(cand)){ h=cand; g_aiPlan=ranked[i].pv; }
         }
         if(!h.valid){
            std::set<std::pair<int,int>> triedRelaxed;
            for(auto& rm : ranked) triedRelaxed.insert(moveExclusionKey(rm.move));
            for(int attempt=0;attempt<30;attempt++){
               int score=0; std::vector<MoveHint> plan;
               MoveHint cand = g_game.getBestMove(triedRelaxed,&score,&plan);
               if(!cand.valid) break;
               if(!plan.empty()) plan.erase(plan.begin());
               if(cand.fromIdx!=-1 && boardFresh(cand)){ h=cand; g_aiPlan=plan; break; }
               triedRelaxed.insert(moveExclusionKey(cand));
            }
         }
      }
      if(!h.valid){
         playSound("nono",g_volume);
         clearAutoHistory();
         invalidateAiPlan();
         return AM_NONE;
      }
   }

   if(h.fromIdx==-1){ clearAutoHistory(); invalidateAiPlan(); noteFoundationProgress(); doDeal(); return AM_MOVED; }

   bool isBuffer = isBufferMove(h);

   if(h.fromType==LOC_COLUMN && h.toType==LOC_FOUNDATION){
      int col=h.fromIdx, ci=h.fromCard;
      if(col<0||col>=NUM_COLS||ci<0||ci>=(int)g_game.cols[col].size()){ invalidateAiPlan(); return AM_NONE; }
      // Defense in depth: same guarantee as commitToFound in doClickMove —
      // a foundation move can only ever be the single bottom-most card.
      if(ci!=(int)g_game.cols[col].size()-1){ invalidateAiPlan(); return AM_NONE; }
      Card card=g_game.cols[col][ci];
      int f=g_game.bestFoundSlot(card);
      if(f<0){ invalidateAiPlan(); return AM_NONE; }
      // Capture positions before commit
      POINT src=cardPixelPos(col,ci);
      POINT dst=g_layout.foundPos(f);
      float ov=g_colDispOv[col];
      std::vector<Card> moving={card};
      pushAutoHistory(encodeAutoMove(cardCode(card),col,-10-f));
      g_game.saveUndo();
      updateRealArtificialSince(h);
      g_game.found[f].push_back(card);
      g_game.cols[col].erase(g_game.cols[col].begin()+ci,g_game.cols[col].end());
      pushBoardStateHistory(boardCanonicalHash(g_game));
      noteFoundationProgress();
      g_hideDstFound=f;
      if(!g_winCounted){g_moveCount++;} if(!g_winCounted) playSound("click",g_volume); markDirty(); onStateChanged();
      startCardAnim(moving,(float)src.x,(float)src.y,
                    (float)dst.x,(float)dst.y, ov, true);
      if(usedPlan) g_aiPlan.erase(g_aiPlan.begin());
      g_aiPlanValid = isBuffer && !g_aiPlan.empty();
      return AM_MOVED;
   }
   if(h.fromType==LOC_COLUMN && h.toType==LOC_COLUMN){      int fc=h.fromIdx, ci=h.fromCard, tc=h.toIdx;
      if(fc<0||fc>=NUM_COLS||ci<0||ci>=(int)g_game.cols[fc].size()){ invalidateAiPlan(); return AM_NONE; }
      if(tc<0||tc>=NUM_COLS){ invalidateAiPlan(); return AM_NONE; }
      std::vector<Card> moving(g_game.cols[fc].begin()+ci,g_game.cols[fc].end());
      if(moving.empty()||!g_game.canDropOnCol(moving[0],tc)){ invalidateAiPlan(); return AM_NONE; }
      POINT src=cardPixelPos(fc,ci);
      int dstIdx=(int)g_game.cols[tc].size();
      float ov=g_colDispOv[fc];
      pushAutoHistory(encodeAutoMove(cardCode(moving[0]),fc,tc));
      g_game.saveUndo();
      updateRealArtificialSince(h);
      for(auto& c:moving) g_game.cols[tc].push_back(c);
      g_game.cols[fc].erase(g_game.cols[fc].begin()+ci,g_game.cols[fc].end());
      pushBoardStateHistory(boardCanonicalHash(g_game));
      noteFoundationProgress();
      g_hideDstCol=tc; g_hideDstFromIdx=dstIdx;
      if(!g_winCounted){g_moveCount++;} if(!g_winCounted) playSound("click",g_volume); markDirty(); onStateChanged();
      g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
      float dstY=landingY(tc,dstIdx);
      float dstOv=landingOv(tc);
      float dstX=(float)g_layout.colPos(tc).x;
      startCardAnim(moving,(float)src.x,(float)src.y, dstX,dstY, ov,false);
      if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv;
      if(usedPlan) g_aiPlan.erase(g_aiPlan.begin());
      g_aiPlanValid = isBuffer && !g_aiPlan.empty();
      return AM_MOVED;
   }
   if(h.fromType==LOC_FOUNDATION && h.toType==LOC_COLUMN){
      int f=h.fromIdx, tc=h.toIdx;
      if(f<0||f>=NUM_FOUND||g_game.found[f].empty()){ invalidateAiPlan(); return AM_NONE; }
      if(tc<0||tc>=NUM_COLS){ invalidateAiPlan(); return AM_NONE; }
      Card card=g_game.found[f].back();
      if(!g_game.canDropOnCol(card,tc)){ invalidateAiPlan(); return AM_NONE; }
      POINT src=g_layout.foundPos(f);
      int dstIdx=(int)g_game.cols[tc].size();
      pushAutoHistory(encodeAutoMove(cardCode(card),-10-f,tc));
      g_game.saveUndo();
      updateRealArtificialSince(h);
      g_game.cols[tc].push_back(card);
      g_game.found[f].pop_back();
      pushBoardStateHistory(boardCanonicalHash(g_game));
      noteFoundationProgress();
      g_hideDstCol=tc; g_hideDstFromIdx=dstIdx;
      if(!g_winCounted){g_moveCount++;} if(!g_winCounted) playSound("click",g_volume); markDirty(); onStateChanged();
      g_colDispOv[tc]=g_colSrcOv[tc]=g_colTgtOv[tc]; g_colAnimStart[tc]=0;
      float dstY2=landingY(tc,dstIdx);
      float dstOv2=landingOv(tc);
      float dstX2=(float)g_layout.colPos(tc).x;
      startCardAnim({card},(float)src.x,(float)src.y, dstX2,dstY2, 0.f,false);
      if(!g_cardAnims.empty()) g_cardAnims.back().dstOv=dstOv2;
      if(usedPlan) g_aiPlan.erase(g_aiPlan.begin());
      g_aiPlanValid = isBuffer && !g_aiPlan.empty();
      return AM_MOVED;
   }
   invalidateAiPlan();
   return AM_NONE;
}

// ── "Samograj" (self-play) ──────────────────────────────────────────────────
// Toggle-button driven auto-play: while active, repeatedly performs the
// engine's best move — exactly like pressing the auto-move key by hand, over
// and over — until the deal is won or genuinely stuck, or the player takes
// the board back manually. (g_samogranoActive itself is declared earlier,
// near g_thinking, so newGame()/loadGameFrom() can reach it.)
static void updateSamogranoButton(){
   if(g_hwnd) InvalidateRect(GetDlgItem(g_hwnd,ID_SAMOGRAJ),nullptr,FALSE);
   updateAutoStatsUI(g_samogranoActive);
}

// Any manual interaction — a card click or another toolbar/menu action —
// hands control back to the player.
static void cancelSamograj(){
   invalidateAiPlan(); // any manual interaction invalidates a pending planned continuation too
   if(!g_samogranoActive) return;
   g_samogranoActive=false;
   g_samogranoMarathon=false;
   updateSamogranoButton();
}

// Adds the deal that just ended to the auto-play scoreboard (once per deal).
// `forceEnd`: the engine itself reported "nothing left to play" (stuck).
static void autoNoteDealEnd(bool forceEnd){
   if(g_autoDealCounted || !(g_won||g_noMoves||forceEnd)) return;
   g_autoDealCounted=true;
   g_autoPlayed++;
   if(g_won) g_autoWon++;
   else if(forceEnd && !g_noMoves) recordLostDeal(g_currentGameNumber,(int)g_freeColMode); // stuck (a "no moves" loss is recorded where it is detected)
   updateAutoStatsUI(g_samogranoActive);
}

static void toggleSamograj(){
   g_samogranoActive=!g_samogranoActive;
   if(g_samogranoActive) resetAutoStats();
   if(!g_samogranoActive) g_samogranoMarathon=false; // stopping self-play always stops the marathon too
   updateSamogranoButton();
}

// Shift+1: same as Samograj, but once a deal is won or stuck it doesn't
// stop - it silently deals the next one and keeps going. Toggled off the
// same way (press again, or any manual interaction via cancelSamograj()).
static void toggleMarathon(){
   if(!g_samogranoActive && !g_samogranoMarathon) resetAutoStats();
   g_samogranoMarathon=!g_samogranoMarathon;
   g_samogranoActive=g_samogranoMarathon;
   updateSamogranoButton();
}

// Called every tick of the main message loop while Samograj is active. A
// no-op whenever a deal/animation is already in flight or the engine is
// still thinking — the loop will simply call again on the next tick.
static void samogranoStep(){
   if(!g_samogranoActive) return;
   if(g_dealing || g_deal.busy() || g_animating || g_thinking) return;
   autoNoteDealEnd(false);
   if(g_samogranoMarathon && (g_won || g_noMoves || g_autoDealStuck)){
      // Previous deal just concluded (win, or genuinely stuck) - the
      // fireworks/dialog/sound for that were already suppressed in
      // onStateChanged() while the marathon is running. Move straight on
      // to a fresh deal (counted normally in stats) and keep playing.
      newGame(false,-1,false,true);
      return;
   }
   AutoMoveOutcome r=doAutoMove();
   autoNoteDealEnd(r==AM_NONE);
   // Stop on the engine's own "nothing left to do" signal (stuck, or the
   // anti-loop stall limit), AND also explicitly on g_won/g_noMoves —
   // checkWin() only requires all 8 kings to be in a winning position (on a
   // foundation, or heading a complete in-column sequence), which can still
   // leave technically-legal moves on the table (e.g. reshuffling a
   // completed sequence) that doAutoMove() would happily keep playing
   // forever without ever returning AM_NONE. Both flags are updated
   // synchronously by onStateChanged() inside the doAutoMove() call above,
   // so this catches a just-played winning (or loss-confirming) move on the
   // very same tick, with no extra pointless move played afterwards.
   //
   // While the marathon is running, a win or a genuine no-moves here is NOT
   // the end — it just means this particular deal is done; the branch at
   // the top of this function deals the next one on the very next tick. So
   // don't stop for those two cases, only for the engine's own "stuck, no
   // sensible move at all" signal (AM_NONE without either of those), which
   // the marathon has no way to recover from on its own.
   if(g_samogranoMarathon){
      if(r==AM_NONE && !g_won && !g_noMoves){
         // The engine has nothing (fresh) left to play although the game still
         // reports a legal move - in practice a dead deal where only
         // reversible shuffles remain. The marathon used to stop dead here;
         // now the deal is written off as lost and the next one is dealt.
         // (A deal that never got a single move in would loop forever, so
         // that case still stops.)
         if(g_moveCount>0) g_autoDealStuck=true; else cancelSamograj();
      }
   } else {
      if(r==AM_NONE || g_won || g_noMoves) cancelSamograj();
   }
}

// Builds/rotates the hint list and starts its preview animation — the part
// of the old ID_HINT handler that actually needs a ready best-move answer.
// Defined just after WndProc (it uses several things declared in between);
// forward-declared here so the thinking-indicator poller below, and
// WndProc's ID_HINT case, can call it once a best-move answer is ready.
static void performHintNow();

// Polled every tick of the main message loop while g_thinking is true: once
// the background best-move search catches up, carries out whichever action
// (Hint or Auto-move) was waiting on it. Until then it's a no-op — the main
// loop's own repaint call is what makes the "Myślę" pulse animate smoothly.
static void tickThinking(){
   if(!g_thinking) return;
   if(!isBestMoveReady()) return;
   g_thinking=false;
   PendingAction act=g_pendingAction; g_pendingAction=PA_NONE;
   if(act==PA_HINT){
      performHintNow();
   } else if(act==PA_AUTOMOVE){
      // If this deferred auto-move was Samograj's, and it turns out there's
      // nothing left to play, stop the toggle right away instead of leaving
      // samogranoStep() to notice (and retry the same dead end) next tick.
      AutoMoveOutcome r=doAutoMove();
      autoNoteDealEnd(r==AM_NONE);
      if(r==AM_NONE) cancelSamograj();
   }
}
// ============================================================================
// Button icon images (PNG, baked into the exe — see app.rc / loadButtonIcons)
// ============================================================================
static const int BTN_ICON_COUNT = 8;
static Bitmap* g_btnIcons[BTN_ICON_COUNT] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
// Order: 0=new, 1=hint, 2=undo, 3=redo, 4=stats, 5=settings, 6=samograj, 7=solver
static const int BTN_ICON_IDS[] = {ID_NEW,ID_HINT,ID_UNDO,ID_REDO,ID_STATS,ID_SETTINGS,ID_SAMOGRAJ,ID_SOLVER};

// Resource names (app.rc, RCDATA) — same order as BTN_ICON_IDS above.
static const wchar_t* BTN_ICON_RES[] = {L"IMG_NEW",L"IMG_HINT",L"IMG_UNDO",L"IMG_REDO",L"IMG_STATY",L"IMG_USTAWIENIA",L"IMG_SAMOGRAJ",L"IMG_SOLVER"};

// Decodes an embedded PNG into a self-contained Bitmap (a deep copy, so the
// backing memory/stream can be released right away).
static Bitmap* loadEmbeddedPng(HINSTANCE hInst,const wchar_t* resName){
   HRSRC hr=FindResourceW(hInst,resName,RT_RCDATA);
   if(!hr) return nullptr;
   HGLOBAL hg=LoadResource(hInst,hr);
   DWORD sz=SizeofResource(hInst,hr);
   const void* p=hg?LockResource(hg):nullptr;
   if(!p||!sz) return nullptr;
   HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,sz);
   if(!mem) return nullptr;
   memcpy(GlobalLock(mem),p,sz); GlobalUnlock(mem);
   IStream* stream=nullptr;
   if(FAILED(CreateStreamOnHGlobal(mem,TRUE,&stream))){ GlobalFree(mem); return nullptr; }
   Bitmap* result=nullptr;
   Bitmap* src=Bitmap::FromStream(stream);
   if(src&&src->GetLastStatus()==Ok)
      result=src->Clone(0,0,(INT)src->GetWidth(),(INT)src->GetHeight(),PixelFormat32bppARGB);
   delete src;
   stream->Release(); // also frees `mem` (fDeleteOnRelease)
   if(result&&result->GetLastStatus()!=Ok){ delete result; result=nullptr; }
   return result;
}

// Toolbar button icons are baked into the exe (app.rc, RCDATA) and ONLY ever
// come from there — deliberately no on-disk override/lookup (a user request:
// the game must not go looking for icon files on disk at all).
static void loadButtonIcons(HINSTANCE hInst){
   for(int i=0;i<BTN_ICON_COUNT;i++){
      delete g_btnIcons[i];
      g_btnIcons[i]=loadEmbeddedPng(hInst,BTN_ICON_RES[i]);
   }
}

static void drawIconButton(DRAWITEMSTRUCT* dis){
   int idx=-1;
   for(int i=0;i<BTN_ICON_COUNT;i++) if(BTN_ICON_IDS[i]==(int)dis->CtlID){idx=i;break;}
   HDC hdc=dis->hDC;
   RECT& rc=dis->rcItem;
   // "Samograj" is a switch button: stays visually pressed for as long as
   // self-play is active, not just while the mouse button is down on it.
   bool pressed=(dis->itemState&ODS_SELECTED)!=0;
   if((int)dis->CtlID==ID_SAMOGRAJ && g_samogranoActive) pressed=true;
   // Background
   FillRect(hdc,&rc,(HBRUSH)(COLOR_BTNFACE+1));
   if(pressed){
      DrawEdge(hdc,&rc,EDGE_SUNKEN,BF_RECT);
   } else {
      DrawEdge(hdc,&rc,EDGE_RAISED,BF_RECT);
   }
   // Icon — kept square and centred even though the button itself is wider
   // than it is tall, so a wider button doesn't stretch the artwork.
   if(idx>=0 && g_btnIcons[idx]){
      int off=pressed?2:1;
      int availW=rc.right-rc.left-8, availH=rc.bottom-rc.top-8;
      int sz=std::min(availW,availH);
      int ix=rc.left+(rc.right-rc.left-sz)/2+off;
      int iy=rc.top+(rc.bottom-rc.top-sz)/2+off;
      Graphics g(hdc);
      g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      g.DrawImage(g_btnIcons[idx],ix,iy,sz,sz);
   } else {
      // Fallback: text if icon missing
      const wchar_t* labels[]={L"New",L"Hint",L"Undo",L"Redo",L"Stats",L"Sett",L"Samo"};
      if(idx>=0 && idx<BTN_ICON_COUNT){
         SetBkMode(hdc,TRANSPARENT);
         DrawTextW(hdc,labels[idx],-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      }
   }
   // Focus rect
   if(dis->itemState&ODS_FOCUS){
      RECT fr={rc.left+3,rc.top+3,rc.right-3,rc.bottom-3};
      DrawFocusRect(hdc,&fr);
   }
}

// ============================================================================
// Called from PeekMessage loop every ~8ms when g_smoothActive is true.
// Returns true if animation is still running.
static bool tickSmoothAnim(){
   DWORD now=timeGetTime();
   bool anyMoving=false;
   for(int col=0;col<NUM_COLS;col++){
      float diff=g_colTgtOv[col]-g_colSrcOv[col];
      if(std::abs(diff)<0.5f){
         g_colDispOv[col]=g_colTgtOv[col];
         continue;
      }
      float elapsed=(float)(now-g_colAnimStart[col]);
      float t=elapsed/(float)ANIM_DURATION_MS;
      if(t>=1.f){
         g_colDispOv[col]=g_colTgtOv[col];
         g_colSrcOv[col]=g_colTgtOv[col];
      } else {
         // Smooth-step ease-in-out: t²(3-2t)
         float s=t*t*(3.f-2.f*t);
         g_colDispOv[col]=g_colSrcOv[col]+diff*s;
         anyMoving=true;
      }
   }
   if(anyMoving){
      // Only mark columns whose overlap actually changed — not the whole board.
      // Background (felt, stosy, rezerwa) doesn't change during overlap animation.
      for(int col=0;col<NUM_COLS;col++){
         if(std::abs(g_colDispOv[col]-g_colTgtOv[col])>0.01f||
            std::abs(g_colSrcOv[col]-g_colTgtOv[col])>0.01f)
            g_colDirty[col]=true;
      }
      invalidateGame();
      UpdateWindow(g_gameHwnd?g_gameHwnd:g_hwnd);
   }
   return anyMoving;
}

// Tick card flight animations. Returns true if any still running.
static bool tickCardAnims(){
   if(g_cardAnims.empty()){ g_animating=false; g_previewAnimating=false; return false; }
   bool anyReal=false, anyPreview=false;
   std::vector<CardAnim> next;
   for(auto& a:g_cardAnims){
      if(a.isPreview){
         if(a.phase==0 && a.flightDone()){
            a.phase=1;
            a.startTime=timeGetTime();
         }
         if(a.phase==1){
            if(timeGetTime()-a.startTime>=(DWORD)((float)CARD_ANIM_PAUSE_MS*effectiveAnimMul()))
               a.phase=2;
         }
      }
      if(!a.done()){
         if(a.isPreview) anyPreview=true; else anyReal=true;
         next.push_back(a);
      } else if(!a.isPreview){
         // Animation just finished: immediately reveal the hidden card on board.
         // Find which column this animation was targeting by matching X position.
         for(int _c=0;_c<NUM_COLS;_c++){
            if(g_hideDealCol[_c]<0) continue;
            if(std::abs(a.ex-(float)g_layout.colPos(_c).x)<4.f){
               g_hideDealCol[_c]=-1;
               break;
            }
         }
         // Also clear hideDstCol if this was a regular col→col move
         if(g_hideDstCol>=0 && std::abs(a.ex-(float)g_layout.colPos(g_hideDstCol).x)<4.f)
            g_hideDstCol=-1;
      }
   }
   bool wasAnimating = g_animating || g_previewAnimating;
   g_cardAnims=next;
   g_animating=anyReal;
   g_previewAnimating=anyPreview;
   bool anyActive=anyReal||anyPreview;

   if(!anyActive){
      // All done: ensure all hidden cards are restored
      g_hideCol=-1; g_hideFromIdx=-1;
      g_hideDstCol=-1; g_hideDstFromIdx=-1;
      g_hideDstFound=-1;
      for(int _i=0;_i<NUM_COLS;_i++) g_hideDealCol[_i]=-1;
      // Show deferred congratulations if any
      if(!g_congratsMsg.empty()){
         std::wstring msg=g_congratsMsg; g_congratsMsg.clear();
         invalidateGame(); UpdateWindow(g_gameHwnd?g_gameHwnd:g_hwnd);
         MessageBoxW(g_hwnd,msg.c_str(),L"Gratulacje!",MB_OK|MB_ICONINFORMATION);
      }
   }

   if(anyActive || wasAnimating){
      invalidateGame();
      UpdateWindow(g_gameHwnd?g_gameHwnd:g_hwnd);
   }
   return anyActive;
}

// ============================================================================
// Solver (toolbar button "Solver")
// ============================================================================
// Flow: pick one or more deals from LostNumbers.csv -> the first one is loaded
// on the table and the player is asked whether to search for a solution -> a
// progress window runs solver.h on every picked deal in turn, in memory only
// (no animation, the table is not touched). A solved deal is written to
// Solved<number>.dat (start position + every move as a Redo step), moved from
// LostNumbers.csv to WonNumbers.csv; a deal that resists 5-minute stages until
// the player declines to continue (or does not answer within 10 s) is moved to
// Unsolvable.csv.
static const ULONGLONG SOLVER_STAGE_MS = 5ULL*60ULL*1000ULL; // time limit of one search stage
static const int       SOLVER_ASK_SECONDS = 10;              // countdown of the "continue?" question
// Absolute safety cap on one deal, summed across every stage AND every resume
// (see g_sv.gameStart) — the search itself never runs out of moves to try on
// its own (a full 104-card deal's search space is far too big to ever
// genuinely exhaust; searchDeal() always stops because of a time/node budget,
// never because it ran out of things to attempt), and the default answer to
// "keep searching?" is "yes" (see SolverAsk), so without this cap an
// unattended deal — nobody there to click "Nie, następne" — would search
// forever, one 5-minute stage after another. At the cap it is given up on
// automatically, exactly like an explicit "no": moved to Unsolvable.csv, its
// SolverState.csv progress kept (so a deliberate later retry still resumes
// past this point rather than restarting), and the batch moves on to the
// next picked deal — with a distinct log line so it reads differently from a
// real "nie znaleziono rozwiązania".
static const ULONGLONG SOLVER_ABSOLUTE_MAX_MS = 2ULL*60ULL*60ULL*1000ULL; // 2h

static HFONT solverFont(bool bold){
   static HFONT f[2]={nullptr,nullptr};
   if(!f[bold?1:0]){
      HDC dc=GetDC(nullptr);
      f[bold?1:0]=CreateFontW(-MulDiv(9,GetDeviceCaps(dc,LOGPIXELSY),72),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,
         FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
         DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
      ReleaseDC(nullptr,dc);
   }
   return f[bold?1:0];
}
static std::wstring solverNum(long long v){ // 1234567 -> "1 234 567"
   std::wstring s=std::to_wstring(v);
   for(int i=(int)s.size()-3;i>0;i-=3) s.insert((size_t)i,L" ");
   return s;
}
static const wchar_t* solverModeName(int mode){ return mode==0?L"Tylko król":L"Dowolna karta"; }

static void solverCenterOn(HWND dlg,HWND parent){
   RECT rp,rd; GetWindowRect(parent,&rp); GetWindowRect(dlg,&rd);
   int x=rp.left+((rp.right-rp.left)-(rd.right-rd.left))/2, y=rp.top+((rp.bottom-rp.top)-(rd.bottom-rd.top))/2;
   SetWindowPos(dlg,nullptr,x<0?0:x,y<0?0:y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
static void solverModalLoop(HWND dlg,HWND parent){
   EnableWindow(parent,FALSE);
   ShowWindow(dlg,SW_SHOW);
   MSG m;
   while(IsWindow(dlg)&&GetMessage(&m,nullptr,0,0)){
      if(m.message==WM_QUIT){ PostQuitMessage((int)m.wParam); break; }
      if(!IsDialogMessage(dlg,&m)){ TranslateMessage(&m); DispatchMessage(&m); }
   }
   EnableWindow(parent,TRUE);
   SetForegroundWindow(parent);
}
static HWND solverChild(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id,bool bold=false){
   HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
   SendMessageW(c,WM_SETFONT,(WPARAM)solverFont(bold),TRUE);
   return c;
}

// ── 1. picking deals ─────────────────────────────────────────────────────────
// The list shows the deals of LostNumbers.csv and, in red, those already moved to
// Unsolvable.csv; a seed can also be typed in by hand.
static const int IDC_PICK_LIST=3001, IDC_PICK_ALL=3002, IDC_PICK_SEED=3003, IDC_PICK_MODE=3004, IDC_PICK_ADD=3005;
struct SolverPick { std::vector<NumEntry> all; std::vector<int> kind; /* 0 lost, 1 unsolvable, 2 typed in */ std::vector<NumEntry> chosen; bool ok=false; HWND list=nullptr, edit=nullptr, combo=nullptr; };
static SolverPick g_pick;

static std::wstring solverPickText(const NumEntry& e,int kind){
   wchar_t b[128]; swprintf(b,128,L"%lld   –   %ls",e.num,solverModeName(e.mode));
   std::wstring s=b;
   if(kind==1) s+=L"   [nierozwiązywalne]";
   else if(kind==2) s+=L"   [wpisane ręcznie]";
   return s;
}
static void solverPickAccept(HWND dlg){
   int n=(int)SendMessageW(g_pick.list,LB_GETSELCOUNT,0,0);
   if(n<=0) return;
   std::vector<int> idx((size_t)n);
   SendMessageW(g_pick.list,LB_GETSELITEMS,(WPARAM)n,(LPARAM)idx.data());
   g_pick.chosen.clear();
   for(int i: idx) if(i>=0 && i<(int)g_pick.all.size()) g_pick.chosen.push_back(g_pick.all[(size_t)i]);
   g_pick.ok=!g_pick.chosen.empty();
   DestroyWindow(dlg);
}
// Adds the typed seed to the list (or just selects it if it is already there).
static void solverPickAddTyped(HWND dlg){
   wchar_t t[32]={}; GetWindowTextW(g_pick.edit,t,32);
   wchar_t* end=nullptr; long long v=_wcstoi64(t,&end,10);
   if(t[0]==0 || (end && *end) || v<0 || v>4294967295LL){
      MessageBoxW(dlg,L"Ziarno to liczba całkowita od 0 do 4294967295.",L"Solver",MB_OK|MB_ICONWARNING);
      SetFocus(g_pick.edit); return;
   }
   int mode=(int)SendMessageW(g_pick.combo,CB_GETCURSEL,0,0); if(mode<0) mode=0;
   int at=-1;
   for(size_t i=0;i<g_pick.all.size();i++) if(g_pick.all[i].num==v){ at=(int)i; break; }
   if(at<0){
      g_pick.all.push_back({v,mode}); g_pick.kind.push_back(2);
      std::wstring s=solverPickText(g_pick.all.back(),2);
      at=(int)SendMessageW(g_pick.list,LB_ADDSTRING,0,(LPARAM)s.c_str());
   }
   SendMessageW(g_pick.list,LB_SETSEL,TRUE,at);
   SendMessageW(g_pick.list,LB_SETTOPINDEX,at,0);
   SetWindowTextW(g_pick.edit,L"");
   SetFocus(g_pick.edit);
}
static LRESULT CALLBACK SolverPickProc(HWND h,UINT m,WPARAM w,LPARAM l){
   switch(m){
   case WM_CREATE:{
      solverChild(h,L"STATIC",L"Zaznacz rozdania (Ctrl/Shift – kilka). Na czerwono: nierozwiązywalne.",SS_LEFT,12,10,436,20,0);
      g_pick.list=solverChild(h,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP,12,34,436,268,IDC_PICK_LIST);
      for(size_t i=0;i<g_pick.all.size();i++){
         std::wstring s=solverPickText(g_pick.all[i],g_pick.kind[i]);
         SendMessageW(g_pick.list,LB_ADDSTRING,0,(LPARAM)s.c_str());
      }
      for(size_t i=0;i<g_pick.all.size();i++) if(g_pick.kind[i]==0){ SendMessageW(g_pick.list,LB_SETSEL,TRUE,(LPARAM)i); break; }
      solverChild(h,L"STATIC",L"Ziarno:",SS_LEFT,12,313,50,20,0);
      g_pick.edit=solverChild(h,L"EDIT",L"",WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,64,309,130,24,IDC_PICK_SEED);
      SendMessageW(g_pick.edit,EM_SETLIMITTEXT,10,0);
      g_pick.combo=solverChild(h,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,204,309,150,120,IDC_PICK_MODE);
      SendMessageW(g_pick.combo,CB_ADDSTRING,0,(LPARAM)L"Tylko król");
      SendMessageW(g_pick.combo,CB_ADDSTRING,0,(LPARAM)L"Dowolna karta");
      SendMessageW(g_pick.combo,CB_SETCURSEL,g_freeColMode?1:0,0);
      solverChild(h,L"BUTTON",L"Dodaj do listy",BS_PUSHBUTTON|WS_TABSTOP,364,308,84,26,IDC_PICK_ADD);
      solverChild(h,L"BUTTON",L"Rozwiąż wybrane",BS_DEFPUSHBUTTON|WS_TABSTOP,12,350,160,30,IDOK);
      solverChild(h,L"BUTTON",L"Zaznacz wszystkie",BS_PUSHBUTTON|WS_TABSTOP,180,350,140,30,IDC_PICK_ALL);
      solverChild(h,L"BUTTON",L"Anuluj",BS_PUSHBUTTON|WS_TABSTOP,328,350,120,30,IDCANCEL);
      return 0;}
   case WM_MEASUREITEM:{
      MEASUREITEMSTRUCT* mi=(MEASUREITEMSTRUCT*)l;
      if(mi->CtlID==(UINT)IDC_PICK_LIST){ mi->itemHeight=20; return TRUE; }
      break;}
   case WM_DRAWITEM:{
      DRAWITEMSTRUCT* di=(DRAWITEMSTRUCT*)l;
      if(di->CtlID!=(UINT)IDC_PICK_LIST || (int)di->itemID<0) break;
      bool sel=(di->itemState&ODS_SELECTED)!=0;
      int kind=((size_t)di->itemID<g_pick.kind.size())?g_pick.kind[(size_t)di->itemID]:0;
      COLORREF fg = sel ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                  : kind==1 ? RGB(190,30,30)      // unsolvable: red
                  : kind==2 ? RGB(30,90,170)      // typed in: blue
                  : GetSysColor(COLOR_WINDOWTEXT);
      FillRect(di->hDC,&di->rcItem,GetSysColorBrush(sel?COLOR_HIGHLIGHT:COLOR_WINDOW));
      wchar_t txt[160]={}; SendMessageW(di->hwndItem,LB_GETTEXT,di->itemID,(LPARAM)txt);
      SetBkMode(di->hDC,TRANSPARENT); SetTextColor(di->hDC,fg);
      HFONT old=(HFONT)SelectObject(di->hDC,solverFont(kind==1));
      RECT tr=di->rcItem; tr.left+=6;
      DrawTextW(di->hDC,txt,-1,&tr,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
      SelectObject(di->hDC,old);
      return TRUE;}
   case WM_COMMAND:
      switch(LOWORD(w)){
      case IDOK:
         if(GetFocus()==g_pick.edit){ solverPickAddTyped(h); return 0; } // Enter in the seed box adds it
         solverPickAccept(h); return 0;
      case IDC_PICK_ADD: solverPickAddTyped(h); return 0;
      case IDC_PICK_ALL: SendMessageW(g_pick.list,LB_SETSEL,TRUE,-1); return 0;
      case IDCANCEL: DestroyWindow(h); return 0;
      case IDC_PICK_LIST: if(HIWORD(w)==LBN_DBLCLK) solverPickAccept(h); return 0;
      }
      break;
   case WM_CLOSE: DestroyWindow(h); return 0;
   }
   return DefWindowProcW(h,m,w,l);
}
static bool pickSolverGames(HWND parent,const std::vector<NumEntry>& lost,const std::vector<NumEntry>& unsolvable,std::vector<NumEntry>& out){
   static bool reg=false;
   if(!reg){
      WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=SolverPickProc; wc.hInstance=GetModuleHandleW(nullptr);
      wc.lpszClassName=L"PasjansSolverPick"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); RegisterClassExW(&wc); reg=true;
   }
   g_pick=SolverPick();
   for(auto& e: lost){ g_pick.all.push_back(e); g_pick.kind.push_back(0); }
   for(auto& e: unsolvable){
      bool dup=false; for(auto& x: g_pick.all) if(x.num==e.num){ dup=true; break; }
      if(!dup){ g_pick.all.push_back(e); g_pick.kind.push_back(1); }
   }
   RECT r={0,0,460,392}; DWORD st=WS_POPUP|WS_CAPTION|WS_SYSMENU; AdjustWindowRectEx(&r,st,FALSE,0);
   HWND dlg=CreateWindowExW(0,L"PasjansSolverPick",L"Solver – wybór rozdań",st,CW_USEDEFAULT,CW_USEDEFAULT,
      r.right-r.left,r.bottom-r.top,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
   if(!dlg) return false;
   solverCenterOn(dlg,parent);
   solverModalLoop(dlg,parent);
   if(g_pick.ok) out=g_pick.chosen;
   return g_pick.ok;
}

// ── 2. "keep searching?" question with a 10 s countdown ──────────────────────
// No answer means: keep searching (the default button is "Tak").
struct SolverAsk { int secs=0; bool yes=true; HWND lbl=nullptr; long long seed=0; };
static SolverAsk g_ask;
static void solverAskLabel(){
   wchar_t b[128]; swprintf(b,128,L"Bez odpowiedzi za %d s kontynuuję poszukiwania.",g_ask.secs);
   SetWindowTextW(g_ask.lbl,b);
}
static LRESULT CALLBACK SolverAskProc(HWND h,UINT m,WPARAM w,LPARAM l){
   switch(m){
   case WM_CREATE:{
      wchar_t b[256];
      swprintf(b,256,L"Nie znalazłem rozwiązania rozdania nr %lld w ciągu 5 minut. Czy kontynuować poszukiwania?",g_ask.seed);
      solverChild(h,L"STATIC",b,SS_LEFT,14,14,492,44,0,true);   // wraps by itself, two lines fit
      g_ask.lbl=solverChild(h,L"STATIC",L"",SS_LEFT,14,66,492,20,0);
      solverAskLabel();
      solverChild(h,L"BUTTON",L"Tak, szukaj dalej",BS_DEFPUSHBUTTON|WS_TABSTOP,110,98,150,30,IDYES);
      solverChild(h,L"BUTTON",L"Nie, następne",BS_PUSHBUTTON|WS_TABSTOP,270,98,150,30,IDNO);
      SetTimer(h,1,1000,nullptr);
      return 0;}
   case WM_TIMER:
      if(--g_ask.secs<=0){ g_ask.yes=true; DestroyWindow(h); }   // no answer: carry on searching
      else solverAskLabel();
      return 0;
   case WM_COMMAND:
      if(LOWORD(w)==IDYES){ g_ask.yes=true; DestroyWindow(h); return 0; }
      if(LOWORD(w)==IDNO||LOWORD(w)==IDCANCEL){ g_ask.yes=false; DestroyWindow(h); return 0; }
      break;
   case WM_CLOSE: g_ask.yes=false; DestroyWindow(h); return 0;
   case WM_DESTROY: KillTimer(h,1); return 0;
   }
   return DefWindowProcW(h,m,w,l);
}
static bool solverAskContinue(HWND parent,long long seed){
   static bool reg=false;
   if(!reg){
      WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=SolverAskProc; wc.hInstance=GetModuleHandleW(nullptr);
      wc.lpszClassName=L"PasjansSolverAsk"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); RegisterClassExW(&wc); reg=true;
   }
   g_ask=SolverAsk(); g_ask.secs=SOLVER_ASK_SECONDS; g_ask.seed=seed;
   RECT r={0,0,520,144}; DWORD st=WS_POPUP|WS_CAPTION|WS_SYSMENU; AdjustWindowRectEx(&r,st,FALSE,0);
   HWND dlg=CreateWindowExW(WS_EX_TOPMOST,L"PasjansSolverAsk",L"Solver",st,CW_USEDEFAULT,CW_USEDEFAULT,
      r.right-r.left,r.bottom-r.top,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
   if(!dlg) return true;
   solverCenterOn(dlg,parent);
   MessageBeep(MB_ICONQUESTION);
   solverModalLoop(dlg,parent);
   return g_ask.yes;
}

// ── 2b. remembered search state (SolverState.csv) ────────────────────────────
// What survives between sessions is the search's *bookkeeping*: how many attempts
// were already made, positions searched, time spent and the best progress. The
// depth-first trees themselves are far too big to keep (millions of positions
// per attempt), but they are not needed: a resumed search simply carries on with
// the next attempt numbers — i.e. new random orderings and larger node budgets —
// instead of repeating the early attempts it has already made.
struct SolverSaved { long long num=0; int mode=0; int attempts=0; long long nodes=0; long long elapsedMs=0; int bestKings=0; int bestPlaced=0; };
static std::wstring getSolverStatePath(){ return exeDirFile(L"SolverState.csv"); }
static std::vector<SolverSaved> readSolverStates(){
   std::vector<SolverSaved> v;
   FILE* f=nullptr; _wfopen_s(&f,getSolverStatePath().c_str(),L"rb");
   if(!f) return v;
   char line[256];
   while(fgets(line,256,f)){
      if(strncmp(line,"GameNumber",10)==0) continue;
      SolverSaved s; char modeName[32]={};
      if(sscanf_s(line,"%lld,%31[^,],%d,%lld,%lld,%d,%d",&s.num,modeName,(unsigned)sizeof(modeName),&s.attempts,&s.nodes,&s.elapsedMs,&s.bestKings,&s.bestPlaced)==7){
         s.mode=strstr(modeName,"Dowolna")?1:0; v.push_back(s);
      }
   }
   fclose(f);
   return v;
}
static void writeSolverStates(const std::vector<SolverSaved>& v){
   if(v.empty()){ DeleteFileW(getSolverStatePath().c_str()); return; }
   FILE* f=nullptr; _wfopen_s(&f,getSolverStatePath().c_str(),L"wb");
   if(!f) return;
   fputs("GameNumber,Mode,Attempts,Nodes,ElapsedMs,BestKings,BestPlaced\r\n",f);
   for(auto& s: v) fprintf(f,"%lld,%s,%d,%lld,%lld,%d,%d\r\n",s.num,s.mode==0?"TylkoKrol":"DowolnaKarta",s.attempts,s.nodes,s.elapsedMs,s.bestKings,s.bestPlaced);
   fclose(f);
}
static bool loadSolverState(long long num,SolverSaved& out){
   for(auto& s: readSolverStates()) if(s.num==num){ out=s; return true; }
   return false;
}
static void saveSolverState(const SolverSaved& st){
   auto v=readSolverStates(); bool found=false;
   for(auto& s: v) if(s.num==st.num){ s=st; found=true; }
   if(!found) v.push_back(st);
   writeSolverStates(v);
}
static void clearSolverState(long long num){
   auto v=readSolverStates(); size_t n=v.size();
   v.erase(std::remove_if(v.begin(),v.end(),[&](const SolverSaved& s){ return s.num==num; }),v.end());
   if(v.size()!=n) writeSolverStates(v);
}

// ── 3. writing the solution (Solved<number>.dat, loadable with "Wczytaj grę") ─
static bool saveSolvedFile(const NumEntry& e,const GameState& start,const std::vector<solver::Mv>& moves,std::wstring& outPath){
   // state after every move; the reserve of a snapshot is what is still undealt
   solver::Board B; B.g=start.boardOnly(); B.g.reserve=start.reserve; B.rpos=0; B.freeMode=(e.mode!=0);
   std::vector<Snapshot> snaps; snaps.reserve(moves.size());
   for(const auto& m: moves){
      B.apply(m);
      Snapshot s;
      for(int i=0;i<NUM_COLS;i++)  s.cols[i]=B.g.cols[i];
      for(int i=0;i<NUM_FOUND;i++) s.found[i]=B.g.found[i];
      s.reserve.assign(B.g.reserve.begin()+B.rpos,B.g.reserve.end());
      s.isDealBoundary=(m.type==3); // this move was a reserve deal (lets the Redo-to-next-deal key find it)
      snaps.push_back(std::move(s));
   }
   Snapshot first;
   for(int i=0;i<NUM_COLS;i++)  first.cols[i]=start.cols[i];
   first.reserve=start.reserve;

   outPath=exeDirSubfolder(L"Solved")+L"Solved"+std::to_wstring(e.num)+L".dat";
   FILE* f=nullptr; _wfopen_s(&f,outPath.c_str(),L"wb");
   if(!f) return false;
   fwrite("PASJ",1,4,f);
   BYTE ver=12; fwrite(&ver,1,1,f);
   auto writeVec=[&](const std::vector<Card>& v){
      BYTE n=(BYTE)std::min((int)v.size(),255); fwrite(&n,1,1,f);
      for(int i=0;i<n;i++){ BYTE s=(BYTE)v[i].suit, r=(BYTE)v[i].rank; fwrite(&s,1,1,f); fwrite(&r,1,1,f); }
   };
   for(int i=0;i<NUM_COLS;i++)  writeVec(start.cols[i]);
   for(int i=0;i<NUM_FOUND;i++) writeVec(start.found[i]);
   writeVec(start.reserve);
   DWORD zero=0; BYTE b0=0, b1=1;
   fwrite(&zero,4,1,f);                        // move count
   BYTE fm=(BYTE)(e.mode!=0?1:0); fwrite(&fm,1,1,f);
   fwrite(&b0,1,1,f);                          // winCounted
   fwrite(&b0,1,1,f);                          // noMovesReached
   fwrite(&b0,1,1,f);                          // noMovesDialogShown
   fwrite(&b1,1,1,f);                          // statsExcluded: replaying a solution is practice, not a new game
   fwrite(&fm,1,1,f);                          // outcome mode
   fwrite(&zero,4,1,f); fwrite(&zero,4,1,f);   // outcome moves / seconds
   writeSnapshot(f,first);                     // initialDeal
   DWORD num=(DWORD)e.num; fwrite(&num,4,1,f); // game number
   fwrite(&zero,4,1,f);                        // elapsed seconds
   WORD un=0; fwrite(&un,2,1,f);               // undo stack: empty
   WORD rn=(WORD)std::min((size_t)65535,snaps.size()); fwrite(&rn,2,1,f);
   // redoStack.back() is the NEXT step, so the file lists the last step first
   for(int i=(int)rn-1;i>=0;i--) writeSnapshot(f,snaps[(size_t)i]);
   bool ok=(ferror(f)==0);
   fclose(f);
   return ok;
}

// ── 4. the progress window and the batch driver ──────────────────────────────
struct SolverRun {
   std::vector<NumEntry> list; size_t idx=0;
   HWND hwnd=nullptr, lblTitle=nullptr, lblTime=nullptr, lblAttempts=nullptr, lblCurrent=nullptr, lblBest=nullptr, lblNodes=nullptr;
   HWND barTime=nullptr, barCurrent=nullptr, barBest=nullptr, btnStop=nullptr;
   std::unique_ptr<solver::Progress> P;
   std::atomic<bool> cancel{false};
   std::atomic<bool> threadDone{false};
   HANDLE thread=nullptr;
   solver::Result result;
   GameState start; long long seed=0; bool freeMode=false;
   ULONGLONG gameStart=0, stageStart=0, deadline=0; int stage=1;
   ULONGLONG lastTick=0; long long lastNodes=0; double speed=0;
   bool busy=false, finished=false;
   int resumedAttempts=0;
   int solvedCount=0, unsolvableCount=0, abortedCount=0;
   std::wstring log;
};
static SolverRun g_sv;
static const int IDC_SV_STOP=3101;

static DWORD WINAPI solverCtlProc(LPVOID){
   g_sv.result=solver::solveDeal(g_sv.start,g_sv.freeMode,*g_sv.P,g_sv.cancel,g_sv.deadline,solver::defaultThreads());
   g_sv.threadDone.store(true);
   return 0;
}
static void solverLaunchThread(){
   g_sv.threadDone.store(false);
   g_sv.thread=CreateThread(nullptr,0,solverCtlProc,nullptr,0,nullptr);
   if(!g_sv.thread){ g_sv.result=solver::Result(); g_sv.result.cancelled=true; g_sv.threadDone.store(true); }
}
static void solverStartCurrent(){
   const NumEntry& e=g_sv.list[g_sv.idx];
   g_sv.seed=e.num; g_sv.freeMode=(e.mode!=0);
   g_sv.start.newGame((unsigned int)e.num);
   g_sv.P.reset(new solver::Progress());
   g_sv.cancel.store(false);
   g_sv.gameStart=g_sv.stageStart=GetTickCount64();
   g_sv.resumedAttempts=0;
   SolverSaved sv;
   if(loadSolverState(e.num,sv)){           // carry on where an earlier session left off
      g_sv.resumedAttempts=sv.attempts;
      g_sv.P->attemptsStarted.store(sv.attempts); g_sv.P->attemptsFinished.store(sv.attempts);
      g_sv.P->totalNodes.store(sv.nodes);
      g_sv.P->bestKings.store(sv.bestKings); g_sv.P->bestPlaced.store(sv.bestPlaced);
      g_sv.gameStart-=(ULONGLONG)sv.elapsedMs;
   }
   g_sv.deadline=g_sv.stageStart+SOLVER_STAGE_MS; g_sv.stage=1;
   g_sv.lastTick=g_sv.stageStart; g_sv.lastNodes=0; g_sv.speed=0;
   if(g_sv.btnStop) EnableWindow(g_sv.btnStop,TRUE);
   solverLaunchThread();
}
static void solverLog(const std::wstring& line){ g_sv.log+=line+L"\r\n"; }

static void solverUpdateUI(){
   ULONGLONG now=GetTickCount64();
   solver::Progress& P=*g_sv.P;
   wchar_t b[320];
   swprintf(b,320,L"Rozdanie %d z %d  –  nr %lld  (%ls)",(int)g_sv.idx+1,(int)g_sv.list.size(),g_sv.seed,solverModeName(g_sv.freeMode?1:0));
   SetWindowTextW(g_sv.lblTitle,b);

   ULONGLONG st=now-g_sv.stageStart, tot=now-g_sv.gameStart;
   if(st>SOLVER_STAGE_MS) st=SOLVER_STAGE_MS;
   swprintf(b,320,L"Czas etapu %d: %d:%02d z 5:00     (łącznie dla tego rozdania %d:%02d)",g_sv.stage,
      (int)(st/60000),(int)((st/1000)%60),(int)(tot/60000),(int)((tot/1000)%60));
   SetWindowTextW(g_sv.lblTime,b);
   SendMessageW(g_sv.barTime,PBM_SETPOS,(WPARAM)(st*1000/SOLVER_STAGE_MS),0);

   if(g_sv.resumedAttempts>0)
      swprintf(b,320,L"Próby: rozpoczęto %d, zakończono %d   (równolegle: %d wątków; wznowione po %d próbach)",P.attemptsStarted.load(),P.attemptsFinished.load(),P.threads.load(),g_sv.resumedAttempts);
   else
      swprintf(b,320,L"Próby: rozpoczęto %d, zakończono %d   (równolegle: %d wątków)",P.attemptsStarted.load(),P.attemptsFinished.load(),P.threads.load());
   SetWindowTextW(g_sv.lblAttempts,b);

   int la=P.leadAttempt.load(); long long ln=P.leadNodes.load(), lb=P.leadBudget.load();
   if(la>0 && lb>0){
      std::wstring n1=solverNum(ln), n2=solverNum(lb);
      swprintf(b,320,L"Bieżąca próba nr %d: ruch %d (najgłębiej %d), przeszukano %ls z %ls węzłów",la,P.leadDepth.load(),P.leadMaxDepth.load(),n1.c_str(),n2.c_str());
      SendMessageW(g_sv.barCurrent,PBM_SETPOS,(WPARAM)(std::min<long long>(1000,ln*1000/lb)),0);
   } else {
      swprintf(b,320,L"Bieżąca próba: uruchamianie…");
      SendMessageW(g_sv.barCurrent,PBM_SETPOS,0,0);
   }
   SetWindowTextW(g_sv.lblCurrent,b);

   int bk=P.bestKings.load(), bp=P.bestPlaced.load(), bpl=P.bestPrefixLen.load();
   if(P.solutionFound.load()){
      // A win exists already — bestKings==8 here isn't "still searching for
      // the win", it's "found it, now looking for a SHORTER line" (see
      // workerProc()'s POLISH_EXTRA_MS in solver.h). Without this message the
      // window looks stuck at "8 z 8" with no explanation for why it keeps running.
      ULONGLONG elapsed=now-P.solutionFoundAt.load();
      int remainSec=(elapsed<solver::POLISH_EXTRA_MS)?(int)((solver::POLISH_EXTRA_MS-elapsed+999)/1000):0;
      swprintf(b,320,L"Rozwiązanie znalezione! Długość: %d ruchów. Szukam krótszej wersji jeszcze przez %d s…",P.solutionLen.load(),remainSec);
   } else if(bpl>0)
      swprintf(b,320,L"Najlepszy dotychczasowy postęp: króle na miejscu %d z 8, ułożone karty %d ze 104 (próby budują dalej na linii %d ruchów)",bk,bp,bpl);
   else
      swprintf(b,320,L"Najlepszy dotychczasowy postęp: króle na miejscu %d z 8, ułożone karty %d ze 104",bk,bp);
   SetWindowTextW(g_sv.lblBest,b);
   SendMessageW(g_sv.barBest,PBM_SETPOS,(WPARAM)(bp*1000/104),0);

   long long nodes=P.totalNodes.load();
   if(now-g_sv.lastTick>=1000){
      g_sv.speed=(double)(nodes-g_sv.lastNodes)*1000.0/(double)(now-g_sv.lastTick);
      g_sv.lastTick=now; g_sv.lastNodes=nodes;
   }
   std::wstring n1=solverNum(nodes), n2=solverNum((long long)g_sv.speed);
   swprintf(b,320,L"Przeszukano łącznie %ls pozycji  (ok. %ls na sekundę)",n1.c_str(),n2.c_str());
   SetWindowTextW(g_sv.lblNodes,b);
}

static void solverFinish(){
   g_sv.finished=true;
   KillTimer(g_sv.hwnd,1);
   DestroyWindow(g_sv.hwnd);
}
static void solverNext(){
   g_sv.idx++;
   if(g_sv.idx>=g_sv.list.size()){ solverFinish(); return; }
   solverStartCurrent();
}
static void solverSaveProgress(){
   if(!g_sv.P) return;
   const NumEntry& e=g_sv.list[g_sv.idx];
   SolverSaved s; s.num=e.num; s.mode=e.mode;
   s.attempts=g_sv.P->attemptsStarted.load(); s.nodes=g_sv.P->totalNodes.load();
   s.elapsedMs=(long long)(GetTickCount64()-g_sv.gameStart);
   s.bestKings=g_sv.P->bestKings.load(); s.bestPlaced=g_sv.P->bestPlaced.load();
   saveSolverState(s);
}
static void solverHandleResult(){
   WaitForSingleObject(g_sv.thread,INFINITE); CloseHandle(g_sv.thread); g_sv.thread=nullptr;
   const NumEntry e=g_sv.list[g_sv.idx];
   solver::Result& r=g_sv.result;
   wchar_t b[256];
   if(r.solved){
      std::wstring path;
      if(saveSolvedFile(e,g_sv.start,r.moves,path)){
         appendWonNumber(e.num,e.mode);
         removeNumberEntry(getLostNumbersPath(),e.num);
         removeNumberEntry(getUnsolvablePath(),e.num);
         clearSolverState(e.num);
         appendSolverLog(e.num,GetTickCount64()-g_sv.gameStart,g_sv.P->attemptsFinished.load(),g_sv.P->totalNodes.load(),(int)r.moves.size());
         g_sv.solvedCount++;
         swprintf(b,256,L"#%lld: rozwiązano w %d ruchach  →  Solved\\Solved%lld.dat",e.num,(int)r.moves.size(),e.num);
      } else {
         swprintf(b,256,L"#%lld: rozwiązano, ale nie udało się zapisać pliku Solved\\Solved%lld.dat",e.num,e.num);
      }
      solverLog(b);
      solverNext();
   } else if(r.cancelled){
      g_sv.abortedCount++;
      solverSaveProgress(); // remembered, so a later search of this deal resumes
      swprintf(b,256,L"#%lld: przerwano (postęp zapamiętany)",e.num); solverLog(b);
      solverFinish();
   } else if(GetTickCount64()-g_sv.gameStart >= SOLVER_ABSOLUTE_MAX_MS){
      // Absolute cap reached (see its own comment) — give up without asking,
      // same outcome as the player explicitly declining to continue.
      solverUpdateUI();
      removeNumberEntry(getLostNumbersPath(),e.num);
      appendNumberEntry(getUnsolvablePath(),e.num,e.mode);
      solverSaveProgress();
      g_sv.unsolvableCount++;
      swprintf(b,256,L"#%lld: przekroczono limit 2 godzin łącznego szukania  →  Unsolvable.csv (postęp zapamiętany)",e.num); solverLog(b);
      solverNext();
   } else {
      // 5-minute stage over without a solution
      solverUpdateUI();
      solverSaveProgress();
      bool again=solverAskContinue(g_sv.hwnd,e.num);
      if(again){
         g_sv.stage++;
         g_sv.stageStart=GetTickCount64();
         g_sv.deadline=g_sv.stageStart+SOLVER_STAGE_MS;
         solverLaunchThread();
      } else {
         removeNumberEntry(getLostNumbersPath(),e.num);
         appendNumberEntry(getUnsolvablePath(),e.num,e.mode);
         g_sv.unsolvableCount++;
         swprintf(b,256,L"#%lld: nie znaleziono rozwiązania  →  Unsolvable.csv (postęp zapamiętany)",e.num); solverLog(b);
         solverNext();
      }
   }
}
static void solverPoll(){
   if(g_sv.busy||g_sv.finished||!g_sv.P) return;
   g_sv.busy=true;
   solverUpdateUI();
   if(g_sv.threadDone.load()) solverHandleResult();
   g_sv.busy=false;
}
static LRESULT CALLBACK SolverProgProc(HWND h,UINT m,WPARAM w,LPARAM l){
   switch(m){
   case WM_CREATE:{
      INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_PROGRESS_CLASS}; InitCommonControlsEx(&icc);
      SolverRun& S=g_sv;
      S.lblTitle   =solverChild(h,L"STATIC",L"",SS_LEFT,14,12,492,22,0,true);
      S.lblTime    =solverChild(h,L"STATIC",L"",SS_LEFT,14,44,492,18,0);
      S.barTime    =solverChild(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,14,64,492,14,0);
      S.lblAttempts=solverChild(h,L"STATIC",L"",SS_LEFT,14,92,492,18,0);
      S.lblCurrent =solverChild(h,L"STATIC",L"",SS_LEFT,14,118,492,18,0);
      S.barCurrent =solverChild(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,14,138,492,14,0);
      // 36px (not the usual 18px one-line height): both messages this label can
      // show — the "(próby budują dalej...)" suffix and the "Rozwiązanie
      // znalezione..." polish-phase message below — routinely wrap to a second
      // line at this dialog's width, and a STATIC control with SS_LEFT word-
      // wraps automatically but simply clips any line past its own height, so
      // a too-short control silently ate the wrapped second line.
      S.lblBest    =solverChild(h,L"STATIC",L"",SS_LEFT,14,164,492,36,0);
      S.barBest    =solverChild(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,14,202,492,14,0);
      S.lblNodes   =solverChild(h,L"STATIC",L"",SS_LEFT,14,228,492,18,0);
      S.btnStop    =solverChild(h,L"BUTTON",L"Przerwij",BS_PUSHBUTTON|WS_TABSTOP,210,260,100,30,IDC_SV_STOP);
      for(HWND bar: {S.barTime,S.barCurrent,S.barBest}) SendMessageW(bar,PBM_SETRANGE32,0,1000);
      SetTimer(h,1,250,nullptr);
      return 0;}
   case WM_TIMER: if(w==1) solverPoll(); return 0;
   case WM_COMMAND:
      if(LOWORD(w)==IDC_SV_STOP || LOWORD(w)==IDCANCEL){ g_sv.cancel.store(true); EnableWindow(g_sv.btnStop,FALSE); return 0; }
      break;
   case WM_CLOSE: g_sv.cancel.store(true); EnableWindow(g_sv.btnStop,FALSE); return 0; // the worker ends, solverPoll() then closes the window
   }
   return DefWindowProcW(h,m,w,l);
}
static void runSolverBatch(HWND parent,const std::vector<NumEntry>& picked){
   static bool reg=false;
   if(!reg){
      WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=SolverProgProc; wc.hInstance=GetModuleHandleW(nullptr);
      wc.lpszClassName=L"PasjansSolverProg"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); RegisterClassExW(&wc); reg=true;
   }
   g_sv.list=picked; g_sv.idx=0; g_sv.finished=false; g_sv.busy=false;
   g_sv.solvedCount=g_sv.unsolvableCount=g_sv.abortedCount=0; g_sv.log.clear();
   RECT r={0,0,520,304}; DWORD st=WS_POPUP|WS_CAPTION|WS_SYSMENU; AdjustWindowRectEx(&r,st,FALSE,0);
   g_sv.hwnd=CreateWindowExW(0,L"PasjansSolverProg",L"Solver – szukanie rozwiązań",st,CW_USEDEFAULT,CW_USEDEFAULT,
      r.right-r.left,r.bottom-r.top,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
   if(!g_sv.hwnd) return;
   solverCenterOn(g_sv.hwnd,parent);
   g_sv.P.reset(new solver::Progress());
   solverStartCurrent();
   solverUpdateUI();
   solverModalLoop(g_sv.hwnd,parent);
   // summary
   std::wstring msg=L"Rozwiązane: "+std::to_wstring(g_sv.solvedCount)+L"   Nierozwiązane: "+std::to_wstring(g_sv.unsolvableCount);
   if(g_sv.abortedCount) msg+=L"   Przerwane: "+std::to_wstring(g_sv.abortedCount);
   msg+=L"\r\n\r\n"+g_sv.log;
   if(g_sv.solvedCount) msg+=L"\r\nRozwiązania (folder Solved) wczytasz przez Akcje → Wczytaj grę; kolejne kroki przechodzi przycisk „Ponów”.";
   MessageBoxW(parent,msg.c_str(),L"Solver – wynik",MB_OK|(g_sv.solvedCount?MB_ICONINFORMATION:MB_ICONWARNING));
}

// ── 5. the toolbar button ────────────────────────────────────────────────────
static void showSolver(HWND hwnd){
   std::vector<NumEntry> lost=readNumberFile(getLostNumbersPath());
   std::vector<NumEntry> unsolvable=readNumberFile(getUnsolvablePath());
   std::vector<NumEntry> sel;
   if(!pickSolverGames(hwnd,lost,unsolvable,sel) || sel.empty()) return;
   // the first picked deal is put on the table (in its own mode), as a practice game
   g_freeColMode=(sel[0].mode!=0);
   newGame(false,sel[0].num,false,true); // silent: no "new deal" sound
   g_statsExcluded=true;
   runSolverBatch(hwnd,sel);   // and the search starts right away
}

// ============================================================================
// "Zagraj nieudany" (Akcje menu, under "Zagraj wygrywający") — pick one deal
// from LostNumbers.csv (the same list the Solver works through: deals that
// ended in "no moves" or the auto-player getting stuck — see LostNumbers.csv's
// own comment) and load it onto the table, same as "Zagraj wygrywający" does
// for WonNumbers.csv. Reuses the Solver picker's small dialog helpers
// (solverChild/solverCenterOn/solverModalLoop/solverModeName), defined above.
// ============================================================================
static const int IDC_PF_LIST=3201, IDC_PF_FILTER=3202;
struct PlayFailedPick {
   std::vector<NumEntry> all;
   std::vector<int> shown;      // indices into `all` for each row currently in the listbox
   long long chosen=-1; int chosenMode=0; bool ok=false;
   HWND list=nullptr, filter=nullptr, btnPlay=nullptr;
};
static PlayFailedPick g_pf;

// In-place, case-already-lowered-by-caller: folds the handful of Polish
// diacritics that show up in "Tylko król"/"Dowolna karta" down to their plain
// ASCII letter, so someone filtering by typing "krol" (no diacritic — the
// common case when a keyboard layout or habit skips them) still matches
// "król". Deliberately tiny/hand-rolled rather than a full Unicode
// normalization table — this dialog only ever displays the mode name and a
// plain decimal number, so that's the entire alphabet it needs to cover.
static void foldPolish(wchar_t* s){
   static const wchar_t from[]=L"ąćęłńóśźż";
   static const wchar_t to[]  =L"acelnoszz";
   for(; *s; ++s){
      const wchar_t* p = wcschr(from,*s);
      if(p) *s = to[p-from];
   }
}
// Rebuilds the visible listbox from `g_pf.all`, keeping only the rows whose
// text contains the filter box's text (case/diacritic-insensitive substring —
// matches against the game number and the mode name both, so typing e.g.
// "krol" narrows to king-only deals). Called once up front and on every
// EN_CHANGE.
static void pfRebuildList(){
   wchar_t f[64]={}; GetWindowTextW(g_pf.filter,f,64); CharLowerW(f); foldPolish(f);
   SendMessageW(g_pf.list,LB_RESETCONTENT,0,0);
   g_pf.shown.clear();
   for(size_t i=0;i<g_pf.all.size();i++){
      wchar_t b[64]; swprintf(b,64,L"%lld   –   %ls",g_pf.all[i].num,solverModeName(g_pf.all[i].mode));
      wchar_t low[64]; wcscpy_s(low,b); CharLowerW(low); foldPolish(low);
      if(f[0] && !wcsstr(low,f)) continue;
      SendMessageW(g_pf.list,LB_ADDSTRING,0,(LPARAM)b);
      g_pf.shown.push_back((int)i);
   }
   EnableWindow(g_pf.btnPlay,FALSE); // filtering (or first build) always drops any prior selection
}
static void pfAccept(HWND dlg){
   int sel=(int)SendMessageW(g_pf.list,LB_GETCURSEL,0,0);
   if(sel<0 || sel>=(int)g_pf.shown.size()) return; // "Zagraj wybrany" is disabled with nothing selected, but LBN_DBLCLK can still race an empty list
   const NumEntry& e = g_pf.all[g_pf.shown[(size_t)sel]];
   g_pf.chosen=e.num; g_pf.chosenMode=e.mode; g_pf.ok=true;
   DestroyWindow(dlg);
}
static LRESULT CALLBACK PlayFailedProc(HWND h,UINT m,WPARAM w,LPARAM l){
   switch(m){
   case WM_CREATE:{
      solverChild(h,L"STATIC",L"Filtruj:",SS_LEFT,12,14,56,20,0);
      g_pf.filter=solverChild(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,72,11,326,24,IDC_PF_FILTER);
      g_pf.list=solverChild(h,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS|WS_TABSTOP,12,44,386,266,IDC_PF_LIST);
      g_pf.btnPlay=solverChild(h,L"BUTTON",L"Zagraj wybrany",BS_DEFPUSHBUTTON|WS_TABSTOP|WS_DISABLED,12,320,180,30,IDOK);
      solverChild(h,L"BUTTON",L"Anuluj",BS_PUSHBUTTON|WS_TABSTOP,248,320,150,30,IDCANCEL);
      pfRebuildList();
      SetFocus(g_pf.filter);
      return 0;}
   case WM_COMMAND:
      switch(LOWORD(w)){
      case IDC_PF_FILTER: if(HIWORD(w)==EN_CHANGE) pfRebuildList(); return 0;
      case IDC_PF_LIST:
         if(HIWORD(w)==LBN_SELCHANGE)
            EnableWindow(g_pf.btnPlay, SendMessageW(g_pf.list,LB_GETCURSEL,0,0)!=LB_ERR);
         else if(HIWORD(w)==LBN_DBLCLK) pfAccept(h);
         return 0;
      case IDOK: pfAccept(h); return 0;
      case IDCANCEL: DestroyWindow(h); return 0;
      }
      break;
   case WM_CLOSE: DestroyWindow(h); return 0;
   }
   return DefWindowProcW(h,m,w,l);
}
static void showPlayFailedDialog(HWND hwnd){
   std::vector<NumEntry> lost=readNumberFile(getLostNumbersPath());
   if(lost.empty()){
      MessageBoxW(hwnd,L"Lista LostNumbers.csv jest pusta.\r\n\r\nDo pliku trafiają rozdania zakończone brakiem ruchów lub utknięciem automatu.",
         L"Zagraj nieudany",MB_OK|MB_ICONINFORMATION);
      return;
   }
   static bool reg=false;
   if(!reg){
      WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=PlayFailedProc; wc.hInstance=GetModuleHandleW(nullptr);
      wc.lpszClassName=L"PasjansPlayFailedPick"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); RegisterClassExW(&wc); reg=true;
   }
   g_pf=PlayFailedPick(); g_pf.all=lost;
   RECT r={0,0,410,364}; DWORD st=WS_POPUP|WS_CAPTION|WS_SYSMENU; AdjustWindowRectEx(&r,st,FALSE,0);
   HWND dlg=CreateWindowExW(0,L"PasjansPlayFailedPick",L"Zagraj nieudany",st,CW_USEDEFAULT,CW_USEDEFAULT,
      r.right-r.left,r.bottom-r.top,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
   if(!dlg) return;
   solverCenterOn(dlg,hwnd);
   solverModalLoop(dlg,hwnd);
   if(g_pf.ok){
      g_freeColMode=(g_pf.chosenMode!=0);
      newGame(false,g_pf.chosen);
      // Replaying an already-lost deal is practice/review, not a fresh
      // attempt — never counted towards statistics (mirrors "Zagraj wygrywający").
      g_statsExcluded=true;
   }
}

// ============================================================================
// Auto-update (GitHub Releases) — see update.h for the network/version/
// self-replace mechanics. Everything here is just the UI glue: kick off the
// check on a background thread so a slow/unreachable network never delays
// startup, hand the result back to the main thread via a private message
// (ReleaseInfo is heap-allocated by the worker and freed by whichever
// handler consumes it), and — only on explicit "Tak" — download the new exe
// on another background thread before handing off to launchSelfUpdate().
static const UINT WM_UPDATE_CHECK_DONE = WM_APP+1;
static const UINT WM_UPDATE_DOWNLOAD_DONE = WM_APP+2;

static DWORD WINAPI updateCheckThreadProc(LPVOID param){
   HWND hwnd=(HWND)param;
   update::ReleaseInfo* info=new update::ReleaseInfo(update::checkLatest());
   PostMessageW(hwnd,WM_UPDATE_CHECK_DONE,0,(LPARAM)info);
   return 0;
}

struct UpdateDownloadJob { HWND hwnd; std::wstring url, destPath; bool ok; };
static DWORD WINAPI updateDownloadThreadProc(LPVOID param){
   UpdateDownloadJob* job=(UpdateDownloadJob*)param;
   job->ok=update::httpDownload(job->url,job->destPath);
   PostMessageW(job->hwnd,WM_UPDATE_DOWNLOAD_DONE,0,(LPARAM)job);
   return 0;
}

// Kicks off the actual download+install once the user answered "Tak" to the
// "nowsza wersja dostępna" prompt. Runs the download on a background thread
// (a release exe is a few MB — enough to visibly hang the UI on a slow link)
static void beginUpdateDownload(HWND hwnd,const std::wstring& url){
   wchar_t exePath[MAX_PATH];
   GetModuleFileNameW(nullptr,exePath,MAX_PATH);
   std::wstring newPath=std::wstring(exePath)+L".new";
   UpdateDownloadJob* job=new UpdateDownloadJob{hwnd,url,newPath,false};
   CreateThread(nullptr,0,updateDownloadThreadProc,job,0,nullptr);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
   switch(msg){
   case WM_UPDATE_CHECK_DONE:{
      std::unique_ptr<update::ReleaseInfo> info((update::ReleaseInfo*)lp);
      if(info->ok && update::isNewer(info->tag,APP_VERSION)){
         std::wstring msg2=L"Dostępna nowsza wersja programu: "+info->tag+
            L" (masz "+std::wstring(APP_VERSION)+L").\n\nZaktualizować teraz?";
         if(!info->notes.empty()){
            std::wstring notes=info->notes;
            if(notes.size()>500) notes=notes.substr(0,500)+L"…";
            msg2+=L"\n\nCo nowego:\n"+notes;
         }
         if(MessageBoxW(hwnd,msg2.c_str(),L"Aktualizacja dostępna",MB_YESNO|MB_ICONINFORMATION)==IDYES)
            beginUpdateDownload(hwnd,info->downloadUrl);
      }
      return 0;}
   case WM_UPDATE_DOWNLOAD_DONE:{
      std::unique_ptr<UpdateDownloadJob> job((UpdateDownloadJob*)lp);
      if(!job->ok){
         MessageBoxW(hwnd,L"Nie udało się pobrać aktualizacji. Spróbuj ponownie później.",
            L"Aktualizacja",MB_OK|MB_ICONERROR);
         DeleteFileW(job->destPath.c_str());
         return 0;
      }
      wchar_t exePath[MAX_PATH];
      GetModuleFileNameW(nullptr,exePath,MAX_PATH);
      if(update::launchSelfUpdate(job->destPath,exePath)){
         DestroyWindow(hwnd); // release the exe's file lock so the helper .bat can swap it in
      } else {
         MessageBoxW(hwnd,L"Pobrano aktualizację, ale nie udało się jej zainstalować automatycznie.",
            L"Aktualizacja",MB_OK|MB_ICONERROR);
      }
      return 0;}
   case WM_CREATE:{
      g_hwnd=hwnd;
      for(int _i=0;_i<NUM_COLS;_i++) g_hideDealCol[_i]=-1;
      // Create child window for D2D game area (below toolbar)
      // This keeps D2D completely separate from Win32 toolbar controls
      const int TH=Layout::TOOLBAR_H;
      RECT cr; GetClientRect(hwnd,&cr);
      g_gameHwnd=CreateWindowExW(0,L"PasjansD7Game",nullptr,
         WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
         0,TH,cr.right,cr.bottom-TH,
         hwnd,nullptr,((CREATESTRUCT*)lp)->hInstance,nullptr);
      CardImagesD2D::instance().init(((CREATESTRUCT*)lp)->hInstance,g_wicFactory);
      // Create shared tooltip window for all buttons
      HWND hTip=CreateWindowExW(0,L"tooltips_class32",nullptr,
         WS_POPUP|0x01/*TTS_ALWAYSTIP*/|0x08/*TTS_NOPREFIX*/,
         0,0,0,0,hwnd,nullptr,((CREATESTRUCT*)lp)->hInstance,nullptr);
      SendMessageW(hTip,0x0418/*TTM_SETDELAYTIME*/,0/*TTDT_AUTOMATIC*/,500);
      SendMessageW(hTip,0x0418/*TTM_SETDELAYTIME*/,1/*TTDT_AUTOPOP*/,8000);
      SendMessageW(hTip,0x0418/*TTM_SETDELAYTIME*/,2/*TTDT_INITIAL*/,500);
      SetPropW(hwnd,L"TIPWND",(HANDLE)hTip);
      // Create 12pt bold font for buttons
      HFONT hBtnFont = CreateFontW(
         -MulDiv(12, GetDeviceCaps(GetDC(nullptr),LOGPIXELSY), 72),
         0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
         DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
         CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

      // Icon buttons (69x64 owner-draw, PNG loaded from exe dir), each with a
      // caption label underneath.
      const int BTN_W=69, BTN_H=64, BTN_Y=4, BTN_GAP=8;
      const int BTN_SPACING=BTN_W+BTN_GAP;
      const int BX_NEW  =8;
      const int BX_HINT =BX_NEW  +BTN_SPACING;
      const int BX_SAMO =BX_HINT +BTN_SPACING;
      const int BX_SOLV =BX_SAMO +BTN_SPACING;
      const int BX_UNDO =BX_SOLV +BTN_SPACING;
      const int BX_REDO =BX_UNDO +BTN_SPACING;
      const int BX_STATS=BX_REDO +BTN_SPACING;
      const int BX_SETT =BX_STATS+BTN_SPACING;
      auto mkIconBtn=[&](int id,int x,const wchar_t* tip)->HWND{
         HWND h=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            x,BTN_Y,BTN_W,BTN_H,hwnd,(HMENU)(INT_PTR)id,
            ((CREATESTRUCT*)lp)->hInstance,nullptr);
         return h;};
      auto addTip=[&](HWND hBtn,const wchar_t* tip){
         // Register button with the shared tooltip window
         struct TI { UINT cbSize;UINT uFlags;HWND hwnd;UINT_PTR uId;
                     RECT rect;HINSTANCE hinst;LPWSTR lpszText;LPARAM lParam;
         } ti={sizeof(struct TI)};
         ti.uFlags=0x10/*TTF_SUBCLASS*/|0x20/*TTF_IDISHWND*/;
         ti.hwnd=hwnd; ti.uId=(UINT_PTR)hBtn;
         ti.lpszText=(LPWSTR)tip;
         SendMessageW(hTip,0x0432/*TTM_ADDTOOLW*/,0,(LPARAM)&ti);
      };
      HWND hBNew  =mkIconBtn(ID_NEW,     BX_NEW,  L"Nowa gra (F2)");
      HWND hBHint =mkIconBtn(ID_HINT,    BX_HINT, L"Podpowiedź (P)");
      HWND hBSamo =mkIconBtn(ID_SAMOGRAJ,BX_SAMO, L"Samograj (G)");
      HWND hBSolv =mkIconBtn(ID_SOLVER,  BX_SOLV, L"Solver");
      HWND hBUndo =mkIconBtn(ID_UNDO,    BX_UNDO, L"Cofnij (←)");
      HWND hBRedo =mkIconBtn(ID_REDO,    BX_REDO, L"Ponów (→)");
      HWND hBStats=mkIconBtn(ID_STATS,   BX_STATS,L"Statystyki (S)");
      HWND hBSett =mkIconBtn(ID_SETTINGS,BX_SETT, L"Ustawienia");
      addTip(hBNew,   L"Nowa gra (F2)");
      addTip(hBHint,  L"Podpowiedź (P)");
      addTip(hBSamo,  L"Samograj (G)");
      addTip(hBSolv,  L"Solver – szukanie rozwiązań rozdań z LostNumbers.csv");
      addTip(hBUndo,  L"Cofnij (←)");
      addTip(hBRedo,  L"Ponów (→)");
      addTip(hBStats, L"Statystyki (S)");
      addTip(hBSett,  L"Ustawienia");

      // Caption labels under each button (small static text, centred)
      HFONT hCaptionFont=CreateFontW(
         -MulDiv(8, GetDeviceCaps(GetDC(nullptr),LOGPIXELSY), 72),
         0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
         DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
         CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
      auto mkLabel=[&](int x,const wchar_t* text){
         HWND h=CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_CENTER,
            x,BTN_Y+BTN_H+2,BTN_W,16,hwnd,nullptr,
            ((CREATESTRUCT*)lp)->hInstance,nullptr);
         SendMessageW(h,WM_SETFONT,(WPARAM)hCaptionFont,TRUE);
      };
      mkLabel(BX_NEW,  L"Nowa");
      mkLabel(BX_HINT, L"Podpowiedź");
      mkLabel(BX_SAMO, L"Samograj");
      mkLabel(BX_SOLV, L"Solver");
      mkLabel(BX_UNDO, L"Cofnij");
      mkLabel(BX_REDO, L"Ponów");
      mkLabel(BX_STATS,L"Statystyki");
      mkLabel(BX_SETT, L"Ustawienia");

      // Auto-play scoreboard: three lines, one under the other, right of the
      // Settings button (hidden until auto-play is used — see updateAutoStatsUI).
      {
         HFONT hStatFont=CreateFontW(
            -MulDiv(10, GetDeviceCaps(GetDC(nullptr),LOGPIXELSY), 72),
            0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
         const int SX=BX_SETT+BTN_W+BTN_GAP+12;
         for(int i=0;i<3;i++){
            g_hAutoStat[i]=CreateWindowW(L"STATIC",L"",WS_CHILD|SS_LEFT|SS_NOPREFIX,
               SX,8+i*26,190,22,hwnd,nullptr,((CREATESTRUCT*)lp)->hInstance,nullptr);
            SendMessageW(g_hAutoStat[i],WM_SETFONT,(WPARAM)hStatFont,TRUE);
         }
         updateAutoStatsUI(false);
      }

      SetMenu(hwnd, buildAkcjeMenu());
      g_renderer.setLayout(&g_layout);
      SoundSystem::instance().init(hwnd);
      loadSettings();
      if(g_checkUpdatesOnStart) CreateThread(nullptr,0,updateCheckThreadProc,hwnd,0,nullptr);
      loadKeyBindings(getIniPath());
      GameState::s_freeColMode=&g_freeColMode;
      GameState::s_realArtificialSince=&g_realArtificialSince;
      GameState::s_realFoundSentAt=&g_realFoundSentAt;
      GameState::s_searchDepthFree=&g_searchDepthFree;
      GameState::s_searchDepthKing=&g_searchDepthKing;
      GameState::s_realMoveCounter=&g_realMoveCounter;
      loadStats();
      // Try to resume saved game; start new if none
      loadButtonIcons(((CREATESTRUCT*)lp)->hInstance);
      if(loadGameState() && !g_game.checkWin() && g_game.hasAnyMove()){
         g_dealing=false;
         g_smoothActive=false;
         g_gameReady=true;
         g_needsFirstLayout=true;
         g_gameStartTick=timeGetTime()-(DWORD)(g_gameSeconds*1000);
         SetTimer(hwnd,TIMER_SECOND,250,nullptr);
         PostMessageW(hwnd, WM_SIZE, SIZE_RESTORED, 0);
      } else {
         newGame();
      }
      return 0;}

   case WM_COMMAND:
      // Restore keyboard focus to main window whenever a control sends WM_COMMAND
      // (buttons steal focus when clicked — this ensures shortcuts keep working)
      SetFocus(hwnd);
      // Any toolbar/menu action other than the Samograj toggle itself is a
      // manual move by the player — hand control back from self-play.
      if(LOWORD(wp)!=ID_SAMOGRAJ) cancelSamograj();
      switch(LOWORD(wp)){
      case ID_NEW:  newGame();  return 0;
      case ID_RESTART: restartCurrentGame(); return 0;
      case ID_PLAY_NUM: showPlayNumberDialog(hwnd); return 0;
      case ID_PLAY_WIN: playRandomWinningNumber(hwnd); return 0;
      case ID_PLAY_LOST: showPlayFailedDialog(hwnd); return 0;
      case ID_UNDO: doUndo();   return 0;
      case ID_REDO: doRedo();   return 0;
      case ID_FS:   toggleFS(); return 0;
      case ID_HELP:     showHelp(hwnd);     return 0;
      case ID_SETTINGS: showSettings(hwnd); return 0;
      case ID_SOLVER:   showSolver(hwnd); return 0;
      case ID_STATS:    showStats(hwnd);    return 0;
      case ID_SAVE_AS:   saveGameAs(hwnd);   return 0;
      case ID_LOAD_FROM: loadGameFrom(hwnd); return 0;
      case ID_SAMOGRAJ:
         toggleSamograj();
         if(g_samogranoActive) samogranoStep(); // kick off the first move now
         return 0;
      case ID_HINT:{
         if(g_animating || g_dealing || g_deal.busy()) return 0;
         playSound("podp",g_volume);
         if(g_hintList.empty() && !isBestMoveReady()){
            // No cached answer yet — defer instead of blocking on a
            // synchronous search: show "Myślę" and build the list once the
            // background precompute thread catches up (see tickThinking()).
            startThinking(PA_HINT);
            return 0;
         }
         performHintNow();
         return 0;}
      } break;

   case WM_PAINT:{
      PAINTSTRUCT ps;BeginPaint(hwnd,&ps);doPaint(hwnd);EndPaint(hwnd,&ps);return 0;}

   case WM_SIZE:
      if(g_hwnd && wp!=SIZE_MINIMIZED){
         int newW=LOWORD(lp), newH=HIWORD(lp);
         if(newW<=0 || newH<=0) return 0; // ignore degenerate sizes (0×0 at startup)

         if(g_d2dRT){D2D1_SIZE_U sz=D2D1::SizeU(LOWORD(lp),HIWORD(lp)>Layout::TOOLBAR_H?(UINT32)(HIWORD(lp)-Layout::TOOLBAR_H):1);if(FAILED(g_d2dRT->Resize(sz)))discardD2DRT();}
         // Resize child game window
         if(g_gameHwnd){
            int newW=LOWORD(lp), newH=HIWORD(lp);
            int gameH=newH-Layout::TOOLBAR_H;
            if(gameH<1)gameH=1;
            SetWindowPos(g_gameHwnd,nullptr,0,Layout::TOOLBAR_H,newW,gameH,SWP_NOZORDER|SWP_NOACTIVATE);
         }
         g_smoothActive=false;
         // Reset move label to default position on resize
         // (user can re-place it after resizing)
         if(g_moveLabelCustom){
            g_moveLabelCustom=false;
            g_moveLabelX=-1.f; g_moveLabelY=-1.f;
            saveSettings();
         }
         recalcLayout();
         snapColOverlaps();
         if(g_needsFirstLayout){
            g_needsFirstLayout=false;
            onStateChanged();
         } else {
            invalidateGame();
         }
      }
      return 0;

   case WM_LBUTTONDOWN: onLDown(LOWORD(lp),HIWORD(lp)); return 0;
   case WM_LBUTTONUP:   onLUp(LOWORD(lp),HIWORD(lp));   return 0;
   case WM_MOUSEMOVE:   onMouseMove(LOWORD(lp),HIWORD(lp)); return 0;

   case WM_TIMER:
      if(wp==TIMER_DEAL){
         auto done=g_deal.tick();
         for(auto& p : done) commitCardToBoard(p.card,p.col,p.cardIdx);
         if(!g_deal.busy()){
            KillTimer(hwnd,TIMER_DEAL); g_dealing=false; onStateChanged();
         } else { invalidateGame(); }
      } else if(wp==TIMER_FW){
         RECT rc;
         if(g_gameHwnd) GetClientRect(g_gameHwnd,&rc);
         else { GetClientRect(hwnd,&rc); rc.bottom-=Layout::TOOLBAR_H; }
         g_fw.tick(rc.right,rc.bottom);
         invalidateGame();
      } else if(wp==TIMER_SMOOTH){
         // Legacy path — now driven by PeekMessage loop; keep as no-op safety net
      } else if(wp==TIMER_HINT_OFF){
         // Auto-hide hint after 3 seconds
         KillTimer(hwnd,TIMER_HINT_OFF);
         g_hintActive=false; g_hintBlinking=false;
         markDirty(); invalidateGame();
      } else if(wp==TIMER_NOMOVES){
         KillTimer(hwnd,TIMER_NOMOVES);
         // Only show dialog if still in no-moves state and not dealing/animating
         if(g_noMoves && !g_dealing && !g_animating && !g_samogranoMarathon){
            showNoMovesDialog(hwnd);
         }
      } else if(wp==TIMER_BLINK){
         // Blink sequence: 3 pairs of (src visible / dst visible)
         if(g_hintBlinkCount < 6){
            g_hintShowSrc = (g_hintBlinkCount % 2 == 0);
            g_hintShowDst = (g_hintBlinkCount % 2 == 1);
            g_hintBlinkCount++;
            markDirty(); invalidateGame();
         } else {
            KillTimer(hwnd,TIMER_BLINK);
            g_hintBlinking=false;
            g_hintActive=true;
            g_hintShowSrc=true; g_hintShowDst=true;
            SetTimer(hwnd,TIMER_HINT_OFF,3000,nullptr);
            markDirty(); invalidateGame();
         }
      } else if(wp==TIMER_SECOND){
         if(g_gameStartTick>0 && !g_won && !g_noMoves && !g_timerPaused)
            g_gameSeconds=(int)((timeGetTime()-g_gameStartTick)/1000);
         invalidateGame();
         if(g_gameHwnd) UpdateWindow(g_gameHwnd);
      } else if(wp==TIMER_PULSE){
         int totalSteps = 3 * 8;
         if(g_reservePulseStep < totalSteps){
            int stepInPulse = g_reservePulseStep % 8;
            // Sine-based: smooth 0→1→0 over 8 steps
            float t = (float)stepInPulse / 7.f;  // 0..1
            float alpha = sinf(t * 3.14159f);     // sin(0..π) = 0→1→0
            g_reservePulseAlpha  = g_reservePulsing   ? alpha : 0.f;
            g_emptyColPulseAlpha = g_emptyColPulsing  ? alpha : 0.f;
            g_reservePulseStep++;
            invalidateGame();
         } else {
            KillTimer(hwnd,TIMER_PULSE);
            g_reservePulsing=false;
            g_emptyColPulsing=false;
            g_reservePulseAlpha=0.f;
            g_emptyColPulseAlpha=0.f;
            invalidateGame();
         }
      }
      return 0;

   case WM_GETMINMAXINFO:{
      MINMAXINFO* mmi=(MINMAXINFO*)lp;
      RECT wr={0,0,Layout::minPanelW(),Layout::minPanelH()};
      AdjustWindowRectEx(&wr,
         g_isFS?WS_POPUP:WS_OVERLAPPEDWINDOW,FALSE,0);
      mmi->ptMinTrackSize.x=wr.right-wr.left;
      mmi->ptMinTrackSize.y=wr.bottom-wr.top;
      return 0;}

   case WM_ACTIVATE:
      // Force full redraw when window is activated/deactivated (fixes artifacts)
      if(g_hwnd){ markDirty(); InvalidateRect(hwnd,nullptr,TRUE); }
      if(LOWORD(wp)==WA_INACTIVE){
         if(!g_timerPaused){ g_timerPaused=true; g_pauseStartTick=timeGetTime(); }
      } else {
         if(g_timerPaused){
            DWORD pausedMs=timeGetTime()-g_pauseStartTick;
            g_gameStartTick+=pausedMs; // shift start forward so elapsed time excludes the pause
            g_timerPaused=false;
         }
      }
      break;

   case WM_KEYDOWN:{
      DWORD vk=(DWORD)wp;
      if(wp==VK_ESCAPE&&g_isFS) toggleFS();
      // Ctrl+Z / Ctrl+Y always work regardless of bindings
      if(wp=='Z'&&(GetKeyState(VK_CONTROL)&0x8000)){ cancelSamograj(); doUndo(); }
      if(wp=='Y'&&(GetKeyState(VK_CONTROL)&0x8000)){ cancelSamograj(); doRedo(); }
      if(g_keys[KA_NEW].matches(vk))        { cancelSamograj(); newGame(); }
      if(g_keys[KA_HINT].matches(vk))       { cancelSamograj(); SendMessage(hwnd,WM_COMMAND,(WPARAM)ID_HINT,0); }
      if(g_keys[KA_UNDO].matches(vk))       { cancelSamograj(); doUndo(); }
      if(g_keys[KA_REDO].matches(vk))       { cancelSamograj(); doRedo(); }
      if(g_keys[KA_UNDO_DEAL].matches(vk))  { cancelSamograj(); doUndoDeal(); }
      if(g_keys[KA_REDO_DEAL].matches(vk))  { cancelSamograj(); doRedoDeal(); }
      if(g_keys[KA_AUTO].matches(vk))       doAutoMove(); // single AI move — same engine Samograj uses, doesn't interrupt it
      if(g_keys[KA_SAMOGRAJ].matches(vk)){
         toggleSamograj();
         if(g_samogranoActive) samogranoStep();
      }
      if(wp=='1'&&(GetKeyState(VK_SHIFT)&0x8000)){ // Shift+1 (!): Samograj bez końca
         toggleMarathon();
         if(g_samogranoActive) samogranoStep();
      }
      if(g_keys[KA_FULLSCREEN].matches(vk)) toggleFS();
      if(g_keys[KA_HELP].matches(vk))       showHelp(hwnd);
      if(wp=='S'){ cancelSamograj(); showStats(hwnd); }
      return 0;}

   case WM_DRAWITEM:
      drawIconButton((DRAWITEMSTRUCT*)lp);
      return TRUE;

   case WM_ERASEBKGND:{
      HDC hdc=(HDC)wp;
      // Fill toolbar area with system color; game area will be painted by doPaint
      RECT rc2; GetClientRect(hwnd,&rc2);
      rc2.bottom=Layout::TOOLBAR_H;
      FillRect(hdc,&rc2,(HBRUSH)(COLOR_BTNFACE+1));
      return 1;}
   case WM_DESTROY:
      saveWindowPlacement();
      saveStats();
      saveGameState();
      discardD2DRT();
      KillTimer(hwnd,TIMER_DEAL); KillTimer(hwnd,TIMER_FW); KillTimer(hwnd,TIMER_BLINK); KillTimer(hwnd,TIMER_SMOOTH); KillTimer(hwnd,TIMER_HINT_OFF); KillTimer(hwnd,TIMER_PULSE); KillTimer(hwnd,TIMER_SECOND);
      PostQuitMessage(0); return 0;
   }
   return DefWindowProcW(hwnd,msg,wp,lp);
}

// Builds/rotates the hint list and starts its preview animation. Split out
// of the ID_HINT handler in WndProc so the deferred "Myślę" path
// (tickThinking()) can call the exact same logic once the engine's answer
// is ready, without duplicating it.
static void performHintNow(){
   // Build hint list if empty, else rotate
   if(g_hintList.empty()){
      g_hintList.clear(); g_hintIndex=0;
      // The ranked beam is already sorted best-to-worst from ONE search, so
      // the hint list is just a walk down it applying the same business
      // filters the old per-attempt retry loop did — no need to re-search
      // once per accepted hint.
      //
      // BUGFIX: this used to `break` (stop scanning entirely) the moment it
      // hit a non-"sensible" (score<=0) candidate, on the assumption that
      // the ranked beam is sorted by immediate score so nothing later could
      // ever be positive either. It isn't sorted that way — it's sorted by
      // full-search cumulative score (immediate score is only a tie-break —
      // see rankCandidates()) — so the single objectively-best move often IS
      // something immediately costly (e.g. occupying an empty column costs
      // -500 in free-column mode) whose long-term payoff just isn't visible
      // one move at a time, while a perfectly good, immediately-positive
      // move sits right behind it in the list. Stopping there meant the hint
      // button could play its sound and then show nothing at all — this now
      // `continue`s past a non-sensible candidate instead, so the scan keeps
      // looking for the best move that IS positive to actually hint.
      // BUGFIX #2: the deal branch below used to be unconditional ("show it
      // whenever it's the first thing hit while the list is still empty"),
      // which combined with the `continue` fix above created a NEW problem:
      // on a board where every real move costs something (e.g. the only
      // legal moves all park a card on the one empty column, each around
      // -450), the scan now skips past all of them looking for a positive
      // one, finds none, and then unconditionally grabs the deal — even one
      // scoring far worse (e.g. -599, since dealing while a column sits
      // empty is itself penalized) than every move it just skipped. A deal
      // is only unconditionally right to show when it's the genuine
      // trivial-board mandatory deal (rankCandidates() forces score=0 for
      // it, and it's the ONLY candidate in `ranked` in that case) — every
      // other time it needs to clear the same "sensible" (score>0) bar as
      // any other move.
      auto buildHintList=[&](const std::vector<RankedMove>& ranked)->bool{
         bool mandatoryDeal = isTrivialBoard(g_game);
         for(auto& rm : ranked){
            const MoveHint& h = rm.move;
            if(h.fromIdx==-1){
               if(!mandatoryDeal && rm.score<=0) continue; // not sensible — keep scanning
               if(!g_hintList.empty()) break;
               g_hintList.push_back(h);
               break;
            }
            // Per spec: the hint cycle shows only "sensible" moves — score>0.
            if(rm.score<=0) continue;
            // If hint is col→empty, don't add more empty-col hints — one pulse covers all
            if(h.toType==LOC_COLUMN && h.toIdx>=0 && g_game.cols[h.toIdx].empty()){
               g_hintList.push_back(h);
               break; // one empty-col hint is enough, we'll pulse all empty cols
            }
            g_hintList.push_back(h);
         }
         return !g_hintList.empty();
      };
      std::vector<RankedMove> ranked = getRankedMovesFast();
      std::vector<RankedMove> full; // filled only if the fallback below runs
      bool haveFull=false;
      if(!buildHintList(ranked)){
         // Rare fallback: with 3+ empty columns, rankCandidates() short-
         // circuits to a SINGLE candidate for speed (see its comment) —
         // if that one candidate isn't "sensible" either, there's nothing
         // left in `ranked` to fall back to even with the fix above. Re-rank
         // once, bypassing that shortcut, so a hint can still surface a
         // genuinely sensible move if the full search finds one. Costs one
         // extra full search, but only in this specific, uncommon situation.
         if(!(ranked.size()==1 && ranked[0].move.fromIdx==-1)){
            full = g_game.getRankedMoves(20, true);
            haveFull=true;
            buildHintList(full);
         }
      }
      if(g_hintList.empty()){
         // Last resort: NOTHING scores as "sensible" anywhere (every legal
         // move currently costs something — e.g. the only free spot forces
         // every move to occupy it). Rather than leave the hint empty, or
         // let some arbitrary lower-ranked candidate slip through just
         // because it happened to be a deal (the bug that used to show a
         // deal scoring far WORSE than the moves already ruled out — see
         // the comment above buildHintList), fall back to the single
         // objectively-best move the search actually found — i.e. exactly
         // the move auto-move/Samograj would play here.
         const std::vector<RankedMove>& best = haveFull ? full : ranked;
         if(!best.empty()) g_hintList.push_back(best[0].move);
      }
   } else {
      g_hintIndex=(g_hintIndex+1)%(int)g_hintList.size();
   }
   if(!g_hintList.empty()){
      const MoveHint& h=g_hintList[g_hintIndex];
      // Reserve hint (deal from reserve): pulse reserve card
      if(h.fromIdx==-1){
         g_reservePulsing=true;
         g_reservePulseStep=0;
         g_reservePulseAlpha=0.f;
         SetTimer(g_hwnd,TIMER_PULSE,50,nullptr);
      // P3 hint (col→empty column): pulse all empty columns
      } else if(h.toType==LOC_COLUMN && h.toIdx>=0
                && g_game.cols[h.toIdx].empty()){
         g_emptyColPulsing=true;
         g_reservePulseStep=0;
         g_emptyColPulseAlpha=0.f;
         SetTimer(g_hwnd,TIMER_PULSE,50,nullptr);
      } else {
         // Animate card flying to destination and back
         std::vector<Card> moving;
         float sx,sy,ex,ey,ov;
         bool toFound=(h.toType==LOC_FOUNDATION);
         if(h.fromType==LOC_COLUMN){
            int fc=h.fromIdx, ci=h.fromCard;
            moving.assign(g_game.cols[fc].begin()+ci,g_game.cols[fc].end());
            POINT sp=g_layout.colPos(fc);
            int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
            ColOverlapInfo oi=calcColOverlap(fc,avH);
            sx=(float)sp.x;
            sy=colCardYExact(fc,ci,oi);
            ov=g_colDispOv[fc];
            if(toFound){
               POINT dp=g_layout.foundPos(h.toIdx);
               ex=(float)dp.x; ey=(float)dp.y;
            } else {
               int tc=h.toIdx;
               // ey computed in simulation block below (overwrites this)
               int dstIdx=(int)g_game.cols[tc].size();
               ex=(float)g_layout.colPos(tc).x;
               ey=(float)(g_layout.tableY+dstIdx*g_colDispOv[tc]); // fallback, overridden below
            }
         } else { // from foundation
            int f=h.fromIdx;
            moving={g_game.found[f].back()};
            POINT sp=g_layout.foundPos(f);
            sx=(float)sp.x; sy=(float)sp.y; ov=0;
            int tc=h.toIdx;
            int dstIdx=(int)g_game.cols[tc].size();
            POINT dp=cardPixelPos(tc,dstIdx);
            ex=(float)dp.x; ey=(float)dp.y;
            toFound=false;
         }
         if(!moving.empty()){
            // Compute dstOv using simulated column state after move.
            // ey uses current column render Y (what board actually shows) so
            // the animated cards land exactly where the board's last card ends.
            float dstOv = ov; // fallback
            if(!toFound && h.fromType==LOC_COLUMN){
               int tc=h.toIdx;
               int avH=g_layout.panelH-g_layout.tableY-Layout::MARGIN_BOT;
               // ey: Y just below current last card of dest column (as board renders it)
               {
                  ColOverlapInfo curInfo=calcColOverlap(tc,avH);
                  int dstIdx=(int)g_game.cols[tc].size();
                  if(dstIdx==0){
                     ey=(float)g_layout.tableY;
                  } else {
                     ey=colCardYExact(tc,dstIdx-1,curInfo)+(float)curInfo.seqOv;
                     ey=std::max(ey,(float)g_layout.tableY);
                  }
               }
               // dstOv: seqOv that would apply after move (simulated)
               for(auto& c:moving) g_game.cols[tc].push_back(c);
               ColOverlapInfo dstInfo=calcColOverlap(tc,avH);
               g_game.cols[tc].erase(
                  g_game.cols[tc].end()-(int)moving.size(),
                  g_game.cols[tc].end());
               dstOv=dstInfo.seqOv;
            }
            CardAnim a;
            a.cards=moving; a.sx=sx; a.sy=sy; a.ex=ex; a.ey=ey;
            a.srcOv=ov; a.dstOv=dstOv;
            a.arcH=0.f; a.dur=0;
            a.startTime=timeGetTime();
            a.toFound=toFound; a.isPreview=true; a.phase=0;
            g_cardAnims.push_back(a);
            g_previewAnimating=true;
            if(h.fromType==LOC_COLUMN){
               g_hideCol=h.fromIdx; g_hideFromIdx=h.fromCard;
            }
            markDirty();
         }
      }
   } else {
      playSound("nono",g_volume);
   }
   markDirty(); invalidateGame();
}

// ============================================================================
// WinMain
// ============================================================================
// WndProc for the game child window (D2D surface)
static LRESULT CALLBACK GameWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
   const int TH=Layout::TOOLBAR_H;
   // Mouse coordinates from child window are relative to child (Y=0 at top of game area)
   // onLDown/onLUp/onMouseMove expect coordinates relative to main window → add TH
   switch(msg){
   case WM_PAINT:{
      PAINTSTRUCT ps; BeginPaint(hwnd,&ps);
      doPaint(hwnd);
      EndPaint(hwnd,&ps); return 0;}
   case WM_ERASEBKGND: return 1;
   case WM_LBUTTONDOWN: onLDown(LOWORD(lp), (int)(short)HIWORD(lp)+TH); return 0;
   case WM_LBUTTONUP:   onLUp  (LOWORD(lp), (int)(short)HIWORD(lp)+TH); return 0;
   case WM_MOUSEMOVE:   onMouseMove(LOWORD(lp), (int)(short)HIWORD(lp)+TH); return 0;
   case WM_LBUTTONDBLCLK: PostMessageW(g_hwnd,WM_LBUTTONDBLCLK,wp,lp); return 0;
   }
   return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nShow){
   // Single-instance guard
   HANDLE hMutex=CreateMutexW(nullptr,TRUE,L"PasjansD_SingleInstance_v7");
   if(GetLastError()==ERROR_ALREADY_EXISTS){
      // Find the existing window and bring it to front
      HWND existing=FindWindowW(L"PasjansD7",nullptr);
      if(existing){
         if(IsIconic(existing)) ShowWindow(existing,SW_RESTORE);
         SetForegroundWindow(existing);
      }
      CloseHandle(hMutex);
      return 0;
   }

   timeBeginPeriod(1);

   CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

   GdiplusStartupInput gsi;ULONG_PTR gdipTok=0;GdiplusStartup(&gdipTok,&gsi,nullptr);
   if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&g_d2dFactory))){
      MessageBoxW(nullptr,L"Błąd Direct2D.",L"Błąd",MB_ICONERROR|MB_OK);return 1;}
   DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&g_dwFactory);
   CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g_wicFactory));
   WNDCLASSEXW wc={sizeof(wc)};
   wc.lpfnWndProc=WndProc;wc.hInstance=hInst;
   wc.lpszClassName=L"PasjansD7";
   wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
   wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
   wc.style=CS_HREDRAW|CS_VREDRAW;
   wc.hIcon=(HICON)LoadImageW(hInst,L"APPICON",IMAGE_ICON,
      GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
   if(!wc.hIcon)wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
   wc.hIconSm=(HICON)LoadImageW(hInst,L"APPICON",IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
   if(!RegisterClassExW(&wc)){
      MessageBoxW(nullptr,L"RegisterClassEx failed.",L"Błąd",MB_ICONERROR|MB_OK);return 1;}
   // Register child window class for D2D game area
   WNDCLASSEXW wg={sizeof(wg)};
   wg.lpfnWndProc=GameWndProc; wg.hInstance=hInst;
   wg.lpszClassName=L"PasjansD7Game";
   wg.hCursor=LoadCursor(nullptr,IDC_ARROW);
   wg.hbrBackground=nullptr; // D2D fills background
   wg.style=CS_HREDRAW|CS_VREDRAW;
   RegisterClassExW(&wg);
   HWND hwnd=CreateWindowExW(0,L"PasjansD7",L"Pasjans Dziadkowy",
      WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1060,780,
      nullptr,nullptr,hInst,nullptr);
   if(!hwnd){
      MessageBoxW(nullptr,L"CreateWindow failed.",L"Błąd",MB_ICONERROR|MB_OK);return 1;}
   // Restore saved window position/size before first show
   {
      std::wstring ini=getIniPath();
      wchar_t buf[128]={};
      GetPrivateProfileStringW(L"Window",L"Placement",L"",buf,128,ini.c_str());
      int l=0,t=0,r=0,b=0,cmd=SW_SHOWNORMAL;
      bool hasPlacement=(buf[0]!=0 &&
         swscanf_s(buf,L"%d,%d,%d,%d,%d",&l,&t,&r,&b,&cmd)==5);
      if(hasPlacement){
         // Verify the saved position is visible on at least one monitor
         RECT rc={l,t,r,b};
         bool onScreen=(MonitorFromRect(&rc,MONITOR_DEFAULTTONULL)!=nullptr);
         if(onScreen){
            WINDOWPLACEMENT wp={sizeof(wp)};
            wp.flags=0;
            wp.showCmd=SW_HIDE;  // don't show yet
            wp.rcNormalPosition={l,t,r,b};
            SetWindowPlacement(hwnd,&wp);
            ShowWindow(hwnd,(cmd==SW_MAXIMIZE)?SW_MAXIMIZE:SW_SHOWNORMAL);
         } else {
            ShowWindow(hwnd,SW_SHOWNORMAL);
         }
      } else {
         ShowWindow(hwnd,SW_SHOWNORMAL);
      }
   }
   UpdateWindow(hwnd);

   // PeekMessage loop: processes messages immediately without blocking,
   // allowing smooth animation ticks independently of WM_TIMER priority.
   // When idle and animation is active, tick every ~8ms using timeGetTime().
   MSG msg = {};
   DWORD lastAnimTick = timeGetTime();
   const DWORD ANIM_INTERVAL_MS = 8; // ~120fps animation ticks
   while(true){
      // Drain all pending messages first (non-blocking)
      bool hadMsg = false;
      while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){
         if(msg.message==WM_QUIT) goto done;
         TranslateMessage(&msg);
         DispatchMessage(&msg);
         hadMsg=true;
      }
      // Advance "Samograj" (self-play) one step, if active. A cheap no-op
      // whenever a deal/animation is already in flight or the engine is
      // still thinking (see samogranoStep()) — those resolve on a later tick.
      if(g_samogranoActive) samogranoStep();
      // Tick smooth overlap animation, card flight animations, and the
      // "Myślę" thinking pulse (all three want the same fast, low-latency
      // tick loop instead of waiting on WM_TIMER).
      bool anyAnim = g_smoothActive || g_animating || g_previewAnimating || g_thinking;
      if(anyAnim){
         DWORD now=timeGetTime();
         if(now-lastAnimTick>=ANIM_INTERVAL_MS){
            lastAnimTick=now;
            if(g_smoothActive && !tickSmoothAnim())
               g_smoothActive=false;
            if(g_animating || g_previewAnimating)
               tickCardAnims();
            if(g_thinking){
               tickThinking();
               invalidateGame(); // repaint so the pulse animates smoothly
            }
         }
         Sleep(1);
      } else {
         WaitMessage();
      }
   }
done:
   discardD2DRT();
   if(g_wicFactory){g_wicFactory->Release();g_wicFactory=nullptr;}
   if(g_dwFactory){g_dwFactory->Release();g_dwFactory=nullptr;}
   if(g_d2dFactory){g_d2dFactory->Release();g_d2dFactory=nullptr;}
   GdiplusShutdown(gdipTok);
   SoundSystem::instance().shutdown();
   CoUninitialize();
   timeEndPeriod(1);
   return (int)msg.wParam;
}
