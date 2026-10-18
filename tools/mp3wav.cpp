// mp3 -> 16-bit PCM wav using Media Foundation; prints the RMS envelope
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
int wmain(int argc,wchar_t** argv){
   if(argc<3){ wprintf(L"usage: in.mp3 out.wav\n"); return 1; }
   CoInitializeEx(nullptr,COINIT_MULTITHREADED); MFStartup(MF_VERSION);
   IMFSourceReader* rd=nullptr;
   HRESULT hr=MFCreateSourceReaderFromURL(argv[1],nullptr,&rd);
   if(FAILED(hr)){ printf("open failed %08lx\n",hr); return 2; }
   IMFMediaType* mt=nullptr; MFCreateMediaType(&mt);
   mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio); mt->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
   hr=rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,mt); mt->Release();
   if(FAILED(hr)){ printf("set type failed %08lx\n",hr); return 3; }
   IMFMediaType* ot=nullptr; rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,&ot);
   UINT32 ch=0,sr=0,bps=0; ot->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch); ot->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&sr); ot->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bps);
   printf("channels %u rate %u bits %u\n",ch,sr,bps);
   std::vector<uint8_t> pcm;
   for(;;){
      DWORD flags=0; IMFSample* s=nullptr;
      hr=rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&s);
      if(FAILED(hr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM)){ if(s) s->Release(); break; }
      if(!s) continue;
      IMFMediaBuffer* b=nullptr; s->ConvertToContiguousBuffer(&b);
      BYTE* p=nullptr; DWORD len=0; b->Lock(&p,nullptr,&len); pcm.insert(pcm.end(),p,p+len); b->Unlock(); b->Release(); s->Release();
   }
   double secs=(double)pcm.size()/(ch*bps/8)/sr; printf("duration %.2f s, %zu bytes\n",secs,pcm.size());
   const int16_t* sm=(const int16_t*)pcm.data(); size_t n=pcm.size()/2; size_t win=(size_t)(sr*ch/4);
   for(size_t i=0,k=0;i<n;i+=win,k++){ double a=0; size_t e=std::min(n,i+win); for(size_t j=i;j<e;j++) a+=(double)sm[j]*sm[j]; printf("%.2f:%.0f ",k*0.25,std::sqrt(a/(e-i))); if(k%8==7) printf("\n"); }
   printf("\n");
   FILE* f=nullptr; _wfopen_s(&f,argv[2],L"wb");
   auto w32=[&](uint32_t v){ fwrite(&v,4,1,f); }; auto w16=[&](uint16_t v){ fwrite(&v,2,1,f); };
   fwrite("RIFF",1,4,f); w32((uint32_t)(36+pcm.size())); fwrite("WAVEfmt ",1,8,f); w32(16); w16(1); w16((uint16_t)ch); w32(sr); w32(sr*ch*bps/8); w16((uint16_t)(ch*bps/8)); w16((uint16_t)bps);
   fwrite("data",1,4,f); w32((uint32_t)pcm.size()); fwrite(pcm.data(),1,pcm.size(),f); fclose(f);
   return 0;
}
