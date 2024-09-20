#include "Display/Deck.h"

#pragma once

class DeckController {
public:
    void addDeckAsFirst(Deck *d);
    void addDeck(Deck *d);
    void update(); // redraw (if needed).
    void first();
    bool next(); // returns true until there are no more pages.
    void close();
private:
    bool _is_showing = false;
    std::list<Deck *>_decks;
    std::list<Deck *>::iterator _currentDeck;
};

