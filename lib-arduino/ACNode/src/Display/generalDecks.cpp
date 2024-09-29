#include "Display/Deck.h"
#include <esp_sntp.h>
#include "lwip/ip_addr.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

#define QR_URL_REDIRECT_TEMPLATE "https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_%s"

void InfoDeck::render_pane(bool refresh) {
    if (!refresh) return;
    _display->clearDisplay();
    _display->print_centred("INFO");
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

    _display->print_centred("SNTP");
    _display->printf("Date :%s\n",ds);
    _display->printf("Time :%s\n",ts);
    
#if 0 // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(3, 0, 0)
    sntp_sync_status_t  s = sntp_get_sync_status();
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
#endif
};

void FirmwareDeck::render_pane(bool refresh) {
    if (!refresh) return;
    _display->print_centred("Firmware");
    _display->printf("Dev :%s\n", _acnode->name());
    _display->printf("Date:%s\n",__DATE__);
    _display->printf("Time:%s\n",__TIME__);
};

void MqttDeck::render_pane(bool refresh) {
    if (!refresh) return;
    _display->print_centred("MQTT");
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
    _display->printf("Port :%u\n",_acnode->mqtt_port);
    _display->printf("Topic:%s/%s\n",_acnode->mqtt_topic_prefix,_acnode->logpath);
};

void QrDeck::render_pane(bool refresh) {
    if (!refresh) return;
    char url[128];
    snprintf(url,sizeof(url),QR_URL_REDIRECT_TEMPLATE,_str);
    _display->print_centered_QR("wiki", url);
};

void LogQrDeck::render_pane(bool refresh) {
    if (!refresh) return;
    char url[32];
    snprintf(url,sizeof(url),"http://%s/",String(_acnode->localIP().toString()).c_str());
    _display->print_centered_QR((char *)"view log", url);
};

