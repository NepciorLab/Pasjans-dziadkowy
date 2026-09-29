// sim.cpp - replicates main.cpp's auto-play decision loop headlessly to find why it stops.
#include "game.h"
#include <cstdio>
#include <map>
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;
static bool g_free=false; static int dFree=4,dKing=5;
static GameState g;
static int autoH[20],autoHead=0,autoCnt=0; static uint64_t bsH[20]; static int bsHead=0,bsCnt=0;
static int stall=0,maxF=-1; static std::array<int,NUM_COLS> realArt; static int realCtr=0; static std::array<int,56> foundSent;
static std::vector<MoveHint> plan; static bool planValid=false;
static int fcount(){int n=0;for(int f=0;f<NUM_FOUND;f++)n+=g.found[f].size();return n;}
static void noteF(){int fc=fcount(); if(maxF<0){maxF=fc;return;} if(fc>maxF){maxF=fc;stall=0;} else stall++;}
static void clearH(){autoHead=autoCnt=0;bsHead=bsCnt=0;stall=0;maxF=-1;realArt.fill(-1);realCtr=0;foundSent.fill(-1);}
static bool inAuto(int k){for(int i=0;i<autoCnt;i++) if(autoH[(autoHead-1-i+20)%20]==k) return true; return false;}
static void pushAuto(int k){autoH[autoHead]=k;autoHead=(autoHead+1)%20;if(autoCnt<20)autoCnt++;}
static bool inBs(uint64_t h){for(int i=0;i<bsCnt;i++) if(bsH[(bsHead-1-i+20)%20]==h) return true; return false;}
static void pushBs(uint64_t h){bsH[bsHead]=h;bsHead=(bsHead+1)%20;if(bsCnt<20)bsCnt++;}
static int cc(const Card&c){return (int)c.suit*13+(int)c.rank;}
static int enc(int c,int f,int t){return c*10000+f*100+t;}
static void updReal(const MoveHint&h){ int ply=++realCtr; realArt=nextArtificialSince(g,realArt,h,ply); for(int c=0;c<NUM_COLS;c++) if(realArt[c]>=0&&ply-realArt[c]>20) realArt[c]=-1;
  if(h.fromType==LOC_COLUMN&&h.toType==LOC_FOUNDATION){ foundSent[GameState::cardIndex(g.cols[h.fromIdx][h.fromCard])]=ply; } }
