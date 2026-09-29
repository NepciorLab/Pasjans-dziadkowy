#include "solver.h"
#include <cstdio>
#include <chrono>
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;
int main(int argc,char**argv){
   int games=argc>1?atoi(argv[1]):10; bool fm=argc>2&&std::string(argv[2])=="free"; int seed0=argc>3?atoi(argv[3]):1; double limit=argc>4?atof(argv[4]):20;
   static bool g_free; g_free=fm; GameState::s_freeColMode=&g_free;
   int solved=0; double tsum=0;
   for(int gi=0;gi<games;gi++){
      GameState g; g.newGame(seed0+gi);
      solver::Progress P; auto t0=std::chrono::steady_clock::now();
      int k=0; bool ok=false; std::vector<solver::Mv> mv;
      for(;;k++){
         double el=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(); if(el>limit) break;
         auto r=solver::runAttempt(g,fm,k,solver::budgetForAttempt(k),P,[&]{ return std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()>limit; });
         if(r.solved){ok=true;mv=r.moves;break;}
      }
      double el=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
      size_t before=mv.size(); double ts=0; if(ok){ auto t1=std::chrono::steady_clock::now(); if(!getenv("NOSHORT")) solver::shortenLine(g,fm,mv,[]{return false;}); ts=std::chrono::duration<double>(std::chrono::steady_clock::now()-t1).count(); printf("  shorten %zu -> %zu in %.1fs\n",before,mv.size(),ts); }
      // verify by replay with the real GameState rules
      bool verified=false;
      if(ok){ GameState s=g; s.boardOnly(); GameState r=g; bool bad=false; 
         size_t idx=0; for(auto&m:mv){ idx++; if(m.type==0){ if(m.ci>=(int)r.cols[m.a].size()||!r.canDropOnCol(r.cols[m.a][m.ci],m.b)){bad=true;break;} MoveHint h; h.valid=true;h.fromType=LOC_COLUMN;h.toType=LOC_COLUMN;h.fromIdx=m.a;h.fromCard=m.ci;h.toIdx=m.b; r.applyHint(h);} 
           else if(m.type==1){ Card c=r.cols[m.a].back(); int f=m.b; if(!r.canDropOnFound(c,f)){bad=true;break;} r.found[f].push_back(c); r.cols[m.a].pop_back(); }
           else if(m.type==2){ Card c=r.found[m.a].back(); if(!r.canDropOnCol(c,m.b)){bad=true;break;} r.cols[m.b].push_back(c); r.found[m.a].pop_back(); }
           else { if(r.reserve.empty()){bad=true;break;} r.dealReserve(); } }
         verified=!bad && r.checkWin(); if(!verified){ auto&m=mv[idx-1]; printf("  VERIFY FAIL at move %zu of %zu bad=%d type=%d a=%d b=%d ci=%d\n",idx,mv.size(),(int)bad,(int)m.type,(int)m.a,(int)m.b,(int)m.ci); if(m.type==0){ printf("   src size %zu, dst size %zu\n",r.cols[m.a].size(),r.cols[m.b].size()); if(m.ci<(int)r.cols[m.a].size()){ Card h=r.cols[m.a][m.ci]; printf("   head %s dst top %s\n",h.imgKey().c_str(), r.cols[m.b].empty()?"(empty)":r.cols[m.b].back().imgKey().c_str()); } } if(m.type==1){ printf("   col size %zu slot size %zu\n",r.cols[m.a].size(),r.found[m.b].size()); if(!r.cols[m.a].empty()) printf("   card %s\n",r.cols[m.a].back().imgKey().c_str()); } } }
      if(getenv("DUMP")&&ok){ for(size_t i=0;i<mv.size();i++) if(i>=(getenv("FROM")?(size_t)atoi(getenv("FROM")):0)&&i<(getenv("TO")?(size_t)atoi(getenv("TO")):80)) printf(" %zu:t%d a%d b%d ci%d sc%d |",i,(int)mv[i].type,(int)mv[i].a,(int)mv[i].b,(int)mv[i].ci,mv[i].score); printf("\n"); }
      printf("seed %d: %s attempts=%d moves=%zu nodes=%lld bestKings=%d placed=%d time=%.1fs verified=%d\n",seed0+gi,ok?"SOLVED":"no",k+1,mv.size(),P.totalNodes.load(),P.bestKings.load(),P.bestPlaced.load(),el,(int)verified);
      if(ok){solved++;tsum+=el;}
   }
   printf("solved %d/%d\n",solved,games);
}
