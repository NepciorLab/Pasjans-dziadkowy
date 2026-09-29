#pragma once
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <string>

// ── Configurable key bindings ─────────────────────────────────────────────────
enum KeyAction {
   KA_NEW=0, KA_HINT, KA_UNDO, KA_REDO, KA_AUTO, KA_SAMOGRAJ, KA_FULLSCREEN, KA_HELP,
   KA_UNDO_DEAL, KA_REDO_DEAL,
   KA_COUNT
};

static const wchar_t* KA_LABELS[KA_COUNT] = {
   L"Nowa gra",
   L"Podpowiedź",
   L"Cofnij",
   L"Ponów",
   L"Automatyczny ruch",
   L"Samograj",
   L"Pełny ekran",
   L"Pomoc",
   L"Cofnij do rezerwy",
   L"Ponów do rezerwy",
};

static const wchar_t* KA_INI_KEYS[KA_COUNT] = {
   L"NewGame", L"Hint", L"Undo", L"Redo", L"AutoMove", L"Samograj", L"FullScreen", L"Help",
   L"UndoDeal", L"RedoDeal"
};

// Default bindings: {key1, key2}  (0 = none)
static const DWORD KA_DEFAULTS[KA_COUNT][2] = {
   {VK_F2,    'N'},          // New game
   {'P',      0},            // Hint
   {VK_LEFT,  'Z'},          // Undo  (Z without modifier here — Ctrl+Z handled separately)
   {VK_RIGHT, 'Y'},          // Redo
   {'A',      0},            // Auto-move
   {'G',      0},            // Samograj (self-play toggle)
   {VK_F11,   VK_UP},        // Fullscreen
   {'H',      0},            // Help
   {VK_LEFT,  0},            // Undo to previous reserve deal (Shift+ same key as Undo)
   {VK_RIGHT, 0},            // Redo to next reserve deal     (Shift+ same key as Redo)
};
// Whether each action's binding requires Shift held (parallel to KA_DEFAULTS)
static const bool KA_DEFAULT_SHIFT[KA_COUNT] = {
   false,false,false,false,false,false,false,false, true,true
};

struct KeyBinding {
   DWORD key1 = 0;
   DWORD key2 = 0;
   bool  shift = false; // require Shift to be held
   bool matches(DWORD vk) const {
      bool shiftHeld = (GetKeyState(VK_SHIFT)&0x8000)!=0;
      if(shiftHeld != shift) return false;
      return (key1 && vk==key1) || (key2 && vk==key2);
   }
};

// Global bindings array
extern KeyBinding g_keys[KA_COUNT];

// Human-readable name for a VK code
inline std::wstring vkName(DWORD vk) {
   if(!vk) return L"—";
   switch(vk) {
      case VK_LEFT:   return L"←";
      case VK_RIGHT:  return L"→";
      case VK_UP:     return L"↑";
      case VK_DOWN:   return L"↓";
      case VK_F1:     return L"F1";  case VK_F2:  return L"F2";
      case VK_F3:     return L"F3";  case VK_F4:  return L"F4";
      case VK_F5:     return L"F5";  case VK_F6:  return L"F6";
      case VK_F7:     return L"F7";  case VK_F8:  return L"F8";
      case VK_F9:     return L"F9";  case VK_F10: return L"F10";
      case VK_F11:    return L"F11"; case VK_F12: return L"F12";
      case VK_ESCAPE: return L"Esc";
      case VK_RETURN: return L"Enter";
      case VK_SPACE:  return L"Spacja";
      case VK_TAB:    return L"Tab";
      case VK_BACK:   return L"Backspace";
      case VK_DELETE: return L"Delete";
      case VK_INSERT: return L"Insert";
      case VK_HOME:   return L"Home";
      case VK_END:    return L"End";
      case VK_PRIOR:  return L"PgUp";
      case VK_NEXT:   return L"PgDn";
      default: {
         wchar_t buf[32] = {};
         // Map VK to character
         UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
         GetKeyNameTextW((LONG)(sc << 16), buf, 32);
         if(buf[0]) return std::wstring(buf);
         // Fallback: show as hex
         wsprintfW(buf, L"0x%02X", vk);
         return buf;
      }
   }
}

// Human-readable name for a full binding, including a "Shift+" prefix when needed
inline std::wstring bindingName(DWORD vk, bool shift) {
   if(!vk) return L"—";
   std::wstring n = vkName(vk);
   if(shift) return L"Shift+"+n;
   return n;
}

// Save/load helpers
inline void saveKeyBindings(const std::wstring& iniPath) {
   wchar_t buf[48];
   for(int i=0;i<KA_COUNT;i++) {
      wsprintfW(buf,L"%u,%u,%u",g_keys[i].key1,g_keys[i].key2,g_keys[i].shift?1u:0u);
      WritePrivateProfileStringW(L"Keys",KA_INI_KEYS[i],buf,iniPath.c_str());
   }
}

inline void loadKeyBindings(const std::wstring& iniPath) {
   wchar_t buf[64];
   for(int i=0;i<KA_COUNT;i++) {
      // Start with defaults
      g_keys[i].key1  = KA_DEFAULTS[i][0];
      g_keys[i].key2  = KA_DEFAULTS[i][1];
      g_keys[i].shift = KA_DEFAULT_SHIFT[i];
      GetPrivateProfileStringW(L"Keys",KA_INI_KEYS[i],L"",buf,64,iniPath.c_str());
      if(buf[0]) {
         DWORD k1=0,k2=0,sh=0;
         int n=swscanf_s(buf,L"%u,%u,%u",&k1,&k2,&sh);
         if(n==3) { g_keys[i].key1=k1; g_keys[i].key2=k2; g_keys[i].shift=(sh!=0); }
         else if(n==2) { g_keys[i].key1=k1; g_keys[i].key2=k2; } // pre-shift ini format
      }
   }
}
