// Builds the fireworks sounds from five recordings (decoded to wav by tools/mp3wav):
//   1 launch shot -> wystrzal1..3, 2a whistle -> lot1..3 (stretched to 1.5 s), 2b whistles -> lot4..5 (a single whistle each),
//   3 explosion -> wybuch1..3, 4 crackle -> iskrzenie (1.5 s).   Output: 32 kHz mono 16 bit, no fade-in, a fade-out at the end.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
typedef std::vector<float> Buf;
static const int OUT_SR=32000;
static bool readWav(const std::string& path,Buf& mono,int& sr){
   FILE* f=fopen(path.c_str(),"rb"); if(!f) return false;
   std::vector<uint8_t> b; { fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); b.resize(n); fread(b.data(),1,n,f); fclose(f); }
   int ch=*(uint16_t*)&b[22]; sr=*(uint32_t*)&b[24]; int bps=*(uint16_t*)&b[34];
   size_t n=(b.size()-44)/(ch*bps/8); mono.assign(n,0.f); const int16_t* s=(const int16_t*)&b[44];
   for(size_t i=0;i<n;i++){ float a=0; for(int c=0;c<ch;c++) a+=s[i*ch+c]/32768.f; mono[i]=a/ch; }
   return true;
}
static void writeWav(const std::string& path,const Buf& x){
   FILE* f=fopen(path.c_str(),"wb"); if(!f){ printf("CANNOT WRITE %s\n",path.c_str()); return; } uint32_t n=(uint32_t)x.size(), bytes=n*2, sr=OUT_SR, br=sr*2; uint16_t one=1,bits=16,blk=2,fmt=1; uint32_t sixteen=16, riff=36+bytes;
   fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&sixteen,4,1,f); fwrite(&fmt,2,1,f); fwrite(&one,2,1,f);
   fwrite(&sr,4,1,f); fwrite(&br,4,1,f); fwrite(&blk,2,1,f); fwrite(&bits,2,1,f); fwrite("data",1,4,f); fwrite(&bytes,4,1,f);
   for(float v:x){ int s=(int)lrintf(std::max(-1.f,std::min(1.f,v))*32767.f); int16_t t=(int16_t)s; fwrite(&t,2,1,f); }
   fclose(f);
}
// RBJ biquad
struct Bq{ double b0,b1,b2,a1,a2,x1=0,x2=0,y1=0,y2=0;
   void lp(double fs,double fc,double q=0.7071){ double w=2*M_PI*fc/fs,c=cos(w),s=sin(w),al=s/(2*q); double a0=1+al; b0=(1-c)/2/a0; b1=(1-c)/a0; b2=b0; a1=-2*c/a0; a2=(1-al)/a0; }
   void hp(double fs,double fc,double q=0.7071){ double w=2*M_PI*fc/fs,c=cos(w),s=sin(w),al=s/(2*q); double a0=1+al; b0=(1+c)/2/a0; b1=-(1+c)/a0; b2=b0; a1=-2*c/a0; a2=(1-al)/a0; }
   float p(float x){ double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1; x1=x; y2=y1; y1=y; return (float)y; } };
