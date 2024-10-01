#include "Display/Deck.h"
#include "ACNode.h"

#pragma once

class DeckController : public ACBase {
public:
    void addDeckAsFirst(Deck *d);
    void addDeck(Deck *d);

    void first();
    bool next(); // returns true until there are no more pages.
    void close();
    
    void loop();

    Deck * current() { return _is_showing ? *_currentDeck : NULL; };
private:
    bool _is_showing = false;
    
    std::list<Deck *>_decks;
    std::list<Deck *>::iterator _currentDeck;
};

