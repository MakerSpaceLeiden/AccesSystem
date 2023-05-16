#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>



class MachineState : public ACBase {
  public:
    static const time_t NEVER = 0;

    typedef enum {
	BOOTING = 1,                  /* Startup state */
        OUTOFORDER,               /* device not functional.  */
        REBOOT,                   /* forcefull reboot  */
        TRANSIENTERROR,           /* hopefully goes away level error  */
        NOCONN,                   /* sort of fairly hopless (though we can cache RFIDs!)  */
        WAITINGFORCARD,           /* waiting for card. */
        CHECKINGCARD,             /* checkeing card. with server */
	// kept free */
	START_PRIVATE_STATES = 100,
	PSTATE_101 = 101, PSTATE_102 = 102, PSTATE_103 = 103, PSTATE_104 = 104, PSTATE_105 = 105, 
	PSTATE_106 = 106, PSTATE_107 = 107, PSTATE_108 = 108, PSTATE_109 = 109, PSTATE_110 = 110, 
	PSTATE_111 = 111, PSTATE_112 = 112, PSTATE_113 = 113, PSTATE_114 = 114, PSTATE_115 = 115, 
	PSTATE_116 = 116, PSTATE_117 = 117, PSTATE_118 = 118, PSTATE_119 = 119, PSTATE_120 = 120, 
	PSTATE_121 = 121, PSTATE_122 = 122, PSTATE_123 = 123, PSTATE_124 = 124, PSTATE_125 = 125, 
	PSTATE_126 = 126, PSTATE_127 = 127, PSTATE_128 = 128, PSTATE_129 = 129, PSTATE_130 = 130, 
	PSTATE_131 = 131, PSTATE_132 = 132, PSTATE_133 = 133, PSTATE_134 = 134, PSTATE_135 = 135, 
	PSTATE_136 = 136, PSTATE_137 = 137, PSTATE_138 = 138, PSTATE_139 = 139, PSTATE_140 = 140, 
	PSTATE_141 = 141, PSTATE_142 = 142, PSTATE_143 = 143, PSTATE_144 = 144, PSTATE_145 = 145, 
	PSTATE_146 = 146, PSTATE_147 = 147, PSTATE_148 = 148, PSTATE_149 = 149, PSTATE_150 = 150, 
	PSTATE_151 = 151, PSTATE_152 = 152, PSTATE_153 = 153, PSTATE_154 = 154, PSTATE_155 = 155, 
	PSTATE_156 = 156, PSTATE_157 = 157, PSTATE_158 = 158, PSTATE_159 = 159, PSTATE_160 = 160, 
	PSTATE_161 = 161, PSTATE_162 = 162, PSTATE_163 = 163, PSTATE_164 = 164, PSTATE_165 = 165, 
	PSTATE_166 = 166, PSTATE_167 = 167, PSTATE_168 = 168, PSTATE_169 = 169, PSTATE_170 = 170, 
	PSTATE_171 = 171, PSTATE_172 = 172, PSTATE_173 = 173, PSTATE_174 = 174, PSTATE_175 = 175, 
	PSTATE_176 = 176, PSTATE_177 = 177, PSTATE_178 = 178, PSTATE_179 = 179, PSTATE_180 = 180, 
	PSTATE_181 = 181, PSTATE_182 = 182, PSTATE_183 = 183, PSTATE_184 = 184, PSTATE_185 = 185, 
	PSTATE_186 = 186, PSTATE_187 = 187, PSTATE_188 = 188, PSTATE_189 = 189, PSTATE_190 = 190, 
	PSTATE_191 = 191, PSTATE_192 = 192, PSTATE_193 = 193, PSTATE_194 = 194, PSTATE_195 = 195, 
	PSTATE_196 = 196, PSTATE_197 = 197, PSTATE_198 = 198, PSTATE_199 = 199, PSTATE_200 = 200, 
	PSTATE_201 = 201, PSTATE_202 = 202, PSTATE_203 = 203, PSTATE_204 = 204, PSTATE_205 = 205, 
	PSTATE_206 = 206, PSTATE_207 = 207, PSTATE_208 = 208, PSTATE_209 = 209, PSTATE_210 = 210, 
	PSTATE_211 = 211, PSTATE_212 = 212, PSTATE_213 = 213, PSTATE_214 = 214, PSTATE_215 = 215, 
	PSTATE_216 = 216, PSTATE_217 = 217, PSTATE_218 = 218, PSTATE_219 = 219, PSTATE_220 = 220, 
	PSTATE_221 = 221, PSTATE_222 = 222, PSTATE_223 = 223, PSTATE_224 = 224, PSTATE_225 = 225, 
	PSTATE_226 = 226, PSTATE_227 = 227, PSTATE_228 = 228, PSTATE_229 = 229, PSTATE_230 = 230, 
	PSTATE_231 = 231, PSTATE_232 = 232, PSTATE_233 = 233, PSTATE_234 = 234, PSTATE_235 = 235, 
	PSTATE_236 = 236, PSTATE_237 = 237, PSTATE_238 = 238, PSTATE_239 = 239, PSTATE_240 = 240, 
	PSTATE_241 = 241, PSTATE_242 = 242, PSTATE_243 = 243, PSTATE_244 = 244, PSTATE_245 = 245, 
	PSTATE_246 = 246, PSTATE_247 = 247, PSTATE_248 = 248, PSTATE_249 = 249, PSTATE_250 = 250, 
	PSTATE_251 = 251, PSTATE_252 = 252, PSTATE_253 = 253, PSTATE_254 = 254, 
	// end 
        ALL_STATES = 255
	} machinestates_t;

