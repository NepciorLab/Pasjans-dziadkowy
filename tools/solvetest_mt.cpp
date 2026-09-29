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
   int games=atoi(argv[1]); bool fm=std::string(argv[2])=="free"; int seed0=atoi(argv[3]); int limitSec=atoi(argv[4]); int thr=argc>5?atoi(argv[5]):solver::defaultThreads();
   int solved=0; double tsum=0;
   for(int gi=0;gi<games;gi++){
      GameState g; g.newGame(seed0+gi); solver::Progress P; std::atomic<bool> cancel{false};
      auto t0=std::chrono::steady_clock::now();
      auto r=solver::solveDeal(g,fm,P,cancel,GetTickCount64()+limitSec*1000ULL,thr);
      double el=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
      printf("seed %d: %s moves=%zu attempts=%d nodes=%lld kings=%d placed=%d prefix=%d time=%.1fs\n",seed0+gi,r.solved?"SOLVED":"no",r.moves.size(),P.attemptsStarted.load(),P.totalNodes.load(),P.bestKings.load(),P.bestPlaced.load(),P.bestPrefixLen.load(),el);
      fflush(stdout); if(r.solved){solved++;tsum+=el;}
   }
   printf("solved %d/%d threads=%d\n",solved,games,thr);
}

