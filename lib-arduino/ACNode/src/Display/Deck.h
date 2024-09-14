#pragma once

#include "Display/Display.h"
#include "ACBase.h"
#include "ACBaseNode.h"

class Deck {
    Deck(ACNodeBase * node, Display * display) : _acnode(node), _display(display) {};
    void display(bool refresh);
protected:
    ACNodeBase * _acnode;
    Display * _display;
    virtual void render_pane(bool refresh) {
        _display->print("*****\nNOT IMPLEMENTED\n*****");
    };
};
class InfoDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class SNTPDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class FirmwareDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class MqttDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class QrDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class LogQrDeck : public Deck {
    virtual void render_pane(bool refresh);
};
class ButtonsDeck : public Deck {
    virtual void render_pane(bool refresh);
};
