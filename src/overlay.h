#pragma once
// ============================================================================
// Overlays: Settings, Statistics and Help, drawn INSIDE the game window in the style of Garibaldka
// (a dimmed table, a dark panel with a gold border and translucent rounded buttons — the panel is blue
// here, the table colour is the one chosen in the settings). Immediate mode: every control registers its
// clickable area while it is drawn, the next mouse click looks it up.
//
// This header is part of main.cpp's single translation unit: it is included right before drawScene() and
// uses the globals declared above that point (settings, statistics, sounds, key bindings, the D2D target).
// ============================================================================

enum { OV_NONE=0, OV_SETTINGS, OV_STATS, OV_HELP };
struct OvHit{ float x,y,w,h; int kind,a; };
enum { OH_CLOSE=1, OH_GROUP, OH_BG, OH_FREE, OH_HILITE, OH_DEPTH, OH_ANIM, OH_UPDATES, OH_VOL, OH_SBROWSE, OH_SPLAY, OH_SMUTE,
       OH_SDEF, OH_KEY, OH_KEYCLR, OH_KEYDEF, OH_STATRESET };

static int   g_ov=OV_NONE;                       // which overlay is open
static std::vector<OvHit> g_ovHits;
static float g_ovW=800.f, g_ovH=600.f;           // size of the game area as last drawn
static int   g_ovGroup=0;                        // settings group: 0 general, 1 sounds, 2 keys
static int   g_ovCapture=-1;                     // key being captured: action*2+slot (-1 none)
static bool  g_ovDragVol=false, g_ovDragAnim=false;
static float g_ovVolX=0, g_ovVolW=1, g_ovAnimX=0, g_ovAnimW=1;     // slider geometry of the last draw
static float g_ovScroll=0, g_ovContentH=0, g_ovBuiltW=-1;           // help window
static bool  g_ovDragThumb=false; static float g_ovGrabDy=0;
static bool  g_ovSwallowUp=false;     // the button-up of a click the overlay consumed (even one that closed it) must not reach the table

static bool ovActive(){ return g_ov!=OV_NONE; }
static void ovClose();
static void invalidateGame();
static std::wstring buildDateText();

// ---- drawing helpers (the target is the game window's Direct2D target)
static void ovRect(float x,float y,float w,float h,float rad,float r,float g,float b,float a,bool fill=true,float stroke=1.5f){
   ID2D1SolidColorBrush* br=nullptr; g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(!br) return;
   auto rr=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),rad,rad);
   if(fill) g_d2dRT->FillRoundedRectangle(rr,br); else g_d2dRT->DrawRoundedRectangle(rr,br,stroke);
   br->Release();
}
static void ovText(const std::wstring& s,float x,float y,float w,float h,float px,float r,float g,float b,float a,bool bold=false,
                   DWRITE_TEXT_ALIGNMENT al=DWRITE_TEXT_ALIGNMENT_CENTER){
   if(!g_dwFactory||s.empty()) return;
   IDWriteTextFormat* f=nullptr;
   g_dwFactory->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
   if(!f) return;
   f->SetTextAlignment(al); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
   f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
   ID2D1SolidColorBrush* br=nullptr; g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(br){ g_d2dRT->DrawText(s.c_str(),(UINT32)s.size(),f,D2D1::RectF(x,y,x+w,y+h),br); br->Release(); }
   f->Release();
}
static void ovWrap(const std::wstring& s,float x,float y,float w,float h,float px,float r,float g,float b,float a,bool bold=false){
   if(!g_dwFactory||s.empty()) return;
   IDWriteTextFormat* f=nullptr;
   g_dwFactory->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
   if(!f) return;
   f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
   ID2D1SolidColorBrush* br=nullptr; g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(br){ g_d2dRT->DrawText(s.c_str(),(UINT32)s.size(),f,D2D1::RectF(x,y,x+w,y+h),br); br->Release(); }
   f->Release();
}
static void ovHit(float x,float y,float w,float h,int kind,int a=0){ g_ovHits.push_back({x,y,w,h,kind,a}); }
static std::wstring ovFileBase(const std::wstring& p){ size_t k=p.find_last_of(L"\\/"); return k==std::wstring::npos?p:p.substr(k+1); }

// Panel colours (blue; Garibaldka's are green)
static const float PNL_R=0.05f, PNL_G=0.11f, PNL_B=0.27f;
static const float LIT_R=0.95f, LIT_G=0.97f, LIT_B=1.00f;        // body text
static const float DIM_R=0.78f, DIM_G=0.85f, DIM_B=0.95f;        // notes, subtitles

