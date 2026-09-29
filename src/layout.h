#pragma once
#include <windows.h>
#include <algorithm>
#include "game.h"

struct Layout {
   int cardW    = 71;
   int cardH    = 100;
   int cornerR  = 6;
   int overlap  = 26;   // "natural" overlap — may be overridden per-column
   int foundY   = 52;
   int tableY   = 165;
   int panelW   = 1024;
   int panelH   = 720;

   // Cached horizontal positions — set once in recalc(), never recomputed
   int m_colStartX = 8;
   int m_reserveX  = 800;
   int m_colGap    = 10;
   int m_colSpacing= 81;

   static constexpr float PNG_ASPECT = 224.f / 160.f;
   // 64px icon (Y=4) + 2px gap + 16px caption label + 8px bottom margin
   static const int TOOLBAR_H  = 94;
   static const int MARGIN_BOT = 8;
   static const int MARGIN_SIDE= 8;

   // -----------------------------------------------------------------------
   // Natural overlap: a fixed fraction of cardH regardless of window height.
   // We only reduce it when cards would go off-screen.
   // -----------------------------------------------------------------------
   static int naturalOverlap(int ch) {
      // Show about 28% of card height as the exposed strip.
      // This is comfortable and doesn't change with window size.
      return std::max(12, (int)(ch * 0.28f));
   }

   // How many pixels does a column of n cards need with given overlap?
   static int colHeight(int n, int ov, int ch) {
      if (n <= 0) return ch;
      return (n - 1) * ov + ch;
   }

   static int minPanelW() {
      const int minCW = 38;
      const int gap   = 10;
      return NUM_COLS * minCW + (NUM_COLS-1)*gap + 10 + minCW + 2*MARGIN_SIDE;
   }

   static int minPanelH() {
      const int minCW = 38;
      const int minCH = (int)(minCW * PNG_ASPECT);
      return TOOLBAR_H + minCH + 12 + minCH + 9*12 + MARGIN_BOT;
   }

   void recalc(int pw, int ph, int maxInCol) {
      panelW = pw;
      panelH = ph;

      // Clamp to minimum so we never crash
      int epw = std::max(pw, minPanelW());
      int eph = std::max(ph, minPanelH());

      // ----------------------------------------------------------------
      // Horizontal: find largest cardW that fits
      //   [MARGIN] [10 cols + gaps] [resGap] [reserve] [MARGIN]
      //   Entire block is centred.
      // Vertical: use naturalOverlap, reduce ONLY if cards don't fit.
      // ----------------------------------------------------------------
      for (int cw = 160; cw >= 36; cw--) {
         int ch    = (int)(cw * PNG_ASPECT);
         int gap   = std::max(10, std::min(40, epw / (NUM_COLS * 7)));
         int colW  = NUM_COLS * cw + (NUM_COLS - 1) * gap;

         // All 10 columns + foundations + reserve all fit in colW
         if (colW + 2 * MARGIN_SIDE > epw) continue;

         // Vertical: compute natural overlap and squeeze only if needed
         int fy  = TOOLBAR_H + 4;
         int ty  = fy + ch + 12;
         int avH = eph - ty - MARGIN_BOT;

         int ov  = naturalOverlap(ch);
         if (maxInCol > 1) {
            int needed = colHeight(maxInCol, ov, ch);
            if (needed > avH)
               ov = (avH - ch) / (maxInCol - 1);
         }
         ov = std::max(12, ov);

         if (ty + colHeight(maxInCol, ov, ch) > eph - MARGIN_BOT) continue;

         // Centre the 10-column block horizontally
         cardW       = cw;
         cardH       = ch;
         cornerR     = std::max(3, cw / 12);
         overlap     = ov;
         foundY      = fy;
         tableY      = ty;
         m_colStartX = (epw - colW) / 2;
         m_reserveX  = m_colStartX + 9 * (cw + gap); // col 9 x pos
         m_colGap    = gap;
         m_colSpacing= cw + gap;
         return;
      }

      // Fallback for tiny windows
      int cw = 36;
      int ch = (int)(cw * PNG_ASPECT);
      cardW       = cw;
      cardH       = ch;
      cornerR     = 3;
      overlap     = 12;
      foundY      = TOOLBAR_H + 4;
      tableY      = foundY + ch + 12;
      m_colStartX = MARGIN_SIDE;
      m_reserveX  = MARGIN_SIDE + 9*(cw+10);
      m_colGap    = 10;
      m_colSpacing= cw + 10;
   }

   // Accessors — always use stored values, never recompute
   int colGap()     const { return m_colGap; }
   int colSpacing() const { return m_colSpacing; }
   int colsWidth()  const { return NUM_COLS * cardW + (NUM_COLS-1) * m_colGap; }

   POINT colPos(int col) const {
      return { m_colStartX + col * m_colSpacing, tableY };
   }

   // Foundation slots 0-7 aligned above columns 0-7
   POINT foundPos(int i) const {
      return { m_colStartX + i * m_colSpacing, foundY };
   }

   // Reserve stack sits above column 9 (rightmost column)
   POINT reservePos() const {
      return { m_colStartX + 9 * m_colSpacing, foundY };
   }

   // Reserve card count label: to the LEFT of reserve stack
   POINT reserveCountPos() const {
      return { m_colStartX + 9 * m_colSpacing - 50, foundY + cardH / 2 - 8 };
   }

   // Foundation preferred suit: ♠♠ ♥♥ ♦♦ ♣♣
   static Suit preferredSuit(int slot) {
      if (slot < 2) return Spades;
      if (slot < 4) return Hearts;
      if (slot < 6) return Diamonds;
      return Clubs;
   }

   int cardY(int /*col*/, int ci) const { return tableY + ci * overlap; }

   static int calcGap(int pw, int /*cw*/) {
      return std::max(10, std::min(40, pw / (NUM_COLS * 7)));
   }
};
