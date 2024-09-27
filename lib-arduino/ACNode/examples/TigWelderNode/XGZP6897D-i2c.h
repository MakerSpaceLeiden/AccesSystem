#include <Wire.h>

// Datasheet: ; https://cfsensor.com/wp-content/uploads/2022/11/XGZP6847D-Pressure-Sensor-V2.5.pdf

#define XGZP6897D_DEFAULT_ADDR (0x6d) // i2c default address

#define CMD (0x30)   // conversion command
#define SCO (1<<3)   // conversion complete/requested bit
#define BOTH (0b010) // which conversions to run (both, no sleep)
#define PRESS (0x06) // regiser for pressure, 3 long, big endian, two complement
#define TMEMP (0x09) // registerfor temperature, 2 long, big endian, two complement

class XGZP6897D {
public:
    // For K - see page 9 of datasheet
    //
    // Pressure        K
    // range(kpa)  (divisor)
    // 500<P≤1000      8
    // 260<P≤500      16
    // 130<P≤260      32
    // 65<P≤130       64
    // 32<P≤65       128
    // 16<P≤32       256
    // 8<P≤16        512
    // 4<P≤8        1024
    // 2≤P≤4        2048
    // 1≤P<2        4096
    //
    XGZP6897D(uint16_t Kdiv, uint8_t i2caddr = XGZP6897D_DEFAULT_ADDR,  TwoWire* wire = &Wire) :
    _wire(wire), _K(Kdiv), _i2caddr(i2caddr) {};
    
    float getTemperatureInC() {
        _readBoth();
        return _temperature;
    };
    
    float getPressureInPa() {
        _readBoth();
        return _pressure;
    };
    const float ERRVAL = -999;
private:
    uint8_t _i2caddr;
    TwoWire * _wire;
    unsigned long _K;
    float _temperature, _pressure;
    
    void _startConversion() {
        _wire->beginTransmission(_i2caddr);
        _wire->write(CMD);
        _wire->write(SCO | BOTH);
        _wire->endTransmission();
    };
    
    bool _checkConversion() {
        _wire->beginTransmission(_i2caddr);
        _wire->write(CMD);
        _wire->endTransmission();
        _wire->requestFrom(_i2caddr, (uint8_t)1);
        return (_wire->read() & SCO);
    };
    
    void _readBoth() {
        _startConversion();
        
        // conversion should take 20 mSeconds; so
        // bail out after 40.
        //
        for(int i = 0; !_checkConversion(); i++) {
            if (i > 7) {
                _pressure = _temperature = 0;
                return;
            }
            delay(5);
        };
        
        _wire->beginTransmission(_i2caddr);
        _wire->write(PRESS); // pressure is the first 3 byts; so reading 5 byts from here also gets the two for temperature.
        _wire->endTransmission();
        
        uint8_t r = _wire->requestFrom(_i2caddr, (uint8_t)(3+2));
        if (r < 5) {
            _pressure = _temperature = ERRVAL;
            return;
        };
        
        unsigned char buff[5];
        for(int i = 0; i < 5; i++)
            buff[i] = _wire->read();
        
        // Read in big endian order, preserve two complement by keeping top byte high
        int32_t p = (buff[0] << 24) | (buff[1] << 16) | (buff[2] << 8);
        p >>= 8; // And shift to he normal range; preserving sign.
        int16_t t = (buff[3] <<  8) | (buff[4]);      // big endian order, two complement
        
        _pressure = float(p) / _K;
        _temperature = float(t) / 256.;
    };
};