// The dimmed table, a panel with title and subtitle and the close button.
static void ovPanel(float px,float py,float pw,float ph,const std::wstring& title,const std::wstring& subtitle){
   ovRect(0,0,g_ovW,g_ovH,0,0,0,0,0.65f);                                       // dim the table
   ovRect(px+5,py+8,pw,ph,16,0,0,0,0.40f);                                      // shadow
   ovRect(px,py,pw,ph,16,PNL_R,PNL_G,PNL_B,0.99f);                              // the panel
   ovRect(px,py,pw,ph,16,0.95f,0.80f,0.30f,0.85f,false,2.f);                    // gold border
   ovText(title,px+28,py+12,pw-120,40,30,1.f,0.86f,0.25f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   ovText(subtitle,px+29,py+48,pw-120,22,15,DIM_R,DIM_G,DIM_B,0.95f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   ovRect(px+24,py+76,pw-48,1.5f,0,1,1,1,0.20f);
   float cbx=px+pw-52, cby=py+16;
   ovRect(cbx,cby,34,34,8,1,1,1,0.14f); ovRect(cbx,cby,34,34,8,1,1,1,0.35f,false,1.f);
   g_renderer.drawLine(cbx+11,cby+11,cbx+23,cby+23,2.2f,255,255,255,230);
   g_renderer.drawLine(cbx+23,cby+11,cbx+11,cby+23,2.2f,255,255,255,230);
   ovHit(cbx,cby,34,34,OH_CLOSE);
}
static void ovButton(float x,float y,float w,float h,const std::wstring& label,int kind,int a,bool active=false,bool enabled=true,float px=14.f){
   ovRect(x,y,w,h,8,1,1,1,enabled?(active?0.26f:0.14f):0.05f);
   if(active) ovRect(x,y,w,h,8,1.f,0.86f,0.30f,0.95f,false,2.f); else ovRect(x,y,w,h,8,1,1,1,enabled?0.38f:0.12f,false,1.f);
   ovText(label,x+4,y,w-8,h,px,1,1,1,enabled?1.f:0.4f,active);
   if(enabled) ovHit(x,y,w,h,kind,a);
}
static void ovCheck(float x,float y,float w,const std::wstring& label,bool on,int kind,int a=0){
   ovRect(x,y+4,20,20,5,1,1,1,0.14f); ovRect(x,y+4,20,20,5,1,1,1,0.50f,false,1.3f);
   if(on){ g_renderer.drawLine(x+4,y+14,x+9,y+19,2.6f,255,220,80,255); g_renderer.drawLine(x+9,y+19,x+17,y+9,2.6f,255,220,80,255); }
   ovText(label,x+32,y,w-32,28,15,LIT_R,LIT_G,LIT_B,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   ovHit(x,y,w,28,kind,a);
}
static void ovHeading(const std::wstring& t,float x,float y,float w){
   ovText(t,x,y,w,28,20,1.f,0.86f,0.30f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   ovRect(x,y+31,w,1.2f,0,1.f,0.86f,0.30f,0.35f);
}
static void ovNote(const std::wstring& t,float x,float y,float w,float h){ ovWrap(t,x,y,w,h,13.5f,DIM_R,DIM_G,DIM_B,1.f); }
static void ovLabel(const std::wstring& t,float x,float y,float w,float h=36){ ovText(t,x,y,w,h,15,LIT_R,LIT_G,LIT_B,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING); }
static void ovKnob(float x,float y){
   D2D1_ELLIPSE e=D2D1::Ellipse(D2D1::Point2F(x,y),10.f,10.f);
   ID2D1SolidColorBrush* br=nullptr; g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1.f,0.93f,0.55f,1.f),&br);
   if(br){ g_d2dRT->FillEllipse(e,br); br->Release(); }
}

// The toolbar and the buttons are tinted from the table colour: repaint them after it changed.
static void toolbarRefresh();

static void ovOpen(int which){ if(g_ov==which){ ovClose(); return; } g_ov=which; g_ovCapture=-1; g_ovDragVol=g_ovDragAnim=g_ovDragThumb=false; g_ovScroll=0; g_ovBuiltW=-1; invalidateGame(); }
static void ovClose(){
   if(g_ov==OV_NONE) return;
   g_ov=OV_NONE; g_ovCapture=-1;
   if(g_ovDragVol||g_ovDragAnim||g_ovDragThumb){ g_ovDragVol=g_ovDragAnim=g_ovDragThumb=false; ReleaseCapture(); }
   SoundSystem::instance().fadeOutAll(200);
   invalidateGame();
}

// ============================================================================
// Settings
// ============================================================================
static const wchar_t* OV_GROUPS[3]={L"Ogólne",L"Dźwięki",L"Klawisze"};

static void settingsOverlayDraw(){
   const float pw=std::min(880.f,g_ovW-30.f), ph=std::min(640.f,g_ovH-30.f), px=std::floor((g_ovW-pw)/2.f), py=std::floor((g_ovH-ph)/2.f);
   ovPanel(px,py,pw,ph,L"Ustawienia",L"Zmiany działają od razu i są zapamiętywane w pliku pasjans.ini");
   const float nx=px+24, ny=py+92, nw=176;
   for(int i=0;i<3;i++){
      float y=ny+i*52.f; bool act=(g_ovGroup==i);
      ovRect(nx,y,nw,44,8,1,1,1,act?0.22f:0.07f);
      if(act) ovRect(nx,y,nw,44,8,1.f,0.86f,0.30f,0.95f,false,2.f); else ovRect(nx,y,nw,44,8,1,1,1,0.20f,false,1.f);
      ovText(OV_GROUPS[i],nx+14,y,nw-20,44,16,1,1,1,1.f,act,DWRITE_TEXT_ALIGNMENT_LEADING);
      ovHit(nx,y,nw,44,OH_GROUP,i);
   }
   ovRect(nx+nw+14,ny,1.2f,ph-92-24,0,1,1,1,0.18f);
   const float cx=nx+nw+30, cw=px+pw-28-cx; float y=ny;
   ovHeading(OV_GROUPS[g_ovGroup],cx,y,cw); y+=48;
   switch(g_ovGroup){
   case 0:{                                                                         // General
      ovLabel(L"Kolor tła",cx,y,200,28); y+=30;
      { const float gap=8.f, sw=std::min(46.f,(cw-(BG_COUNT-1)*gap)/BG_COUNT);
        for(int i=0;i<BG_COUNT;i++){
           float sx=cx+i*(sw+gap);
           ID2D1SolidColorBrush* br=nullptr; g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(BG_COLORS[i].r/255.f,BG_COLORS[i].g/255.f,BG_COLORS[i].b/255.f),&br);
           if(br){ g_d2dRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx,y,sx+sw,y+sw),8,8),br); br->Release(); }
           if(i==g_bgIndex) ovRect(sx-2,y-2,sw+4,sw+4,9,1.f,0.86f,0.30f,1.f,false,3.f); else ovRect(sx,y,sw,sw,8,1,1,1,0.35f,false,1.f);
           ovHit(sx,y,sw,sw,OH_BG,i);
        }
        y+=sw+14; }
      ovLabel(L"Wolne miejsce na pustej kolumnie",cx,y,cw,28); y+=30;
      ovButton(cx,y,210,36,L"Tylko król (standardowe)",OH_FREE,0,!g_freeColMode);
      ovButton(cx+218,y,170,36,L"Dowolna karta",OH_FREE,1,g_freeColMode);
      y+=46;
      ovLabel(L"Zaznaczanie sekwencji",cx,y,cw,28); y+=30;
      { const float bw=(cw-16.f)/3.f;
        ovButton(cx,y,bw,36,L"Nie zaznaczaj",OH_HILITE,0,g_moveHighlightMode==0);
        ovButton(cx+bw+8,y,bw,36,L"Obrys wokół sekwencji",OH_HILITE,1,g_moveHighlightMode==1);
        ovButton(cx+2*(bw+8),y,bw,36,L"Przyciemnij niemożliwe",OH_HILITE,2,g_moveHighlightMode==2); }
      y+=46;
      ovLabel(L"Głębokość przewidywań SI (dowolna karta)",cx,y,cw-210,36);
      { float bx=cx+cw-190;
        ovButton(bx,y,36,36,L"−",OH_DEPTH,0,false,g_searchDepthFree>1,18);
        ovText(std::to_wstring(g_searchDepthFree),bx+38,y,50,36,16,1,1,1,1.f,true);
        ovButton(bx+90,y,36,36,L"+",OH_DEPTH,1,false,g_searchDepthFree<MAX_SEARCH_DEPTH,18); }
      y+=42;
      ovLabel(L"Głębokość przewidywań SI (tylko król)",cx,y,cw-210,36);
      { float bx=cx+cw-190;
        ovButton(bx,y,36,36,L"−",OH_DEPTH,2,false,g_searchDepthKing>1,18);
        ovText(std::to_wstring(g_searchDepthKing),bx+38,y,50,36,16,1,1,1,1.f,true);
        ovButton(bx+90,y,36,36,L"+",OH_DEPTH,3,false,g_searchDepthKing<MAX_SEARCH_DEPTH,18); }
      y+=46;
      ovLabel(L"Prędkość animacji",cx,y,200,28); y+=30;
      g_ovAnimX=cx+40.f; g_ovAnimW=cw-80.f;
      ovRect(g_ovAnimX,y+11,g_ovAnimW,6,3,1,1,1,0.20f);
      ovRect(g_ovAnimX,y+11,g_ovAnimW*(g_animSpeedStep+2)/4.f,6,3,1.f,0.86f,0.30f,0.95f);
      for(int k=0;k<5;k++){
         float sx=g_ovAnimX+g_ovAnimW*k/4.f; bool on=(k==g_animSpeedStep+2);
         ovRect(sx-1.5f,y+6,3,16,1.5f,1,1,1,0.45f);
         ovText(animSpeedLabel(k-2),sx-62,y+28,124,22,12.5f,on?1.f:0.8f,on?0.93f:0.88f,on?0.45f:0.95f,1.f,on);
      }
      ovKnob(g_ovAnimX+g_ovAnimW*(g_animSpeedStep+2)/4.f,y+14);
      ovHit(g_ovAnimX-24,y-4,g_ovAnimW+48,48,OH_ANIM);
      y+=58;
      ovCheck(cx,y,cw,L"Sprawdzaj aktualizacje przy starcie gry",g_checkUpdatesOnStart,OH_UPDATES); y+=34;
      ovNote(std::wstring(L"Wersja ")+APP_VERSION+L", zbudowana "+buildDateText()+L".",cx+32,y,cw-32,22);
      break;}
   case 1:{                                                                         // Sounds
      ovLabel(L"Głośność",cx,y,100,30);
      g_ovVolX=cx+110; g_ovVolW=cw-110-70;
      ovRect(g_ovVolX,y+12,g_ovVolW,6,3,1,1,1,0.20f);
      ovRect(g_ovVolX,y+12,g_ovVolW*g_volume,6,3,1.f,0.86f,0.30f,0.95f);
      ovKnob(g_ovVolX+g_ovVolW*g_volume,y+15);
      ovHit(g_ovVolX-10,y,g_ovVolW+20,30,OH_VOL);
      ovText(std::to_wstring((int)(g_volume*100.f+0.5f))+L"%",cx+cw-60,y,60,30,15,1,1,1,1.f,false,DWRITE_TEXT_ALIGNMENT_TRAILING);
      y+=44;
      ovText(L"Własne dźwięki (WAV lub MP3)",cx,y,cw,26,15,1.f,0.86f,0.30f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING); y+=32;
      for(int i=0;i<SOUND_UI_COUNT;i++){
         const bool mut=SoundSystem::instance().isMuted(i); const std::wstring& cp=SoundSystem::instance().customPath(i);
         ovText(SOUND_LABELS[i],cx,y,190,32,14.5f,LIT_R,LIT_G,LIT_B,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         std::wstring cur=mut?L"(bez dźwięku)":cp.empty()?L"domyślny":ovFileBase(cp);
         ovRect(cx+194,y+2,cw-194-196,28,6,0,0,0,0.30f);
         ovText(cur,cx+202,y+2,cw-194-196-16,28,13.5f,mut?0.75f:1.f,mut?0.78f:1.f,mut?0.85f:1.f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         float bx=cx+cw-190;
         ovButton(bx,y+1,70,30,L"Wybierz",OH_SBROWSE,i,false,true,13.f);
         ovButton(bx+76,y+1,36,30,L"▶",OH_SPLAY,i,false,!mut,13.f);
         ovButton(bx+116,y+1,36,30,L"✕",OH_SMUTE,i,mut,true,13.f);
         ovButton(bx+156,y+1,36,30,L"↺",OH_SDEF,i,false,mut||!cp.empty(),13.f);
         y+=38;
      }
      break;}
   case 2:{                                                                         // Keys
      ovNote(L"Kliknij pole skrótu i naciśnij klawisz (z Shiftem, jeśli ma go wymagać). Każda akcja ma dwa skróty; ✕ czyści skrót.",cx,y-4,cw,40); y+=38;
      for(int i=0;i<KA_COUNT;i++,y+=32){
         ovText(KA_LABELS[i],cx,y,240,30,14.5f,LIT_R,LIT_G,LIT_B,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         for(int s=0;s<2;s++){
            float bx=cx+250+s*160; int id=i*2+s; bool cap=(g_ovCapture==id);
            DWORD vk=s==0?g_keys[i].key1:g_keys[i].key2;
            ovButton(bx,y,104,28,cap?std::wstring(L"Naciśnij…"):bindingName(vk,g_keys[i].shift),OH_KEY,id,cap,true,13.5f);
            ovButton(bx+108,y,28,28,L"✕",OH_KEYCLR,id,false,vk!=0,12.f);
         }
      }
      y+=6;
      ovButton(cx,y,200,34,L"Przywróć domyślne",OH_KEYDEF,0);
      ovNote(L"Stałe: Ctrl+Z / Ctrl+Y (cofnij / ponów), S (statystyki), Shift+1 (Samograj bez końca), Esc (zamyka okno).",cx+212,y+2,cw-212,34);
      break;}
   }
}

static void ovSetVolume(float mx){
   float v=(mx-g_ovVolX)/std::max(1.f,g_ovVolW);
   g_volume=std::max(0.f,std::min(1.f,std::round(v*100.f)/100.f)); invalidateGame();
}
static void ovSetAnim(float mx){
   int st=(int)std::lround((mx-g_ovAnimX)/std::max(1.f,g_ovAnimW)*4.f)-2;
   st=std::max(-2,std::min(2,st));
   if(st!=g_animSpeedStep){ applyAnimSpeedStep(st); invalidateGame(); }
}
static void keysResetDefaults(){
   for(int i=0;i<KA_COUNT;i++){ g_keys[i].key1=KA_DEFAULTS[i][0]; g_keys[i].key2=KA_DEFAULTS[i][1]; g_keys[i].shift=KA_DEFAULT_SHIFT[i]; }
}

static void settingsOverlayClick(const OvHit& h,float mx){
   switch(h.kind){
   case OH_GROUP: g_ovGroup=h.a; g_ovCapture=-1; SoundSystem::instance().fadeOutAll(150); break;
   case OH_BG:    g_bgIndex=h.a; saveSettings(); toolbarRefresh(); break;
   case OH_FREE:{
      bool nf=(h.a==1);
      if(nf==g_freeColMode) break;
      if(g_gameStarted && !g_won && g_moveCount>0 &&
         MessageBoxW(g_hwnd,L"Zmiana trybu rozpocznie nową grę. Kontynuować?",L"Wolne miejsce",MB_YESNO|MB_ICONQUESTION)!=IDYES) break;
      bool restart=(g_gameStarted && !g_won && g_moveCount>0);
      g_freeColMode=nf; saveSettings();
      if(restart) newGame();
      break;}
   case OH_HILITE: g_moveHighlightMode=h.a; saveSettings(); break;
   case OH_DEPTH:{
      int& d= h.a<2 ? g_searchDepthFree : g_searchDepthKing;
      int nd=d+((h.a%2)?1:-1); nd=std::max(1,std::min(MAX_SEARCH_DEPTH,nd));
      if(nd!=d){ d=nd; invalidateBestMoveCache(); saveSettings(); }
      break;}
   case OH_ANIM:  g_ovDragAnim=true; SetCapture(g_gameHwnd); ovSetAnim(mx); break;
   case OH_UPDATES: g_checkUpdatesOnStart=!g_checkUpdatesOnStart; saveSettings(); break;
   case OH_VOL:   g_ovDragVol=true; SetCapture(g_gameHwnd); ovSetVolume(mx); break;
   case OH_SBROWSE:{
      wchar_t path[MAX_PATH]={};
      OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
      ofn.lpstrFilter=L"Pliki dźwiękowe (*.wav;*.mp3)\0*.wav;*.mp3\0WAV (*.wav)\0*.wav\0MP3 (*.mp3)\0*.mp3\0Wszystkie\0*.*\0";
      ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
      wchar_t title[64]={}; wcsncpy_s(title,64,SOUND_LABELS[h.a],_TRUNCATE); ofn.lpstrTitle=title;
      if(GetOpenFileNameW(&ofn)){
         SoundSystem::instance().setCustomPath(h.a,path); SoundSystem::instance().setMuted(h.a,false);
         SoundSystem::instance().fadeOutAll(100); SoundSystem::instance().playIdx(h.a,g_volume);
         saveSettings();
      }
      break;}
   case OH_SPLAY: SoundSystem::instance().fadeOutAll(150); SoundSystem::instance().playIdx(h.a,g_volume); break;
   case OH_SMUTE: SoundSystem::instance().setMuted(h.a,true); SoundSystem::instance().fadeOutAll(100); saveSettings(); break;
   case OH_SDEF:  SoundSystem::instance().setMuted(h.a,false); SoundSystem::instance().setCustomPath(h.a,L""); saveSettings(); break;
   case OH_KEY:   g_ovCapture=h.a; break;
   case OH_KEYCLR:{ KeyBinding& k=g_keys[h.a/2]; if(h.a%2==0) k.key1=0; else k.key2=0; saveSettings(); break;}
   case OH_KEYDEF: keysResetDefaults(); saveSettings(); break;
   }
}

// ============================================================================
// Statistics
// ============================================================================
static void statsOverlayDraw(){
   const float pw=std::min(780.f,g_ovW-30.f), ph=std::min(620.f,g_ovH-30.f), px=std::floor((g_ovW-pw)/2.f), py=std::floor((g_ovH-ph)/2.f);
   ovPanel(px,py,pw,ph,L"Statystyki",L"Rozegrane i zakończone partie z tego komputera");
   const float x0=px+30, w=pw-60;
   const float c0=w*0.46f, c1=w*0.27f;                                   // label column, two mode columns
   float y=py+92;
   ovText(L"",x0,y,c0,30,15,1,1,1,1.f);
   ovText(L"Tylko król",x0+c0,y,c1,30,16,1.f,0.86f,0.30f,1.f,true);
   ovText(L"Dowolna karta",x0+c0+c1,y,c1,30,16,1.f,0.86f,0.30f,1.f,true);
   for(int m=0;m<2;m++) if((m==1)==g_freeColMode) ovRect(x0+c0+m*c1+c1*0.2f,y+30,c1*0.6f,3,1.5f,1.f,0.86f,0.30f,1.f);   // the mode being played
   y+=38; ovRect(x0,y,w,1.5f,0,1.f,0.86f,0.30f,0.45f); y+=6;
   auto avgMoves=[](int sum,int cnt)->std::wstring{ if(cnt<=0) return L"–"; int t=(int)((double)sum/cnt*10+0.5); wchar_t b[24]; swprintf(b,24,L"%d,%d",t/10,t%10); return b; };
   auto avgTime=[](int sum,int cnt)->std::wstring{ if(cnt<=0) return L"–"; return fmtTime((int)((double)sum/cnt+0.5)); };
   struct Row{ std::wstring name; std::wstring v[2]; bool bold; bool rule; };
   std::vector<Row> rows;
   { Row r; r.bold=false; r.rule=false;
     r.name=L"Rozegrane";                for(int m=0;m<2;m++) r.v[m]=std::to_wstring(g_statsGames[m]); rows.push_back(r);
     r.name=L"Wygrane";                  for(int m=0;m<2;m++) r.v[m]=std::to_wstring(g_statsWins[m]); rows.push_back(r);
     r.name=L"Przegrane";                for(int m=0;m<2;m++) r.v[m]=std::to_wstring(g_statsGames[m]-g_statsWins[m]); rows.push_back(r);
     r.name=L"% wygranych"; r.bold=true; for(int m=0;m<2;m++){ int g=g_statsGames[m]; int p10=g>0?(int)(g_statsWins[m]*1000.0/g+0.5):0; wchar_t b[24]; if(g>0) swprintf(b,24,L"%d,%d%%",p10/10,p10%10); else wcscpy_s(b,L"–"); r.v[m]=b; } rows.push_back(r); r.bold=false;
     r.name=L"Śr. ruchów (wygrane)";     for(int m=0;m<2;m++) r.v[m]=avgMoves(g_statsMoveSum[m][1],g_statsWins[m]); r.rule=true; rows.push_back(r); r.rule=false;
     r.name=L"Śr. ruchów (przegrane)";   for(int m=0;m<2;m++) r.v[m]=avgMoves(g_statsMoveSum[m][0],g_statsGames[m]-g_statsWins[m]); rows.push_back(r);
     r.name=L"Śr. czas (wygrane)";       for(int m=0;m<2;m++) r.v[m]=avgTime(g_statsTimeSum[m][1],g_statsWins[m]); rows.push_back(r);
     r.name=L"Śr. czas (przegrane)";     for(int m=0;m<2;m++) r.v[m]=avgTime(g_statsTimeSum[m][0],g_statsGames[m]-g_statsWins[m]); rows.push_back(r);
     r.name=L"Rekord: najmniej ruchów";  for(int m=0;m<2;m++) r.v[m]=g_statsRecordMoves[m]<0?L"–":std::to_wstring(g_statsRecordMoves[m]); r.rule=true; rows.push_back(r); r.rule=false;
     r.name=L"Rekord: najkrótszy czas";  for(int m=0;m<2;m++) r.v[m]=g_statsRecordTime[m]<0?L"–":fmtTime(g_statsRecordTime[m]); rows.push_back(r); }
   const float rh=std::min(34.f,(ph-92-38-6-140)/(float)rows.size());
   for(const Row& r:rows){
      if(r.rule){ ovRect(x0,y+2,w,1.2f,0,1,1,1,0.25f); y+=8; }
      ovText(r.name,x0,y,c0,rh,15.5f,LIT_R,LIT_G,LIT_B,1.f,r.bold,DWRITE_TEXT_ALIGNMENT_LEADING);
      for(int m=0;m<2;m++) ovText(r.v[m],x0+c0+m*c1,y,c1,rh,16,LIT_R,LIT_G,LIT_B,1.f,r.bold);
      y+=rh;
   }
   y+=8;
   ovNote(L"Do statystyk wchodzą tylko partie rozegrane do końca w trybie, w którym zostały rozpoczęte. Gry wczytane z pliku, powtórki i odtwarzanie rozwiązań nie są liczone.",x0,y,w,48);
   ovButton(x0,py+ph-60,190,36,L"Wyzeruj statystyki",OH_STATRESET,0);
}
static void statsReset(){
   if(MessageBoxW(g_hwnd,L"Wyzerować wszystkie statystyki i rekordy? Tego nie można cofnąć.",L"Statystyki",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
   for(int m=0;m<2;m++){
      g_statsGames[m]=g_statsWins[m]=0;
      g_statsMoveSum[m][0]=g_statsMoveSum[m][1]=0;
      g_statsTimeSum[m][0]=g_statsTimeSum[m][1]=0;
      g_statsRecordMoves[m]=-1; g_statsRecordTime[m]=-1;
   }
   g_gameStarted=false; g_moveCount=0; g_gameSeconds=0;
   saveStats(); ovClose(); newGame();
}

// ============================================================================
// Help
// ============================================================================
enum { HK_H=1, HK_P=2, HK_B=3, HK_NOTE=4 };      // heading, paragraph, bullet (bold lead-in), small note
struct HelpItem{ int kind; const wchar_t* lead; const wchar_t* text; };
static const HelpItem HELP_DOC[]={
 {HK_H,nullptr,L"Cel gry"},
 {HK_P,nullptr,L"Umieść wszystkich 8 króli: każdy na kolumnie, z całym ułożonym sekwensem pod sobą, albo na stosie (fundamencie) zbudowanym od asa do króla w jednym kolorze. Grasz dwiema taliami (104 karty)."},

 {HK_H,nullptr,L"Zasady"},
 {HK_B,L"Stół",L"10 kolumn. Kartę lub cały sekwens (kolory na przemian, wartości malejące ku dołowi) możesz przenieść na kartę o 1 wyższą i innego koloru."},
 {HK_B,L"Pusta kolumna",L"w trybie „Tylko król” przyjmuje tylko króla (lub sekwens z królem na górze), w trybie „Dowolna karta” każdą kartę. Tryb zmieniasz w Ustawieniach."},
 {HK_B,L"Stosy",L"na górze: as, 2, … król w jednym kolorze. Ze stosu możesz zwrócić kartę na stół."},
 {HK_B,L"Rezerwa",L"kliknij stos nierozłożonych kart, aby dołożyć po jednej karcie do każdej kolumny (oprócz kolumn z królem na szczycie sekwensu). Kolor rewersu (czerwony lub niebieski) pokazuje, z której talii pochodzi następna karta."},

 {HK_H,nullptr,L"Sterowanie myszą"},
 {HK_B,L"Przeciąganie",L"przenosi kartę lub sekwens tam, gdzie chcesz."},
 {HK_B,L"Kliknięcie",L"przenosi kartę na najbliższe możliwe miejsce (auto-ruch)."},
 {HK_B,L"Przycisk Podpowiedź",L"pokazuje najlepszy ruch: karta leci do miejsca docelowego."},

 {HK_H,nullptr,L"Skróty klawiszowe"},
 {HK_B,L"F2, N",L"nowa gra"},
 {HK_B,L"P",L"podpowiedź"},
 {HK_B,L"←, Z, Ctrl+Z",L"cofnij ruch (z Shiftem: cofnij do rezerwy)"},
 {HK_B,L"→, Y, Ctrl+Y",L"ponów ruch (z Shiftem: ponów do rezerwy)"},
 {HK_B,L"A",L"jeden automatyczny ruch"},
 {HK_B,L"G",L"Samograj (automat układa pasjansa); Shift+1: Samograj bez końca"},
 {HK_B,L"S",L"statystyki"},
 {HK_B,L"F11, ↑",L"pełny ekran (Esc wychodzi z pełnego ekranu)"},
 {HK_B,L"H",L"ta pomoc"},
 {HK_NOTE,nullptr,L"Podane są skróty domyślne. Większość możesz zmienić w Ustawieniach → Klawisze."},

 {HK_H,nullptr,L"Numery rozdań i zapisy"},
 {HK_P,nullptr,L"Każde rozdanie ma numer: ten sam numer zawsze daje ten sam układ kart. W menu Akcje: Zagraj numer, Zagraj wygrywający, Zagraj nieudany, Zapisz i Wczytaj grę (folder Saves)."},
 {HK_P,nullptr,L"Pasjans rozwiązany przez Ciebie zapisuje się w folderze Solved jako SolvedUser<numer>.dat. Wczytasz go przez Wczytaj grę i przejdziesz ruch po ruchu przyciskiem Ponów."},

 {HK_H,nullptr,L"Solver"},
 {HK_P,nullptr,L"Przycisk Solver szuka w tle rozwiązań rozdań z listy przegranych (LostNumbers.csv). Wybierasz czas jednego etapu (1–10 min), liczbę wątków i to, czy po upływie czasu przejść do następnego rozdania. Rozwiązania trafiają do folderu Solved."},

 {HK_H,nullptr,L"Aktualizacje"},
 {HK_P,nullptr,L"Przy starcie gra sprawdza w serwisie GitHub, czy jest nowsza wersja, i proponuje jej instalację. Możesz to wyłączyć w Ustawieniach → Ogólne."},

 {HK_H,nullptr,L"Pliki obok programu"},
 {HK_B,L"pasjans.ini",L"ustawienia i statystyki."},
 {HK_B,L"Sounds",L"wrzucone tu pliki WAV (np. click.wav, sukces.wav) zastępują wbudowane dźwięki."},
 {HK_B,L"WonNumbers.csv, LostNumbers.csv, Unsolvable.csv",L"numery wygranych, przegranych i nierozwiązywalnych rozdań."},
};
struct HelpBlock{ IDWriteTextLayout* lay=nullptr; int kind=0; float y=0,h=0; };
static std::vector<HelpBlock> g_helpBlocks;
static void helpRelease(){ for(auto& b:g_helpBlocks) if(b.lay) b.lay->Release(); g_helpBlocks.clear(); }
static void helpGeom(float& px,float& py,float& pw,float& ph,float& vx,float& vy,float& vw,float& vh){
   pw=std::min(840.f,g_ovW-30.f); ph=g_ovH-30.f; px=std::floor((g_ovW-pw)/2.f); py=15.f;
   vx=px+28.f; vy=py+82.f; vw=pw-28.f-40.f; vh=ph-82.f-24.f;
}
static void helpBuild(float width){
   helpRelease(); float y=0;
   for(const HelpItem& it:HELP_DOC){
      float px= it.kind==HK_H?22.f : it.kind==HK_NOTE?13.f : 16.f;
      float indent= it.kind==HK_B?24.f:0.f;
      std::wstring text= it.lead ? std::wstring(it.lead)+L" – "+it.text : std::wstring(it.text);
      IDWriteTextFormat* f=nullptr;
      g_dwFactory->CreateTextFormat(L"Segoe UI",nullptr,it.kind==HK_H?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
         it.kind==HK_NOTE?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
      if(!f) continue;
      f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,px*1.45f,px*1.12f);
      IDWriteTextLayout* lay=nullptr;
      g_dwFactory->CreateTextLayout(text.c_str(),(UINT32)text.size(),f,width-indent,100000.f,&lay);
      f->Release(); if(!lay) continue;
      if(it.lead){ DWRITE_TEXT_RANGE r={0,(UINT32)wcslen(it.lead)}; lay->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,r); }
      DWRITE_TEXT_METRICS m{}; lay->GetMetrics(&m);
      float before= it.kind==HK_H?(y==0?0.f:24.f) : it.kind==HK_B?5.f : 8.f;
      HelpBlock b; b.lay=lay; b.kind=it.kind; b.y=y+before; b.h=m.height;
      y=b.y+b.h+(it.kind==HK_H?12.f:0.f);
      g_helpBlocks.push_back(b);
   }
   g_ovContentH=y+12.f; g_ovBuiltW=width;
}
static void helpClamp(float vh){ g_ovScroll=std::max(0.f,std::min(g_ovScroll,std::max(0.f,g_ovContentH-vh))); }
static void helpThumb(float vy,float vh,float& ty,float& th){
   float maxS=std::max(1.f,g_ovContentH-vh);
   th=std::max(40.f,vh*vh/std::max(vh,g_ovContentH));
   ty=vy+(vh-th)*(g_ovScroll/maxS);
}
static void helpOverlayDraw(){
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   if(g_ovBuiltW!=vw) helpBuild(vw);
   helpClamp(vh);
   ovPanel(px,py,pw,ph,L"Pasjans Dziadkowy",std::wstring(L"Wersja ")+APP_VERSION+L"  ·  zbudowana "+buildDateText());
   ID2D1SolidColorBrush *bBody=nullptr,*bGold=nullptr,*bDim=nullptr,*bRule=nullptr;
   g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(0.94f,0.96f,1.f,1.f),&bBody);
   g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1.f,0.86f,0.30f,1.f),&bGold);
   g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(DIM_R,DIM_G,DIM_B,1.f),&bDim);
   g_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1.f,0.86f,0.30f,0.35f),&bRule);
   g_d2dRT->PushAxisAlignedClip(D2D1::RectF(vx-4,vy,vx+vw+8,vy+vh),D2D1_ANTIALIAS_MODE_ALIASED);
   for(const HelpBlock& b:g_helpBlocks){
      float top=vy+b.y-g_ovScroll;
      if(top+b.h<vy-4||top>vy+vh+4) continue;
      ID2D1SolidColorBrush* br= b.kind==HK_H?bGold : b.kind==HK_NOTE?bDim : bBody;
      if(b.kind==HK_B){
         D2D1_ELLIPSE e=D2D1::Ellipse(D2D1::Point2F(vx+8.f,top+11.f),3.2f,3.2f);
         if(bGold) g_d2dRT->FillEllipse(e,bGold);
         if(br) g_d2dRT->DrawTextLayout(D2D1::Point2F(vx+24.f,top),b.lay,br);
      } else {
         if(br) g_d2dRT->DrawTextLayout(D2D1::Point2F(vx,top),b.lay,br);
         if(b.kind==HK_H && bRule) g_d2dRT->DrawLine(D2D1::Point2F(vx,top+b.h+4.f),D2D1::Point2F(vx+vw,top+b.h+4.f),bRule,1.2f);
      }
   }
   g_d2dRT->PopAxisAlignedClip();
   for(auto* b:{bBody,bGold,bDim,bRule}) if(b) b->Release();
   if(g_ovContentH>vh+1){
      float sx=px+pw-30, ty,th; helpThumb(vy,vh,ty,th);
      ovRect(sx,vy,8,vh,4,1,1,1,0.10f);
      ovRect(sx,ty,8,th,4,1.f,0.86f,0.30f,g_ovDragThumb?0.95f:0.70f);
   }
}
static void helpMouseDown(float mx,float my){
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   if(g_ovContentH>vh+1){
      float sx=px+pw-30, ty,th; helpThumb(vy,vh,ty,th);
      if(mx>=sx-6&&mx<=sx+14&&my>=vy&&my<=vy+vh){
         if(my>=ty&&my<=ty+th){ g_ovDragThumb=true; g_ovGrabDy=my-ty; SetCapture(g_gameHwnd); }
         else { g_ovScroll+=(my<ty?-1.f:1.f)*vh*0.9f; helpClamp(vh); }
         invalidateGame();
      }
   }
}
static void helpMouseMove(float my){
   if(!g_ovDragThumb) return;
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   float ty,th; helpThumb(vy,vh,ty,th);
   float track=vh-th; if(track<1.f) return;
   g_ovScroll=((my-g_ovGrabDy-vy)/track)*(g_ovContentH-vh);
   helpClamp(vh); invalidateGame();
}

