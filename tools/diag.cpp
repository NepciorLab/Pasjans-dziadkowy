#include "solver.h"
#include <cstdio>
#include <chrono>
bool* GameState::s_freeColMode = nullptr;
std::array<int,NUM_COLS>* GameState::s_realArtificialSince = nullptr;
int* GameState::s_realMoveCounter = nullptr;
std::array<int,56>* GameState::s_realFoundSentAt = nullptr;
int* GameState::s_searchDepthFree = nullptr;
int* GameState::s_searchDepthKing = nullptr;
static void dump(solver::Board B, const std::vector<solver::Mv>& moves){
   for(auto& mv: moves) B.apply(mv);
   for(int c=0;c<NUM_COLS;c++){
      printf("col%d (%zu):",c,B.g.cols[c].size());
      for(auto& k: B.g.cols[c]) printf(" %s",k.imgKey().c_str());
      printf("\n");
   }
   for(int f=0;f<NUM_FOUND;f++) printf("found%d (%zu): %s\n",f,B.g.found[f].size(), B.g.found[f].empty()?"-":B.g.found[f].back().imgKey().c_str());
   printf("reserve dealt: %d / %zu\n",B.rpos,B.g.reserve.size());
   int kk,pl; solver::progressOf(B.g,kk,pl);
   printf("kings=%d placed=%d\n",kk,pl);
   std::vector<solver::Mv> cand; solver::generate(B,cand);
   std::stable_sort(cand.begin(),cand.end(),[](const solver::Mv&a,const solver::Mv&b){return a.score>b.score;});
   printf("legal moves (%zu):\n",cand.size());
   for(auto& m: cand) printf("  type=%d a=%d b=%d ci=%d score=%d\n",m.type,m.a,m.b,m.ci,m.score);
}
int main(int argc,char**argv){
   unsigned int seed=(unsigned int)strtoull(argv[1],nullptr,10);
   bool fm = argc>2 && std::string(argv[2])=="free";
   int seconds = argc>3?atoi(argv[3]):60;
   int threads = argc>4?atoi(argv[4]):solver::defaultThreads();
   GameState g; g.newGame(seed);
   solver::Progress P; std::atomic<bool> cancel{false};
   auto r=solver::solveDeal(g,fm,P,cancel,GetTickCount64()+seconds*1000ULL,threads);
   printf("solved=%d attempts=%d nodes=%lld bestEffortLen=%zu bestPotential=%d\n",r.solved,P.attemptsStarted.load(),P.totalNodes.load(),r.bestEffort.size(),r.bestPotential);
   if(r.solved){ printf("moves=%zu\n",r.moves.size()); return 0; }
   solver::Board B; B.g=g.boardOnly(); B.g.reserve=g.reserve; B.rpos=0; B.freeMode=fm;
   dump(B,r.bestEffort);
}
