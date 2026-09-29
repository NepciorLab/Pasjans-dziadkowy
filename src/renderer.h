#pragma once
#include <objbase.h>
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include "layout.h"
#include "card_images.h"
#include "anim.h"
using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Rounded rectangle helper
// ---------------------------------------------------------------------------
static GraphicsPath* RRect(float x, float y, float w, float h, float r) {
   GraphicsPath* p = new GraphicsPath();
   p->AddArc(x,     y,     r*2,r*2,180,90);
   p->AddArc(x+w-r*2, y,   r*2,r*2,270,90);
   p->AddArc(x+w-r*2, y+h-r*2, r*2,r*2,  0,90);
   p->AddArc(x,     y+h-r*2, r*2,r*2, 90,90);
   p->CloseFigure();
   return p;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
class Renderer {
public:
   void drawCard(Graphics& g, const Card& card, int x, int y,
                 bool selected, bool hinted) const {
      drawShadow(g, x, y);
      int cw=m_lay->cardW, ch=m_lay->cardH;
      Bitmap* bmp = GetCardScaled(card.imgKey(), cw, ch);
      if(bmp) {
         // Scaled bitmap matches target size exactly — simple copy, no scaling work
         g.DrawImage(bmp, x, y, cw, ch);
      } else {
         // Fallback: white rounded rect
         GraphicsPath* p = RRect((float)x,(float)y,(float)cw,(float)ch,(float)m_lay->cornerR);
         SolidBrush wb(Color(255,255,255,255));
         g.FillPath(&wb,p);
         delete p;
      }
      if(selected || hinted) {
         Color oc = selected ? Color(100,30,144,255) : Color(100,255,200,0);
         Color bc = selected ? Color(230,30,144,255) : Color(230,255,200,0);
         GraphicsPath* p = RRect((float)x,(float)y,(float)cw,(float)ch,(float)m_lay->cornerR);
         SolidBrush ob(oc); g.FillPath(&ob,p);
         Pen bp(bc,2.5f);   g.DrawPath(&bp,p);
         delete p;
      }
   }

   void drawBack(Graphics& g, int x, int y) const {
      drawShadow(g, x, y);
      int cw=m_lay->cardW, ch=m_lay->cardH;
      Bitmap* bmp = GetCardScaled("B1", cw, ch);
      if(bmp) {
         g.DrawImage(bmp, x, y, cw, ch);
         return;
      }
      // Fallback: blue gradient
      GraphicsPath* p = RRect((float)x,(float)y,(float)cw,(float)ch,(float)m_lay->cornerR);
      Point p1(x,y), p2(x+m_lay->cardW, y+m_lay->cardH);
      LinearGradientBrush gb(p1,p2,Color(255,20,80,180),Color(255,10,40,120));
      g.FillPath(&gb,p);
      Pen pen(Color(200,180,180,180),0.8f); g.DrawPath(&pen,p);
      delete p;
   }

   void drawEmpty(Graphics& g, int x, int y, const wchar_t* label) const {
      GraphicsPath* p = RRect((float)x,(float)y,(float)m_lay->cardW,(float)m_lay->cardH,(float)m_lay->cornerR);
      SolidBrush fb(Color(30,0,0,0));
      g.FillPath(&fb,p);
      float dashes[]={4.f,4.f};
      Pen dp(Color(80,255,255,255),1.5f);
      dp.SetDashPattern(dashes,2);
      g.DrawPath(&dp,p);
      delete p;
      if(label && *label) {
         float fs = std::max(7.f, 11.f * m_lay->cardW / 71.f);
         FontFamily ff(L"Segoe UI");
         Font f(&ff, fs, FontStyleRegular, UnitPoint);
         SolidBrush lb(Color(110,255,255,255));
         RectF bounds;
         g.MeasureString(label,-1,&f,PointF(0,0),&bounds);
         g.DrawString(label,-1,&f,
            PointF(x+(m_lay->cardW-bounds.Width)/2.f,
                   y+(m_lay->cardH-bounds.Height)/2.f),&lb);
      }
   }

   void drawHighlight(Graphics& g, int x, int y) const {
      float r = (float)m_lay->cornerR + 3;
      GraphicsPath* p = RRect((float)x-3,(float)y-3,
         (float)m_lay->cardW+6,(float)m_lay->cardH+6, r);
      Pen pen(Color(210,255,240,0),2.5f);
      g.DrawPath(&pen,p);
      delete p;
   }

   void setLayout(const Layout* lay) { m_lay = lay; }

   // Draw empty foundation slot with suit symbol above and "A" below
   void drawEmptyFound(Graphics& g, int x, int y, const wchar_t* /*ignored*/, int slot) const {
      // Dashed border
      GraphicsPath* p = RRect((float)x,(float)y,(float)m_lay->cardW,(float)m_lay->cardH,(float)m_lay->cornerR);
      SolidBrush fb(Color(30,0,0,0));
      g.FillPath(&fb,p);
      float dashes[]={4.f,4.f};
      Pen dp(Color(80,255,255,255),1.5f);
      dp.SetDashPattern(dashes,2);
      g.DrawPath(&dp,p);
      delete p;

      const wchar_t* suits[]={L"♠",L"♠",L"♥",L"♥",L"♦",L"♦",L"♣",L"♣"};
      bool isRed=(slot==2||slot==3||slot==4||slot==5);
      // Card-accurate red (like the cards); black suits are solid dark
      Color suitCol = isRed
         ? Color(180, 200, 0, 0)      // vivid card red, semi-transparent
         : Color(180,  20,20,20);     // near-black

      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      // Sizes: "A" slightly larger than suit symbol
      float fsA  = std::max(10.f, cw*0.26f);
      float fsSuit = std::max(9.f,  cw*0.32f);

      FontFamily ffA(L"Georgia");
      Font fontA(&ffA, fsA, FontStyleBold, UnitPoint);
      SolidBrush brA(suitCol);
      RectF ab; g.MeasureString(L"A",-1,&fontA,PointF(0,0),&ab);

      FontFamily ffS(L"Segoe UI");
      Font fontS(&ffS, fsSuit, FontStyleBold, UnitPoint);
      SolidBrush brS(suitCol);
      RectF sb; g.MeasureString(suits[slot],-1,&fontS,PointF(0,0),&sb);

      // Total content height; centre block vertically in card
      float totalH = ab.Height + 4.f + sb.Height;
      float startY = y + (ch - totalH) / 2.f;

      // "A" on top
      g.DrawString(L"A",-1,&fontA,
         PointF(x+(cw-ab.Width)/2.f, startY), &brA);

      // Suit symbol below "A"
      g.DrawString(suits[slot],-1,&fontS,
         PointF(x+(cw-sb.Width)/2.f, startY+ab.Height+4.f), &brS);
   }

private:
   const Layout* m_lay = nullptr;

   void drawShadow(Graphics& g, int x, int y) const {
      int off = std::max(1, m_lay->cardW/40);
      GraphicsPath* sh = RRect((float)x+off,(float)y+off,
         (float)m_lay->cardW,(float)m_lay->cardH,(float)m_lay->cornerR);
      SolidBrush shb(Color(50,0,0,0));
      g.FillPath(&shb,sh);
      delete sh;
   }
};
