#include "Display/Deck.h"
#include "Display/DeckController.h"
#include "Display/fonts.h"

void Deck::display(bool refresh) {
    if (refresh)
        _display->clearDisplay();
    
    _display->setFont(FONT_SMALL);
    _display->setTextSize(1);
    _display->setTextColor(SH110X_WHITE);
    _display->setCursor(0, 0);

    // virtual - i.e. call the top level implementation.
    render_pane(refresh);
    
    _display->display();
};
