// We keep a record in NVRAM (EEPROM) of how long we've been
// welding (and in the future, perhaps when a bottle was last
// changed).
//
#include "EEPROM.h"

#define WRE_IDENT "WR03"
#define WRE_VERSION (*(unsigned long *)WRE_IDENT)
EEPROMClass welding_stats(WRE_IDENT);

typedef struct welding_rec {
    unsigned long version;
    unsigned long welding_timer;
    unsigned long bottle_date;
    char changed_by[20];
} welding_rec_t;
welding_rec_t wr;

static void welding_init() {
    welding_stats.begin(sizeof(welding_rec_t));
    size_t n = welding_stats.readBytes(0, &wr, sizeof(wr));
    
    if (n >= sizeof(wr) && wr.version == WRE_VERSION) {
        Debug.println("Welding data from EEPROM read OK");
        return;
    };
        
    // Init/populate eeprom if nothing is found or if the eeprom does not contain our version marker.
    wr = {
        .version = WRE_VERSION,
        .welding_timer = 0,
        .bottle_date = 0,
    };
    wr.changed_by[0] = 0;
    welding_stats.writeBytes(0, &wr, sizeof(wr));
    welding_stats.commit();
    
    n = welding_stats.readBytes(0, &wr, sizeof(wr));
    if (n < sizeof(wr) || wr.version != WRE_VERSION || wr.welding_timer != 0) {
        Log.println("***** Welding EEPROM corrupt");
        return;
    };

    Log.println("Welding counters in EEPROM reset");
};

static void welding_save(bool force = false) {
    // Only write things if there is an actual change.
    //
    welding_rec_t c;
    welding_stats.readBytes(0, &c, sizeof(c));
    if (wr.version = c.version && wr.welding_timer == c.welding_timer && wr.bottle_date == c.bottle_date)
        return;

    welding_stats.writeBytes(0, &wr, sizeof(wr));
    welding_stats.commit();
}

static void welding_bottle_reset(const char * name) {
    wr.welding_timer = 0;
    wr.bottle_date = time(NULL);

    // Copy 19 chars or less; or until we see
    // a space & protect us from non-ascii as
    // the display does not have those in the
    // font table. Add a terminating 0.
    //
    int i = 0; const char *p;
    for(p = name; *p && *p != ' ' && i < 19; p++) {
        if (*p > 32 && *p < 128)
            wr.changed_by[i] = *p;
        i++;
    };
    wr.changed_by[i] = 0;
    
    welding_save(true);
};
