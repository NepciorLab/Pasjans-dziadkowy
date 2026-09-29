// bench.cpp - headless benchmark / regression digest for the AI engine.
// usage: bench [games] [maxSteps] [free|king] [depth]
#include "game.h"
#include <cstdio>
#include <chrono>
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;
int main(int argc,char**argv){
   int games = argc>1?atoi(argv[1]):5;
   int maxSteps = argc>2?atoi(argv[2]):60;
   bool freeMode = !(argc>3 && std::string(argv[3])=="king");
   int depth = argc>4?atoi(argv[4]):4;
   static bool fm; fm=freeMode; GameState::s_freeColMode=&fm;
   static int d; d=depth; GameState::s_searchDepthFree=&d; GameState::s_searchDepthKing=&d;
   using clk=std::chrono::steady_clock;
   double total=0,worst=0; long steps=0; int wins=0; long fcards=0; uint64_t digest=1469598103934665603ULL;
   auto mix=[&](uint64_t v){ digest=(digest^v)*1099511628211ULL; };
   for(int g=1; g<=games; g++){
      GameState gs; gs.newGame(1000+g);
      for(int s=0;s<maxSteps;s++){
         auto t0=clk::now();
         auto ranked=gs.getRankedMoves();
         double dt=std::chrono::duration<double>(clk::now()-t0).count();
         total+=dt; if(dt>worst) worst=dt; steps++;
         if(ranked.empty()) break;
         for(auto&r:ranked){ mix(r.move.fromType); mix(r.move.toType); mix((uint64_t)(r.move.fromIdx+50)); mix(r.move.fromCard); mix(r.move.toIdx); mix((uint64_t)(r.score+100000));
            for(auto&m:r.pv){ mix((uint64_t)(m.fromIdx+50)); mix(m.fromCard); mix(m.toIdx);} }
         if(!gs.applyHint(ranked[0].move)) break;
         if(ranked[0].move.fromIdx!=-1) gs.autoCompleteFoundations();
         if(gs.checkWin()){ wins++; break; }
      }
      for(int f=0;f<NUM_FOUND;f++) fcards+=(long)gs.found[f].size();
   }
   printf("wins=%d fcards=%ld ",wins,fcards); printf("games=%d steps=%ld total=%.3fs avg=%.2fms worst=%.1fms digest=%016llx\n",games,steps,total,total/steps*1000,worst*1000,(unsigned long long)digest);
}

