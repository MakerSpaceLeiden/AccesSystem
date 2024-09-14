#include "Display/Deck.h"
#include <esp_sntp.h>

void Deck::display(bool refresh) {
    if (refresh) {
        _display->clearDisplay();
        _display->setTextSize(1);
        _display->setTextColor(SH110X_WHITE);
        _display->setCursor(0, 0);
    };
    _display->setFont(NULL); // Fairly large 5x7 font
    
    // virtual - i.e. call the top level implementation.
    render_pane(refresh);
    
    _display->display();
};

void InfoDeck::render_pane(bool refresh) {
    _display->println("   -- INFO --");
    _display->printf("Node :%s\n",_acnode->moi);
    _display->printf("IPv4 :%s\n", String(_acnode->localIP().toString()).c_str());
    _display->printf("Via  :%s\n", _acnode->_wired ? "LAN" : "WiFi");
#ifdef SYSLOG_HOST
    _display->printf("Syslg:%s\n", SYSLOG_HOST);
#else
    _display->printf("Syslg:OFF\n");
#endif
    _display->printf("Up   :%s\n",_acnode->uptime().c_str());
    _display->printf("CPU  :%.1f%cC\n", coreTemp(),ADAFRUIT_GFX_DEGREE_SYMBOL);
    _display->printf("Heap :%.1fkB\n", ESP.getFreeHeap() / 1024.);
};

void SNTPDeck::render_pane(bool refresh) {
        time_t now = time(NULL);
        struct tm * t = localtime(&now);
        char ds[10], ts[10];
        strftime(ds,sizeof(ds),"%Y-%m-%d",t);
        strftime(ts,sizeof(ts),"%H:%M:%S",t);
        sntp_sync_status_t  s = sntp_get_sync_status();
        _display->println("   -- SNTP --");
        _display->printf("Date :%s\n",ds);
        _display->printf("Time :%s\n",ts);
        _display->printf("sNTP :%s\n",esp_sntp_enabled() ?
                         (s == SNTP_SYNC_STATUS_IN_PROGRESS ? "adjusting" :
                          (s == SNTP_SYNC_STATUS_COMPLETED ? "OK" : "Pending")
                          ) : "OFF");
        for(int i = 0, j = 0; i < SNTP_MAX_SERVERS&& j < 5; i++) {
            char buff[INET6_ADDRSTRLEN];
            const char * s = esp_sntp_getservername(i);
            if (!s) {
                ip_addr_t const *ip = esp_sntp_getserver(i);
                if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL && !(ip_addr_isany(ip)))
                    s = buff;
            };
            if (s) {
                _display->printf("     :%s\n",s);
                j++;
            };
        };
    };

void FirmwareDeck::render_pane(bool refresh) {
    _display->println(" -- Firmware --");
    _display->printf("Dev :%s\n", _acnode->_name());
    _display->printf("Date:%s\n",__DATE__);
    _display->printf("Time:%s\n",__TIME__);
};

void MqttDeck::render_pane(bool refresh) {
        _display->println("    -- MQTT --");
        char buff[16],*p = _acnode->mqtt_server,*q=(char*)"Host";
        while(*p) {
            char * s = index(p,'.');
            int l = 12;
            if (s && s - p < l && strlen(p) > l) l = s - p+1;
            strncpy(buff,p,l);
            buff[l] = '\0';
            p+=strlen(buff);
            _display->printf("%s :%s\n",q,buff);
            q = (char *)"    ";
        };
        _display->printf("Port :%u\n",mqtt_port);
        _display->printf("Topic:%s/%s\n",mqtt_topic_prefix,logpath);
        _display->printf(" /%s/#\n",moi);
};

void QrDeck::render_pane(bool refresh) {
    char url[128];
    snprintf(url,sizeof(url),QR_URL_REDIRECT_TEMPLATE,moi);
    _display->_display_QR(NULL, url);
};

void LogQrDeck::render_pane(bool refresh) {
        char url[32];
        snprintf(url,sizeof(url),"http://%s/",String(localIP().toString()).c_str());
        _display->_display_QR((char *)"view log", url);
    };

void ButtonsDeck::render_pane(bool refresh) {
    typedef struct iostate {
        uint8_t pin; const char * label; int lst; int tpe;
    } iostate_t;
    static const iostate_t _s[] = {
        { BUTT0, "YES/nxt", 1, INPUT_PULLUP },
        { BUTT1, "NO/back", 1, INPUT_PULLUP },
#ifdef BUTT2
        { BUTT2, "MENU", 1, INPUT_PULLUP },
#endif
        { CURR0, "Curr 1" , 1, INPUT },
        { OPTO0, "Opto 1", 1, INPUT  },
        { OPTO1, "Opto 2", 1, INPUT },
#ifdef OPTO2
        { OPTO2, "Opto 3", 1, INPUT },
        { OPTO3, "Opto 4", 1, INPUT },
#endif
        { 255, NULL },
    };

    if (refresh)
        _display->println("    -- INPUTS --");
    
    for (int i = 0;; i++) {
        iostate_t * s = &iostates[i];
        if (!s->label)
            break;
        
        int x =  2 + (i / 4)   * SCREEN_WIDTH / 2;
        int y = 12 + (i % 4) * 11;
        
        // first time round - print the text and UI; after that
        // just deal with the updates.
        if (_pageState != page) {
            _display->drawRect(x, y, 10, 10, SH110X_WHITE);
            _display->setCursor(x + 12 , y + 1);
            _display->print(s->label);
        };
        
        // upate the on/off dot in the middle always.
        s->lst = !xdigitalRead(s->pin); // they are all pullup style
        _display->fillRect(x + 2, y + 2, 10 - 4, 10 - 4, s->lst ? SH110X_WHITE : SH110X_BLACK);
    }
};
