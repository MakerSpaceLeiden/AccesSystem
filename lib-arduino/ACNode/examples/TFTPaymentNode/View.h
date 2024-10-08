// We keep the price planel on a sprite; so it mecomes easier
// scroll smoothly without flickering as we do not have enough
// memory for double buffering.
//
static void drawPricePanel(int offset, int amount) {
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.loadFont(AA_FONT_LARGE);
  spr.drawString(amounts[amount], offset + tft.width() / 2, 0);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);

  spr.loadFont(AA_FONT_SMALL);
  spr.drawString(descs[amount], offset + tft.width() / 2, 39);

  spr.loadFont(AA_FONT_LARGE);
  spr.drawString(prices[amount], offset + tft.width() / 2, spr.height() - 16); // we know it is only digits.
};

static void drawPricePanels(int left, int right) {
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  drawPricePanel(0, left);
  drawPricePanel(tft.width() , right);
}

static void scrollpanel_loop() {
#ifdef SPRITESCROLL
  static int last_amount = amount;
  if (amount == last_amount) {
#endif
    drawPricePanels(amount, amount);
    spr.pushSprite(0, 32);
    return;
#ifdef SPRITESCROLL
  };
  if ((amount - last_amount + NA) % NA == 1) {
    drawPricePanels(last_amount, amount);
    for (int x = 0; x < tft.width() + 4 /* intentional overshoot */;  x += slide_speed(x)) {
      spr.pushSprite(-x, 32 );
    }
    spr.pushSprite(tft.width(), 32); // Snap back
  } else {
    drawPricePanels( amount, last_amount);
    for (int x = 0; x < tft.width() + 4 /* intentional overshoot */; x += slide_speed(x)) {
      spr.pushSprite(-tft.width() + x, 32 );  // Snap back
    }
    spr.pushSprite(0, 32);
  }
  last_amount = amount;
#endif
}


void updateDisplay(state_t md)
{
  // Serial.printf("Update %d\n", md);
  tft.fillScreen(TFT_BLACK);
  updateClock(true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  switch (md) {
    case BOOT:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("MSL Payments", tft.width() / 2, tft.height() / 2 - 10);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(version, tft.width() / 2, tft.height() / 2 + 26);
      tft.drawString(__DATE__, tft.width() / 2, tft.height() / 2 + 42);
      tft.drawString(__TIME__, tft.width() / 2, tft.height() / 2 + 60);
      break;
    case WAITING_FOR_NTP:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("get time...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case FETCH_CA:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("security...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("registering...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER_PRICELIST:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("pricelist...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case WAIT_FOR_REGISTER_SWIPE:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("new terminal", tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_MEDIUM);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("swipe admin tag", tft.width() / 2, tft.height() / 2 + 20);
      break;
    case ENTER_AMOUNT:
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      if (NA) {
        tft.drawString("Swipe to pay", tft.width() / 2, 20);
        scrollpanel_loop();
      } else {
        tft.drawString("- no articles -", tft.width() / 2, 20);
      }
      break;
    case OK_OR_CANCEL:
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("cancel", tft.width() / 2 - 30, tft.height()  - 12);
      tft.drawString("OK", tft.width() / 2 + 48, tft.height()  - 12);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("PAY", tft.width() / 2, tft.height() / 2 - 52);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(amounts[amount], tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(prices[amount], tft.width() / 2, tft.height() / 2 + 15);
      tft.drawString(" ? ", tft.width() / 2, tft.height() / 2 + 35);
      break;
    case DID_CANCEL:
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("aborted", tft.width() / 2, tft.height() / 2 - 52);
      break;
    case DID_OK:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("paying..", tft.width() / 2, tft.height() / 2 - 52);
      break;
  }
}
