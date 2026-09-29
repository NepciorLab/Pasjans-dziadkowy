// replay.cpp - parses a v9-v12 .dat save, reconstructs the move-by-move
// history from its undo stack, and for each move reports:
//   - how many legal moves the engine saw at that point (branching factor)
//   - where the human's actual move ranks in the engine's OWN full ranking
//     (getRankedMoves()), and how far its score trails the engine's #1 pick
//   - how the branching factor changed as a direct result of that move
// This is read-only analysis: it never plays anything for real, just replays
// recorded board snapshots and asks the live engine what IT would have done.
#include "game.h"
#include <cstdio>
#include <vector>
#include <algorithm>
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;

static bool g_free=false;

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;

static bool readVec(FILE* f, std::vector<Card>& v){
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
struct Snap { std::vector<Card> cols[NUM_COLS]; std::vector<Card> found[NUM_FOUND]; std::vector<Card> reserve; bool isDealBoundary=false; };
static bool readSnap(FILE* f, Snap& s, bool hasTag){
   for(int i=0;i<NUM_COLS;i++)  if(!readVec(f,s.cols[i]))  return false;
   for(int i=0;i<NUM_FOUND;i++) if(!readVec(f,s.found[i])) return false;
   if(!readVec(f,s.reserve)) return false;
   if(hasTag){ BYTE db=0; if(fread(&db,1,1,f)!=1) return false; s.isDealBoundary=(db!=0); }
   return true;
}
static GameState toGame(const Snap& s){
   GameState g;
   for(int i=0;i<NUM_COLS;i++)  g.cols[i]=s.cols[i];
   for(int i=0;i<NUM_FOUND;i++) g.found[i]=s.found[i];
   g.reserve=s.reserve;
   return g;
}
static int totalCards(const Snap& s){
   int n=(int)s.reserve.size();
   for(int i=0;i<NUM_COLS;i++) n+=(int)s.cols[i].size();
   for(int i=0;i<NUM_FOUND;i++) n+=(int)s.found[i].size();
   return n;
}
static void progress(const Snap& s, int& kings, int& placed){
   kings=0; placed=0;
   for(int f=0;f<NUM_FOUND;f++){ placed+=(int)s.found[f].size(); if(!s.found[f].empty()&&s.found[f].back().rank==King) kings++; }
   for(int c=0;c<NUM_COLS;c++){
      if(s.cols[c].empty()) continue;
      if(s.cols[c][0].rank!=King) continue;
      bool ok=true;
      for(size_t i=0;i+1<s.cols[c].size();i++){
         if((int)s.cols[c][i].rank!=(int)s.cols[c][i+1].rank+1 || s.cols[c][i].isRed()==s.cols[c][i+1].isRed()){ ok=false; break; }
      }
      if(ok){ kings++; placed+=(int)s.cols[c].size(); }
   }
}

// Classifies the transition from `a` to `b` as one MoveHint-shaped move.
// Returns false (unrecognised / a genuine deal — reserve shrank) when it
// isn't a single discrete table move; `isDeal` is set for that case.
struct Move { LocType fromType; int fromIdx; int fromCard; LocType toType; int toIdx; int count; };
static bool classify(const Snap& a, const Snap& b, Move& mv, bool& isDeal){
   isDeal=false;
   if(b.reserve.size() < a.reserve.size()){ isDeal=true; return false; }
   // column -> foundation (single card)
   for(int f=0;f<NUM_FOUND;f++){
      if(b.found[f].size()==a.found[f].size()+1){
         Card moved=b.found[f].back();
         for(int c=0;c<NUM_COLS;c++){
            if(a.cols[c].size()==b.cols[c].size()+1 && !a.cols[c].empty() && a.cols[c].back()==moved){
               // verify everything else is unchanged
               bool same=true;
               for(int cc=0;cc<NUM_COLS;cc++) if(cc!=c && a.cols[cc].size()!=b.cols[cc].size()) same=false;
               for(int ff=0;ff<NUM_FOUND;ff++) if(ff!=f && a.found[ff].size()!=b.found[ff].size()) same=false;
               if(same){ mv={LOC_COLUMN,c,(int)b.cols[c].size(),LOC_FOUNDATION,f,1}; return true; }
            }
         }
      }
   }
   // foundation -> column (single card)
   for(int f=0;f<NUM_FOUND;f++){
      if(a.found[f].size()==b.found[f].size()+1){
         Card moved=a.found[f].back();
         for(int c=0;c<NUM_COLS;c++){
            if(b.cols[c].size()==a.cols[c].size()+1 && !b.cols[c].empty() && b.cols[c].back()==moved){
               bool same=true;
               for(int cc=0;cc<NUM_COLS;cc++) if(cc!=c && a.cols[cc].size()!=b.cols[cc].size()) same=false;
               for(int ff=0;ff<NUM_FOUND;ff++) if(ff!=f && a.found[ff].size()!=b.found[ff].size()) same=false;
               if(same){ mv={LOC_FOUNDATION,f,0,LOC_COLUMN,c,1}; return true; }
            }
         }
      }
   }
   // column -> column (one or more cards)
   for(int fc=0;fc<NUM_COLS;fc++){
      int an=(int)a.cols[fc].size(), bn=(int)b.cols[fc].size();
      if(bn>=an) continue; // must shrink
      int moved=an-bn;
      for(int tc=0;tc<NUM_COLS;tc++){
         if(tc==fc) continue;
         int atn=(int)a.cols[tc].size(), btn=(int)b.cols[tc].size();
         if(btn!=atn+moved) continue;
         bool match=true;
         for(int i=0;i<moved;i++) if(!(a.cols[fc][bn+i]==b.cols[tc][atn+i])) { match=false; break; }
         if(!match) continue;
         bool same=true;
         for(int cc=0;cc<NUM_COLS;cc++) if(cc!=fc && cc!=tc && a.cols[cc].size()!=b.cols[cc].size()) same=false;
         for(int ff=0;ff<NUM_FOUND;ff++) if(a.found[ff].size()!=b.found[ff].size()) same=false;
         if(same){ mv={LOC_COLUMN,fc,bn,LOC_COLUMN,tc,moved}; return true; }
      }
   }
   return false;
}
static std::wstring cardName(const Card& c){
   const wchar_t* R[]={L"",L"A",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"10",L"J",L"Q",L"K"};
   const wchar_t* S[]={L"H",L"D",L"C",L"S"};
   return std::wstring(R[c.rank])+S[c.suit];
}

int main(int argc,char**argv){
   const char* path = argc>1?argv[1]:"replay.dat";
   FILE* f=nullptr; fopen_s(&f,path,"rb");
   if(!f){ printf("cannot open %s\n",path); return 1; }
   char magic[4]={}; fread(magic,1,4,f);
   BYTE ver=0; fread(&ver,1,1,f);
   printf("magic=%.4s ver=%d\n",magic,ver);
   if(memcmp(magic,"PASJ",4)!=0 || ver<1 || ver>12){ printf("bad format\n"); return 1; }
   Snap live;
   for(int i=0;i<NUM_COLS;i++)  readVec(f,live.cols[i]);
   for(int i=0;i<NUM_FOUND;i++) readVec(f,live.found[i]);
   readVec(f,live.reserve);
   DWORD mc=0; if(ver>=2) fread(&mc,4,1,f);
   BYTE freeMode=0; if(ver>=3) fread(&freeMode,1,1,f);
   BYTE winCounted=0; if(ver>=4) fread(&winCounted,1,1,f);
   BYTE noMovesReached=0; if(ver>=7) fread(&noMovesReached,1,1,f);
   BYTE noMovesDialogShown=0; if(ver>=8) fread(&noMovesDialogShown,1,1,f);
   BYTE statsExcluded=0; if(ver>=9) fread(&statsExcluded,1,1,f);
   BYTE outcomeMode=0; DWORD outcomeMoves=0, outcomeSecs=0;
   if(ver>=10){ fread(&outcomeMode,1,1,f); fread(&outcomeMoves,4,1,f); fread(&outcomeSecs,4,1,f); }
   Snap initialDeal;
   if(ver>=9) readSnap(f,initialDeal,true);
   DWORD gameNum=0; if(ver>=11) fread(&gameNum,4,1,f);
   DWORD secs=0; if(ver>=5) fread(&secs,4,1,f);
   std::vector<Snap> undo, redo;
   if(ver>=6){
      if(ver>=12){ WORD un=0; fread(&un,2,1,f); for(int i=0;i<un;i++){ Snap s; readSnap(f,s,ver>=9); undo.push_back(s); } }
      else       { BYTE un=0; fread(&un,1,1,f); for(int i=0;i<un;i++){ Snap s; readSnap(f,s,ver>=9); undo.push_back(s); } }
      if(ver>=12){ WORD rn=0; fread(&rn,2,1,f); for(int i=0;i<rn;i++){ Snap s; readSnap(f,s,ver>=9); redo.push_back(s); } }
      else       { BYTE rn=0; fread(&rn,1,1,f); for(int i=0;i<rn;i++){ Snap s; readSnap(f,s,ver>=9); redo.push_back(s); } }
   }
   fclose(f);
   printf("moveCount=%lu freeMode=%d winCounted=%d gameNumber=%lu elapsedSecs=%lu\n",mc,freeMode,winCounted,gameNum,secs);
   printf("undo entries=%zu redo entries=%zu live-total-cards=%d\n",undo.size(),redo.size(),totalCards(live));
   int lk,lp; progress(live,lk,lp);
   printf("live: kings=%d placed=%d\n",lk,lp);
   if(!undo.empty()){ int k0,p0; progress(undo[0],k0,p0); printf("undo[0]: kings=%d placed=%d totalCards=%d\n",k0,p0,totalCards(undo[0])); }

   g_free = (freeMode!=0);
   GameState::s_freeColMode=&g_free;

   // chronological board list: undo[0..N-1] then live (the state AFTER the last move)
   std::vector<Snap> chain=undo;
   chain.push_back(live);
   int n=(int)chain.size();
   printf("\n=== %d move(s) reconstructed ===\n\n", n-1);

   for(int i=0;i+1<n;i++){
      const Snap& a=chain[i]; const Snap& b=chain[i+1];
      Move mv{}; bool isDeal=false;
      bool ok=classify(a,b,mv,isDeal);
      GameState ga=toGame(a);
      auto cands=ga.allCandidateMoves();
      int branchBefore=(int)cands.size();
      GameState gb=toGame(b);
      int branchAfter=(int)gb.allCandidateMoves().size();
      int ka,pa,kb,pb; progress(a,ka,pa); progress(b,kb,pb);

      if(isDeal){
         printf("#%3d DEAL z rezerwy            branch %3d -> %3d (%+d)  placed %d->%d kings %d->%d\n",
            i+1, branchBefore, branchAfter, branchAfter-branchBefore, pa,pb,ka,kb);
         continue;
      }
      if(!ok){
         printf("#%3d ??? nie rozpoznano ruchu (branch %d->%d)\n", i+1, branchBefore, branchAfter);
         continue;
      }
      // Describe the move
      std::wstring who, what;
      if(mv.fromType==LOC_COLUMN){
         const Card& head=a.cols[mv.fromIdx][mv.fromCard];
         wchar_t buf[64];
         if(mv.toType==LOC_COLUMN) swprintf(buf,64,L"kol.%d -> kol.%d (%d kart, glowa %ls)",mv.fromIdx+1,mv.toIdx+1,mv.count,cardName(head).c_str());
         else                      swprintf(buf,64,L"kol.%d -> STOS (%ls)",mv.fromIdx+1,cardName(head).c_str());
         what=buf;
      } else {
         const Card& c=a.found[mv.fromIdx].back();
         wchar_t buf[64]; swprintf(buf,64,L"STOS -> kol.%d (%ls) [BUFOR]",mv.toIdx+1,cardName(c).c_str());
         what=buf;
      }

      // Rank the human's actual move within the engine's OWN full ranking at this position.
      auto ranked = ga.getRankedMoves(200,true); // bypass the empty-col short-circuit so we see the true full ranking
      int rank=-1; int topScore = ranked.empty()?0:ranked.front().score; int humanScore=0;
      for(size_t r=0;r<ranked.size();r++){
         const MoveHint& rm=ranked[r].move;
         bool match=false;
         if(mv.fromType==LOC_COLUMN && rm.fromType==LOC_COLUMN && rm.fromIdx==mv.fromIdx && rm.fromCard==mv.fromCard &&
            ((mv.toType==LOC_COLUMN && rm.toType==LOC_COLUMN && rm.toIdx==mv.toIdx) ||
             (mv.toType==LOC_FOUNDATION && rm.toType==LOC_FOUNDATION))) match=true;
         if(mv.fromType==LOC_FOUNDATION && rm.fromType==LOC_FOUNDATION && rm.fromIdx==mv.fromIdx &&
            rm.toType==LOC_COLUMN && rm.toIdx==mv.toIdx) match=true;
         if(match){ rank=(int)r; humanScore=ranked[r].score; break; }
      }
      wprintf(L"#%3d %-40ls branch %3d->%3d (%+d)  placed %d->%d kings %d->%d  ",
         i+1, what.c_str(), branchBefore, branchAfter, branchAfter-branchBefore, pa,pb,ka,kb);
      if(rank<0) printf("  [silnik NIE rozwaza tego ruchu w ogole!]\n");
      else if(rank==0) printf("  ruch #1 wg silnika (%d kandydatow)\n",(int)ranked.size());
      else printf("  ruch #%d/%d wg silnika (wynik %d vs najlepszy %d, strata %d)\n",rank+1,(int)ranked.size(),humanScore,topScore,topScore-humanScore);
   }
   return 0;
}
