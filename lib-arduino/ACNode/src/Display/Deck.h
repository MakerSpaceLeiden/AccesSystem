#pragma once

#include "Display/Display.h"
#include "ACBase.h"
#include "ACBaseNode.h"
class DeckController;

extern Display * _display;

class Deck {
public:
    Deck(ACNodeBase * node) : _acnode(node) {};
    ACNodeBase * _acnode;

    virtual void render_pane(bool refresh) {
        _display->print("*****\nNOT IMPLEMENTED\n*****");
    };
    void display(bool refresh);
};

class DeckController {
public:
    void addDeck(Deck *d);
    void update(); // redraw (if needed).
    void first();
    bool next(); // returns true until there are no more pages.
private:
    bool _is_showing = false;
    std::list<Deck *>_decks;
    std::list<Deck *>::iterator _currentDeck;
};

class InfoDeck : public Deck {
public:
    InfoDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
class SNTPDeck : public Deck {
public:
    SNTPDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
class FirmwareDeck : public Deck {
public:
    FirmwareDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
class MqttDeck : public Deck {
public:
    MqttDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
class QrDeck : public Deck {
public:
    QrDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(const char * item, bool refresh);
};
class LogQrDeck : public Deck {
public:
    LogQrDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