static bool isBuf(const MoveHint&h){ if(h.fromType==LOC_FOUNDATION&&h.toType==LOC_COLUMN) return true; if(g_free&&h.fromType==LOC_COLUMN&&h.toType==LOC_COLUMN&&g.cols[h.toIdx].empty()) return true; return false; }
// returns 0 moved, 1 stopped
static int step(std::string& why){
   if(stall>=50){why="stall50";return 1;}
   MoveHint h; bool used=planValid&&!plan.empty();
   if(used){ h=plan.front(); bool ok=h.valid; if(ok){ if(h.fromType==LOC_COLUMN) ok=h.fromIdx>=0&&h.fromIdx<NUM_COLS&&h.fromCard>=0&&h.fromCard<(int)g.cols[h.fromIdx].size(); else ok=h.fromIdx>=0&&h.fromIdx<NUM_FOUND&&!g.found[h.fromIdx].empty(); } if(ok&&h.toType==LOC_COLUMN) ok=h.toIdx>=0&&h.toIdx<NUM_COLS; if(!ok){plan.clear();planValid=false;used=false;} }
   if(!used){
      auto fresh=[&](const MoveHint&c)->bool{ Card mc; int ef,et; if(c.fromType==LOC_COLUMN){ if(c.fromIdx<0||c.fromIdx>=NUM_COLS||c.fromCard<0||c.fromCard>=(int)g.cols[c.fromIdx].size()) return false; mc=g.cols[c.fromIdx][c.fromCard]; ef=c.fromIdx;} else { if(c.fromIdx<0||c.fromIdx>=NUM_FOUND||g.found[c.fromIdx].empty()) return false; mc=g.found[c.fromIdx].back(); ef=-10-c.fromIdx;} et=(c.toType==LOC_COLUMN)?c.toIdx:-10-c.toIdx; int id=cc(mc); if(inAuto(enc(id,ef,et))||inAuto(enc(id,et,ef))) return false; GameState s=g.boardOnly(); s.applyHint(c); return !inBs(boardCanonicalHash(s)); };
      auto ranked=g.getRankedMoves(); int chosen=-1;
      for(size_t i=0;i<ranked.size();i++){ const MoveHint&c=ranked[i].move; if(c.fromIdx==-1){chosen=i;break;} if(fresh(c)){chosen=i;break;} }
      if(chosen>=0){h=ranked[chosen].move;plan=ranked[chosen].pv;}
      else { std::set<std::pair<int,int>> tried; for(auto&r:ranked) tried.insert(moveExclusionKey(r.move)); h.valid=false;
         for(int a=0;a<30;a++){ int sc=0; std::vector<MoveHint> pl; MoveHint c=g.getBestMove(tried,&sc,&pl); if(!c.valid) break; if(!pl.empty()) pl.erase(pl.begin()); if(c.fromIdx==-1){h=c;plan=pl;break;} if(fresh(c)){h=c;plan=pl;break;} tried.insert(moveExclusionKey(c)); } }
      if(!h.valid && getenv("RELAX")){
         auto bfresh=[&](const MoveHint&c)->bool{ GameState s=g.boardOnly(); s.applyHint(c); return !inBs(boardCanonicalHash(s)); };
         for(size_t i=0;i<ranked.size()&&!h.valid;i++) if(ranked[i].move.fromIdx!=-1 && bfresh(ranked[i].move)){ h=ranked[i].move; plan=ranked[i].pv; }
         if(!h.valid){ std::set<std::pair<int,int>> tried; for(auto&r:ranked) tried.insert(moveExclusionKey(r.move));
            for(int a=0;a<30;a++){ int sc=0; std::vector<MoveHint> pl; MoveHint c=g.getBestMove(tried,&sc,&pl); if(!c.valid) break; if(!pl.empty()) pl.erase(pl.begin()); if(c.fromIdx!=-1&&bfresh(c)){h=c;plan=pl;break;} tried.insert(moveExclusionKey(c)); } }
      }
      if(!h.valid){ if(getenv("DIAG")&&g.hasTableOrFoundationMove()){ printf("--- stuck board (anyMove=Y) ranked=%zu\n",ranked.size()); for(int c=0;c<NUM_COLS;c++){printf(" col%d:",c); for(auto&k:g.cols[c]) printf(" %s",k.imgKey().c_str()); printf("\n");} auto all=g.allCandidateMoves(); for(auto&m:all){ printf("  cand %d/%d->%d/%d ci=%d fromIdx=%d",(int)m.fromType,m.fromIdx,(int)m.toType,m.toIdx,m.fromCard,m.fromIdx); Card mc=(m.fromType==LOC_COLUMN&&m.fromIdx>=0)?g.cols[m.fromIdx][m.fromCard]:Card{}; int ef=m.fromType==LOC_COLUMN?m.fromIdx:-10-m.fromIdx, et=m.toType==LOC_COLUMN?m.toIdx:-10-m.toIdx; bool a=inAuto(enc(cc(mc),ef,et)),r=inAuto(enc(cc(mc),et,ef)); GameState s2=g.boardOnly(); s2.applyHint(m); bool b=inBs(boardCanonicalHash(s2)); printf(" hist=%d rev=%d boardRepeat=%d\n",a,r,b);} printf("  legal-any-move list (isPointless filtered by generator): ");
 for(int fc=0;fc<NUM_COLS;fc++){ if(g.cols[fc].empty())continue; int sl=g.seqLenFromBottom(fc),si=(int)g.cols[fc].size()-sl; for(int p=0;p<sl;p++){int ci=si+p; for(int tc=0;tc<NUM_COLS;tc++){ if(tc==fc)continue; if(!g.canDropOnCol(g.cols[fc][ci],tc))continue; printf("[%d.%d->%d%s]",fc,ci,tc,g.isPointlessColToCol(fc,ci,tc)?"P":"");}}} printf("\n"); }
   why = std::string("nocand ranked=")+std::to_string(ranked.size())+" reserve="+std::to_string(g.reserve.size())+" anyMove="+(g.hasTableOrFoundationMove()?"Y":"N"); return 1; }
   }
   if(h.fromIdx==-1){ clearH(); planValid=false; plan.clear(); noteF(); g.dealReserve(); return 0; }
   bool buf=isBuf(h);
   int ef,et; Card mc;
   if(h.fromType==LOC_COLUMN&&h.toType==LOC_FOUNDATION){ int col=h.fromIdx,ci=h.fromCard; if(ci!=(int)g.cols[col].size()-1){why="badfound";return 1;} mc=g.cols[col][ci]; int f=g.bestFoundSlot(mc); if(f<0){why="nofslot";return 1;} pushAuto(enc(cc(mc),col,-10-f)); updReal(h); g.found[f].push_back(mc); g.cols[col].pop_back(); }
   else if(h.fromType==LOC_COLUMN&&h.toType==LOC_COLUMN){ int fc=h.fromIdx,ci=h.fromCard,tc=h.toIdx; std::vector<Card> mv(g.cols[fc].begin()+ci,g.cols[fc].end()); if(mv.empty()||!g.canDropOnCol(mv[0],tc)){why="badcolmove";return 1;} pushAuto(enc(cc(mv[0]),fc,tc)); updReal(h); for(auto&c:mv) g.cols[tc].push_back(c); g.cols[fc].erase(g.cols[fc].begin()+ci,g.cols[fc].end()); }
   else if(h.fromType==LOC_FOUNDATION&&h.toType==LOC_COLUMN){ int f=h.fromIdx,tc=h.toIdx; mc=g.found[f].back(); if(!g.canDropOnCol(mc,tc)){why="badbuf";return 1;} pushAuto(enc(cc(mc),-10-f,tc)); updReal(h); g.cols[tc].push_back(mc); g.found[f].pop_back(); }
   else {why="unknownmove";return 1;}
   pushBs(boardCanonicalHash(g)); noteF();
   if(used) plan.erase(plan.begin()); planValid = buf && !plan.empty();
   return 0;
}
int main(int argc,char**argv){
   int games=argc>1?atoi(argv[1]):50; g_free = argc>2 && std::string(argv[2])=="free"; int startSeed=argc>3?atoi(argv[3]):1;
   GameState::s_freeColMode=&g_free; GameState::s_realArtificialSince=&realArt; GameState::s_realMoveCounter=&realCtr; GameState::s_realFoundSentAt=&foundSent; GameState::s_searchDepthFree=&dFree; GameState::s_searchDepthKing=&dKing;
   std::map<std::string,int> reasons; int wins=0;
   for(int gi=0;gi<games;gi++){
      g.newGame(startSeed+gi); clearH(); plan.clear(); planValid=false;
      std::string why; int moves=0; bool won=false;
      for(;moves<3000;moves++){ if(g.checkWin()){won=true;break;} if(step(why)) break; }
      if(won) wins++;
      else { std::string k=why.substr(0,why.find(' ')); reasons[k]++; }
      if(getenv("PERGAME")) printf("PG %d %s %d\n",startSeed+gi,won?"W":"L",moves);
   }
   printf("games=%d wins=%d\n",games,wins); for(auto&r:reasons) printf("  %s: %d\n",r.first.c_str(),r.second);
}






