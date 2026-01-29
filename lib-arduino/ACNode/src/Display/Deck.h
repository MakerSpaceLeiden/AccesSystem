#pragma once

#include <ESPAsyncWebServer.h>
#include <TLog.h>

// #include "ACBase.h"
// #include "ACBaseNode.h"

class DeckController;
class ACNodeBase;

class Deck {
public:
    Deck(ACNodeBase * node) : _acnode(node) {};
    virtual void display(bool refresh = true);
    virtual void render_pane(bool refresh);

protected:
    ACNodeBase * _acnode;

    friend class DeckController;
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
    QrDeck(ACNodeBase * node, const char * path) : Deck(node), _str(path) {};
    virtual void render_pane(bool refresh);
private:
    const char * _str;
};
class LogQrDeck : public Deck {
public:
    LogQrDeck(ACNodeBase * node) : Deck(node) {};
    virtual void render_pane(bool refresh);
};
