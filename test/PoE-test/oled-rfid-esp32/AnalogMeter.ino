#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

void drawMeter(double angle) {
  const int w = display.width();
  const int h = display.height();
  
  display.setTextColor(WHITE);

  // create more fonts at http://oleddisplay.squix.ch/
  // display.setTextAlignment(TEXT_ALIGN_CENTER);
  // display.setFont(ArialMT_Plain_10);
  // display.drawString(display.width() / 2, display.height() - 12, "regen");

  display.drawRect(0, 0, w - 1, h - 1, WHITE);

  const int total_angle = 80;
  const int start_angle = 90 + total_angle / 2;

  // rotational centre
  const int xc = w / 2;
  const int yc = h * 2; // Fairly 'under' the end of the screen - so you can get a wide swing.

  const double needleLen = h * 0.95 + (yc - h); // fill 0.95 of the display height.

  const int ticks = 7;
  const double tickLen = h * 0.05;  
  const double tickR = needleLen * 0.95; // needle overshots ticks by a bit.
  const double tickI = tickR - tickLen; // inner radius tick.
  
  for (int i = 0; i < ticks; i++) {
    double a = M_PI * (start_angle - total_angle * i / (ticks - 1)) / 180.;
    double cha = cos(a);
    double sha = sin(a);

    int x0 = xc + tickR * cha;
    int y0 = yc - tickR * sha;
    int x1 = xc + tickI * cha;
    int y1 = yc - tickI * sha;
    display.drawLine(x0, y0, x1, y1, WHITE);
  }

  display.fillCircle(xc, yc, 3, WHITE);

  if (angle < 0) angle = 0;
  if (angle > 1) angle = 1;

  double a = M_PI * (start_angle - angle * total_angle) / 180.;
  double cha = cos(a);
  double sha = sin(a);

  int dh = yc - h + 2;
  int x0 = xc + dh / tan(a);
  int y0 = h - 2;

  x0 = xc;
  y0 = yc;

  int x1 = xc + needleLen * cha;
  int y1 = yc - needleLen * sha;
  const int handwidth = 4;
  display.drawLine(x0, y0, x1, y1, WHITE);
}


