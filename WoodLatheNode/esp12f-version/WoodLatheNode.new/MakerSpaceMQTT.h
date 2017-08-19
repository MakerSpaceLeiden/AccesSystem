#ifndef _MSMQTTH_H
#define _MSMQTTH_H


// MQTT limits - which are partly ESP chip rather than protocol specific.
// MQTT limits - which are partly ESP chip rather than protocol specific.
#define MAX_NAME       24
#define MAX_TOPIC      64
#define MAX_MSG        (MQTT_MAX_PACKET_SIZE - 32)
#define MAX_TAG_LEN    10/* Based on the MFRC522 header */
#define BEATFORMAT     "%012u" // hard-coded - it is part of the HMAC */
#define MAX_BEAT       16

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT (1883)
#endif

class ACNode {
  public:
    char mqtt_topic_prefix[MAX_TOPIC - 2 * MAX_NAME];
    const char mqtt_server[34];
    const uint16_t mqtt_port = 1883;
    const char nodename[MAX_NAME];
    const char machine[MAX_NAME];
    const char master[MAX_NAME];
    const char logpath[MAX_NAME];
    const char passwd[MAX_NAME];
    JsonObject configuration;

    ACNode(void);
        
    virtual void begin();
    virtual void updateConfig(JsonObject * newCnf);
    virtual void send(const char *payload);
    virtual int process(const char * topic, const char * command, unsigned int beat, const char * rest);
    virtual void loop();

  private:
    PubSubClient client;
    DynamicJsonBuffer jsonBuffer;
    void callback(char* topic, byte * payload_theirs, unsigned int length);
};
#endif

