#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <map>

// Load PNG card images from embedded Win32 RCDATA resources.
// Resource names: "CARD_AH", "CARD_KS", "CARD_B1", etc.
// getScaled(key, w, h) returns a pre-scaled bitmap cached until invalidateScaled() is called.

class CardImages {
public:
   static CardImages& instance() {
      static CardImages inst;
      return inst;
   }

   void init(HINSTANCE hInst) {
      m_hInst = hInst;
   }

   // Returns original (unscaled) bitmap
   Gdiplus::Bitmap* get(const std::string& key) {
      auto it = m_cache.find(key);
      if(it != m_cache.end()) return it->second;
      Gdiplus::Bitmap* bmp = load(key);
      m_cache[key] = bmp;
      return bmp;
   }

   // Returns bitmap pre-scaled to (w x h). Cache is keyed on (key, w, h).
   // Call invalidateScaled() when card size changes (window resize).
   Gdiplus::Bitmap* getScaled(const std::string& key, int w, int h) {
      if(w<=0 || h<=0) return nullptr;
      // Check scaled cache
      auto it = m_scaledCache.find(key);
      if(it != m_scaledCache.end()
         && it->second.w == w && it->second.h == h)
         return it->second.bmp;

      // Need to (re)scale
      Gdiplus::Bitmap* src = get(key);
      if(!src) return nullptr;

      // Create new bitmap at target size using high-quality bicubic
      Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
      if(!dst || dst->GetLastStatus() != Gdiplus::Ok){
         delete dst; return src; // fallback to original
      }
      Gdiplus::Graphics g2(dst);
      g2.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
      g2.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
      g2.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
      g2.DrawImage(src, 0, 0, w, h);

      // Store in scaled cache
      ScaledEntry e; e.bmp=dst; e.w=w; e.h=h;
      auto old = m_scaledCache.find(key);
      if(old != m_scaledCache.end()) delete old->second.bmp;
      m_scaledCache[key] = e;
      return dst;
   }

   // Call when window is resized — clears the scaled cache so next draw re-scales
   void invalidateScaled() {
      for(auto& kv : m_scaledCache) delete kv.second.bmp;
      m_scaledCache.clear();
   }

   ~CardImages() {
      for(auto& kv : m_cache) delete kv.second;
      for(auto& kv : m_scaledCache) delete kv.second.bmp;
   }

private:
   CardImages() : m_hInst(nullptr) {}
   CardImages(const CardImages&) = delete;

   Gdiplus::Bitmap* load(const std::string& key) {
      if(!m_hInst) return nullptr;
      std::string resName = "CARD_" + key;
      HRSRC hRes = FindResourceA(m_hInst, resName.c_str(), (LPCSTR)RT_RCDATA);
      if(!hRes) return nullptr;
      DWORD   size  = SizeofResource(m_hInst, hRes);
      HGLOBAL hLoad = LoadResource(m_hInst, hRes);
      if(!hLoad || size == 0) return nullptr;
      void* pSrc = LockResource(hLoad);
      if(!pSrc) return nullptr;
      HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
      if(!hMem) return nullptr;
      void* pDst = GlobalLock(hMem);
      if(!pDst) { GlobalFree(hMem); return nullptr; }
      memcpy(pDst, pSrc, size);
      GlobalUnlock(hMem);
      IStream* stream = nullptr;
      if(FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
         GlobalFree(hMem); return nullptr;
      }
      Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
      stream->Release();
      if(!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
         delete bmp; return nullptr;
      }
      return bmp;
   }

   struct ScaledEntry { Gdiplus::Bitmap* bmp; int w, h; };

   HINSTANCE m_hInst;
   std::map<std::string, Gdiplus::Bitmap*> m_cache;
   std::map<std::string, ScaledEntry>      m_scaledCache;
};

inline Gdiplus::Bitmap* GetCard(const std::string& key) {
   return CardImages::instance().get(key);
}
inline Gdiplus::Bitmap* GetCardScaled(const std::string& key, int w, int h) {
   return CardImages::instance().getScaled(key, w, h);
}