static Buf lowpass(const Buf& x,double fs,double fc,int passes=2){ Buf y=x; for(int k=0;k<passes;k++){ Bq q; q.lp(fs,fc); for(float& v:y) v=q.p(v); } return y; }
static Buf highpass(const Buf& x,double fs,double fc){ Buf y=x; Bq q; q.hp(fs,fc); for(float& v:y) v=q.p(v); return y; }
// plays x `speed` times faster (pitch x speed, duration / speed); the result is at the sample rate `srOut`
static Buf resample(const Buf& x,double srIn,double srOut,double speed=1.0){
   Buf src=x; double ratio=srIn/srOut*speed;                       // input samples per output sample
   if(ratio>1.0) src=lowpass(x,srIn,0.46*srIn/ratio);                // anti-aliasing
   size_t n=(size_t)(x.size()/ratio); Buf y(n);
   for(size_t i=0;i<n;i++){
      double p=i*ratio; long i1=(long)p; double t=p-i1;
      auto g=[&](long k){ return (k<0||k>=(long)src.size())?0.f:src[k]; };
      float p0=g(i1-1),p1=g(i1),p2=g(i1+1),p3=g(i1+2);
      y[i]=(float)(0.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t));
   }
   return y;
}
static float peakOf(const Buf& x){ float m=0; for(float v:x) m=std::max(m,fabsf(v)); return m; }
static void normalize(Buf& x,float peak){ float m=peakOf(x); if(m>0) for(float& v:x) v*=peak/m; }
static void trimStart(Buf& x,double frac){            // cut the near-silence before the sound starts
   float thr=peakOf(x)*(float)frac; size_t i=0; while(i<x.size()&&fabsf(x[i])<thr) i++;
   if(i>0) x.erase(x.begin(),x.begin()+i);
}
// start where the 10 ms rms first reaches `frac` of its maximum: a sound that swells in the source starts at a good level instead of fading in
static void trimStartEnv(Buf& x,double sr,double frac){
   size_t w=(size_t)(0.010*sr); std::vector<float> env; for(size_t i=0;i+w<=x.size();i+=w/2){ double a=0; for(size_t j=i;j<i+w;j++) a+=x[j]*x[j]; env.push_back((float)sqrt(a/w)); }
   float mx=*std::max_element(env.begin(),env.end()); size_t k=0; while(k<env.size()&&env[k]<mx*frac) k++;
   size_t cutAt=k*(w/2); if(cutAt>0) x.erase(x.begin(),x.begin()+cutAt);
}
static void fadeOut(Buf& x,double sec,double sr=OUT_SR){
   size_t n=std::min(x.size(),(size_t)(sec*sr)); for(size_t i=0;i<n;i++){ double t=(double)(i+1)/n; double g=0.5*(1+cos(M_PI*t)); x[x.size()-n+i]*=(float)g; }
}
static Buf cut(const Buf& x,double from,double len,double sr){ size_t a=(size_t)(from*sr), b=std::min(x.size(),a+(size_t)(len*sr)); return Buf(x.begin()+a,x.begin()+b); }
static Buf echo(const Buf& x,double sr,const std::vector<std::pair<double,float>>& taps,double tail){      // taps: (delay s, gain)
   size_t extra=(size_t)(tail*sr); Buf y(x.size()+extra,0.f); for(size_t i=0;i<x.size();i++) y[i]+=x[i];
   for(auto& t:taps){ size_t d=(size_t)(t.first*sr); for(size_t i=0;i<x.size();i++) if(i+d<y.size()) y[i+d]+=x[i]*t.second; }
   return y;
}
// Waveform-similarity overlap-add: changes the duration to outLen samples without changing the pitch
static Buf wsola(const Buf& x,size_t outLen,double sr){
   const size_t win=(size_t)(0.046*sr), hs=win/2, tol=(size_t)(0.012*sr); const double ratio=(double)outLen/x.size();
   Buf y(outLen+win,0.f), norm(outLen+win,0.f); Buf w(win); for(size_t i=0;i<win;i++) w[i]=(float)(0.5-0.5*cos(2*M_PI*(i+0.5)/win));
   long prev=0; bool first=true; size_t out=0;
   for(size_t k=0; out<outLen; k++, out+=hs){
      long target=(long)(out/ratio); long pos=target;
      if(!first){
         long nat=prev+(long)hs; double best=-1e30; long lo=std::max(0L,target-(long)tol), hi=std::min((long)x.size()-(long)win-1,target+(long)tol);
         for(long c=lo;c<=hi;c++){ double s=0; for(size_t i=0;i<win;i+=3){ long a=nat+(long)i; if(a<0||a>=(long)x.size()) continue; s+=x[a]*x[c+i]; } if(s>best){ best=s; pos=c; } }
      } else pos=std::max(0L,std::min((long)x.size()-(long)win-1,target));
      first=false; prev=pos;
      for(size_t i=0;i<win&&out+i<y.size();i++){ long a=pos+(long)i; float v=(a>=0&&a<(long)x.size())?x[a]:0.f; y[out+i]+=v*w[i]; norm[out+i]+=w[i]; }
   }
   Buf r(outLen); for(size_t i=0;i<outLen;i++) r[i]= norm[i]>1e-3f? y[i]/norm[i] : 0.f; return r;
}
static void info(const char* name,const Buf& x){
   double rms=0; for(float v:x) rms+=v*v; rms=sqrt(rms/x.size()); size_t a=0; while(a<x.size()&&fabsf(x[a])<0.01f) a++;
   printf("%-16s %.3f s  peak %.2f  rms %.3f  first sample >1%% at %.1f ms\n",name,x.size()/(double)OUT_SR,peakOf(x),rms,a*1000.0/OUT_SR);
}
int main(int argc,char** argv){
   if(argc<3){ printf("usage: sfxgen <dir with 1.wav 2a.wav 2b.wav 3.wav 4.wav> <out dir>\n"); return 1; }
   std::string in=argv[1], out=argv[2]; Buf w1,w2a,w2b,w3,w4; int s1,s2a,s2b,s3,s4;
   if(!readWav(in+"\\1.wav",w1,s1)||!readWav(in+"\\2a.wav",w2a,s2a)||!readWav(in+"\\2b.wav",w2b,s2b)||!readWav(in+"\\3.wav",w3,s3)||!readWav(in+"\\4.wav",w4,s4)){ printf("input missing\n"); return 2; }
   std::mt19937 rng(20261016);
   // ---- 1. the launch: a single shot with a light echo at the end
   {
      Buf base=w1; trimStart(base,0.03);
      struct V{ double speed; double lp; float peak; std::vector<std::pair<double,float>> taps; double tail; } vs[3]={
         {0.90,9000,0.88,{{0.26f,0.20f},{0.52f,0.09f}},0.55},
         {1.00,12000,0.90,{{0.20f,0.16f},{0.41f,0.07f}},0.45},
         {1.12,7000,0.86,{{0.31f,0.20f},{0.60f,0.10f}},0.65}};
      for(int i=0;i<3;i++){
         Buf y=resample(base,s1,OUT_SR,vs[i].speed); y=lowpass(y,OUT_SR,vs[i].lp,1);
         y=echo(y,OUT_SR,vs[i].taps,vs[i].tail); normalize(y,vs[i].peak); fadeOut(y,0.10);
         char nm[32]; sprintf(nm,"wystrzal%d",i+1); info(nm,y); writeWav(out+"\\"+nm+".wav",y);
      }
   }
   // ---- 2. the flight: whistles of 1.5 s
   {
      Buf base=w2a; trimStartEnv(base,s2a,0.50);
      const double speeds[3]={0.87,1.0,1.15};
      for(int i=0;i<3;i++){
         Buf y=resample(base,s2a,OUT_SR,speeds[i]);              // the pitch of this variant
         y=wsola(y,(size_t)(1.5*OUT_SR),OUT_SR);                 // always 1.5 s, the pitch kept
         normalize(y,0.55f); fadeOut(y,0.16);
         char nm[32]; sprintf(nm,"lot%d",i+1); info(nm,y); writeWav(out+"\\"+nm+".wav",y);
      }
      // two single whistles of 2b: the one starting at 0.8 s (about 1.8 kHz) and the one starting at 4.3 s (about 4 kHz)
      const double starts[2]={0.80,4.30};
      for(int i=0;i<2;i++){
         Buf seg=cut(w2b,starts[i],1.5,s2b); trimStart(seg,0.10);
         Buf y=resample(seg,s2b,OUT_SR,1.0); normalize(y,0.55f); fadeOut(y,0.18);
         char nm[32]; sprintf(nm,"lot%d",i+4); info(nm,y); writeWav(out+"\\"+nm+".wav",y);
      }
   }
   // ---- 3. the explosion: three different single bangs
   {
      struct V{ double speed; double hp; double lp; float peak; } vs[3]={{0.85,40,6500,0.95},{1.00,50,12000,0.95},{1.16,70,9000,0.95}};
      for(int i=0;i<3;i++){
         Buf base=w3; trimStart(base,0.03); Buf y=resample(base,s3,OUT_SR,vs[i].speed); y=highpass(y,OUT_SR,vs[i].hp); y=lowpass(y,OUT_SR,vs[i].lp,1);
         normalize(y,vs[i].peak); fadeOut(y,0.18);
         char nm[32]; sprintf(nm,"wybuch%d",i+1); info(nm,y); writeWav(out+"\\"+nm+".wav",y);
      }
   }
   // ---- 4. the crackle: the original start, then more of the same crackle (grains of the original) up to 1.5 s
   {
      Buf src=resample(w4,s4,OUT_SR,1.0);
      Buf head=cut(src,0.12,0.30,OUT_SR);                          // the loud part of the original
      const size_t N=(size_t)(1.5*OUT_SR); Buf y(N+4096,0.f);
      for(size_t i=0;i<head.size()&&i<N;i++) y[i]=head[i];
      Buf pool=cut(src,0.10,0.34,OUT_SR); const size_t gl=(size_t)(0.055*OUT_SR);
      std::uniform_real_distribution<double> U(0,1);
      for(double t=0.22; t<1.5; t+=0.028+0.020*U(rng)){
         double env=std::max(0.0,1.0-(t-0.22)/1.30); env=0.20+0.80*env*env;       // the crackle thins out towards the end
         size_t at=(size_t)(t*OUT_SR), from=(size_t)(U(rng)*(pool.size()-gl-1)); float g=(float)((0.45+0.55*U(rng))*env);
         for(size_t i=0;i<gl&&at+i<y.size();i++){ float hann=(float)(0.5-0.5*cos(2*M_PI*(i+0.5)/gl)); y[at+i]+=pool[from+i]*hann*g; }
      }
      y.resize(N); y=highpass(y,OUT_SR,500); normalize(y,0.60f); fadeOut(y,0.30);
      info("iskrzenie",y); writeWav(out+"\\iskrzenie.wav",y);
   }
   return 0;
}
