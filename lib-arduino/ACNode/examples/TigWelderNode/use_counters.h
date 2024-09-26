// We keep a record in NVRAM (EEPROM) of how long we've been
// welding (and in the future, perhaps when a bottle was last
// changed).
//
#include "EEPROM.h"

#define WRE_IDENT "WR02"
#define WRE_VERSION (*(unsigned long *)WRE_IDENT)
EEPROMClass welding_stats(WRE_IDENT);

typedef struct welding_rec {
    unsigned long version;
    unsigned long welding_timer;
    unsigned long bottle_date;
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
        .bottle_date = 0
    };
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
    // do not record stuff under 5 seconds unless forced
    //
    static unsigned lst = wr.welding_timer;
    if (wr.welding_timer - lst < 5 && !force)
        return;
    
    welding_stats.writeBytes(0, &wr, sizeof(wr));
    welding_stats.commit();
}