// ============================================================================
// Entry points: drawing, mouse, wheel and keyboard (called from drawScene / GameWndProc / WndProc)
// ============================================================================
static void overlaysDraw(float W,float H){
   if(g_ov==OV_NONE||!g_d2dRT) return;
   g_ovW=W; g_ovH=H; g_ovHits.clear();
   switch(g_ov){
   case OV_SETTINGS: settingsOverlayDraw(); break;
   case OV_STATS:    statsOverlayDraw(); break;
   case OV_HELP:     helpOverlayDraw(); break;
   }
}
// The panel rectangle of the open overlay (a click outside it closes it).
static void ovPanelRect(float& px,float& py,float& pw,float& ph){
   if(g_ov==OV_SETTINGS){ pw=std::min(880.f,g_ovW-30.f); ph=std::min(640.f,g_ovH-30.f); }
   else if(g_ov==OV_STATS){ pw=std::min(780.f,g_ovW-30.f); ph=std::min(620.f,g_ovH-30.f); }
   else { float vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh); return; }
   px=std::floor((g_ovW-pw)/2.f); py=std::floor((g_ovH-ph)/2.f);
}
static void ovMouseDown(float mx,float my){
   g_ovSwallowUp=true;
   if(g_ovCapture>=0){ g_ovCapture=-1; invalidateGame(); }                    // a click cancels the waiting for a key
   for(const OvHit& h:g_ovHits){
      if(mx<h.x||mx>h.x+h.w||my<h.y||my>h.y+h.h) continue;
      if(h.kind==OH_CLOSE){ ovClose(); return; }
      if(g_ov==OV_SETTINGS) settingsOverlayClick(h,mx);
      else if(g_ov==OV_STATS && h.kind==OH_STATRESET){ statsReset(); }
      invalidateGame(); return;
   }
   float px,py,pw,ph; ovPanelRect(px,py,pw,ph);
   if(mx<px||mx>px+pw||my<py||my>py+ph){ ovClose(); return; }
   if(g_ov==OV_HELP) helpMouseDown(mx,my);
}
static void ovMouseMove(float mx,float my){
   if(g_ovDragVol) ovSetVolume(mx); else if(g_ovDragAnim) ovSetAnim(mx); else if(g_ovDragThumb) helpMouseMove(my);
}
static void ovMouseUp(){
   g_ovSwallowUp=false;
   if(g_ovDragVol){ g_ovDragVol=false; ReleaseCapture(); saveSettings(); playSound("click",g_volume); invalidateGame(); }
   if(g_ovDragAnim){ g_ovDragAnim=false; ReleaseCapture(); saveSettings(); invalidateGame(); }
   if(g_ovDragThumb){ g_ovDragThumb=false; ReleaseCapture(); invalidateGame(); }
}
static void ovWheel(int delta){
   if(g_ov!=OV_HELP) return;
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   g_ovScroll-=(float)delta/120.f*60.f; helpClamp(vh); invalidateGame();
}
static void ovKey(WPARAM k){
   if(g_ov==OV_SETTINGS && g_ovCapture>=0){
      DWORD vk=(DWORD)k;
      if(vk==VK_SHIFT||vk==VK_CONTROL||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN) return;      // a modifier alone is not a key
      if(vk==VK_ESCAPE){ g_ovCapture=-1; invalidateGame(); return; }
      int a=g_ovCapture/2, sl=g_ovCapture%2;
      if(sl==0) g_keys[a].key1=vk; else g_keys[a].key2=vk;
      g_keys[a].shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
      g_ovCapture=-1; saveSettings(); invalidateGame(); return;
   }
   if(k==VK_ESCAPE){ ovClose(); return; }
   if(g_ov==OV_HELP){
      float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
      switch(k){
      case VK_UP:    g_ovScroll-=46; break;
      case VK_DOWN:  g_ovScroll+=46; break;
      case VK_PRIOR: g_ovScroll-=vh*0.9f; break;
      case VK_NEXT:  g_ovScroll+=vh*0.9f; break;
      case VK_HOME:  g_ovScroll=0; break;
      case VK_END:   g_ovScroll=1e9f; break;
      default: if(g_keys[KA_HELP].matches((DWORD)k)) ovClose(); return;
      }
      helpClamp(vh); invalidateGame();
   }
   else if(g_ov==OV_STATS && k=='S') ovClose();
}

static void showSettings(HWND){ ovOpen(OV_SETTINGS); }
static void showStats(HWND){ ovOpen(OV_STATS); }
static void showHelp(HWND){ ovOpen(OV_HELP); }