    typedef uint8_t machinestate_t;

    typedef std::function<void(machinestate_t currentState)> THandlerFunction_OnLoopCB;
    typedef std::function<void(machinestate_t oldState, machinestate_t newState)> THandlerFunction_OnChangeCB; // in & out
    typedef std::function<void(machinestate_t currentState)> THandlerFunction_OnTimeoutCB;

  private:
    typedef struct {
      const char * label;                   /* name of this state */
      LED::led_state_t ledState;            /* flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1. */
      time_t maxTimeInMilliSeconds;         /* how long we can stay in this state before we timeout. */
      machinestate_t failStateOnTimeout;   /* what state we transition to on timeout. */
      unsigned long timeInState;
      unsigned long timeoutTransitions;
      unsigned long autoReportCycle;

      THandlerFunction_OnLoopCB onLoopCB;
      THandlerFunction_OnChangeCB onChangeCB;
      THandlerFunction_OnTimeoutCB onTimeoutCB;
    } state_t;
    state_t * _state2stateStruct[256];

    machinestate_t machinestate, laststate;
    unsigned long laststatechange, lastReport;

    state_t * _initState(uint8_t state, state_t dflt);
    state_t * _initState(uint8_t state, state_t * dflt);

  public:
    const char * label();
    const char * label(uint8_t label);
    MachineState();

    machinestate_t state();

    void operator=(machinestate_t s);
    void setState(machinestate_t s);

    void setOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB);

    void setOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB);

    void setOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB);

    uint8_t addState(state_t aState);

    uint8_t addState(const char * label, machinestate_t nextstate);

    uint8_t addState(const char * label, time_t timeout, machinestate_t nextstate);

    uint8_t addState(const char * label, LED::led_state_t ledState, 
		time_t timeout, machinestate_t nextstate);

    void defineState(machinestate_t state,
		const char * label, 
		LED::led_state_t ledState = LED::LED_ERROR,
		time_t timeout = NEVER, 
		machinestate_t nextstate = WAITINGFORCARD,
	     	unsigned long timeInState = NEVER,
		unsigned long timeoutTransitions = NEVER, 
		unsigned long autoReportCycle = NEVER,
		THandlerFunction_OnLoopCB onLoopCB = NULL, 
		THandlerFunction_OnChangeCB onChangeCB = NULL, 
		THandlerFunction_OnTimeoutCB onTimeoutCB = NULL);

    // ACBase - standard handlers.
    //
    void begin();
    void report(JsonObject& report);
    void loop();
};
