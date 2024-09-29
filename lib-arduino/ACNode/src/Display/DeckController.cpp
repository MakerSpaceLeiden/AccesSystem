#include "Display/DeckController.h"

void DeckController::addDeck(Deck *d) {
    _decks.insert(_decks.end(), d);
};

void DeckController::addDeckAsFirst(Deck *d) {
    _decks.insert(_decks.begin(), d);
};

void DeckController::update() { // redraw (if needed).
    if (!_is_showing)
        return;
    Deck * d = * _currentDeck;
    if (d)
        d->display(false);
};

void DeckController::close() {
    _is_showing = false;
    _currentDeck = _decks.begin();
};

void DeckController::first() {
    _is_showing = true;
    _currentDeck = _decks.begin();
    Deck * d = *_currentDeck;
    d->display(true);
}

// returns true until there are no more pages. But we
// leave it up to the callee to figure out what to do,
// e.g. go back to first(); to call close().
bool DeckController::next() {
    _currentDeck++;
    if (_currentDeck == _decks.end()) {
        _currentDeck = _decks.begin();
        _is_showing = false;
        return false;
    };
    _is_showing = true;
    Deck * d = *_currentDeck;
    d->display(true);
    return true;
};
