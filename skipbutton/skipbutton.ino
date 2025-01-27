#include <stdint.h>
#include <stdio.h>

#include "Arduino.h"
#include "WifiEspNow.h"
#include "ESP8266WiFi.h"
#include "EEPROM.h"

static const char AP_NAME[] = "revspace-espnow";
static const uint16_t LONG_PRESS_DELAY = 1500; // milliseconds

typedef enum {
    E_SEND,
    E_ACK,
    E_DISCOVER,
    E_SLEEP,
    E_LONG_PRESS
} skip_mode_t;

static skip_mode_t mode = E_SEND;

void setup(void)
{
    // welcome
    Serial.begin(115200);
    Serial.println("\nESPNOW-SKIP");

    // show blue LED to indicate we are on
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);

    WifiEspNow.begin();
    EEPROM.begin(512);
}

static bool find_ap(const char *name, struct WifiEspNowPeerInfo *peer)
{
    // scan for networks and try to find our AP
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        Serial.println(WiFi.SSID(i));
        if (strcmp(name, WiFi.SSID(i).c_str()) == 0) {
            // copy receiver data
            peer->channel = WiFi.channel(i);
            memcpy(peer->mac, WiFi.BSSID(i), sizeof(peer->mac));
            return true;
        }
    }
    // not found
    return false;
}

static void send_topic_text(uint8_t *mac, const char *topic, const char *text)
{
    char buf[250];
    int n = snprintf(buf, sizeof(buf), "%s %s", topic, text);
    WifiEspNow.send(mac, (uint8_t *)buf, n);
}

static bool valid_peer(struct WifiEspNowPeerInfo *peer)
{
    return (peer->channel >= 1) && (peer->channel <= 14);
}

void loop(void)
{
    WifiEspNowSendStatus status;
    struct WifiEspNowPeerInfo recv;
    char line[128];

    switch (mode) {

    case E_SEND:
        // read last known receiver info from EEPROM
        EEPROM.get(0, recv);
        if (valid_peer(&recv)) {
            // send SKIP message to last known address
            Serial.printf(line, "Sending SKIP to %02X:%02X:%02X:%02X:%02X:%02X (chan %d)...",
                    recv.mac[0], recv.mac[1], recv.mac[2], recv.mac[3], recv.mac[4], recv.mac[5], recv.channel);

            WifiEspNow.addPeer(recv.mac, recv.channel, nullptr);
            send_topic_text(recv.mac, "revspace/button/skip", "jump_fwd");

            mode = E_ACK;
        } else {
            mode = E_DISCOVER;
        }
        break;

    case E_ACK:
        // wait for tx ack
        status = WifiEspNow.getSendStatus();
        switch (status) {
        case WifiEspNowSendStatus::NONE:
            if (millis() > 3000) {
                Serial.println("TX ack timeout");
                mode = E_DISCOVER;
            }
            break;
        case WifiEspNowSendStatus::OK:
            Serial.println("TX success");
            mode = E_SLEEP;
            break;
        case WifiEspNowSendStatus::FAIL:
        default:
            Serial.println("TX failed");
            mode = E_DISCOVER;
            break;
        }
        break;

    case E_DISCOVER:
        Serial.println("Discovering master ...");
        if (find_ap(AP_NAME, &recv)) {
            // save it in EEPROM
            Serial.printf(line, "found '%s' at %02X:%02X:%02X:%02X:%02X:%02X (chan %d), saving to EEPROM\n", AP_NAME,
                recv.mac[0], recv.mac[1], recv.mac[2], recv.mac[3], recv.mac[4], recv.mac[5], recv.channel);
            EEPROM.put(0, recv);
            EEPROM.end();
        } else {
            Serial.println("no master found!");
        }
        mode = E_SLEEP;
        break;

    // sorry, I am lazy and I replaced deepsleep by regular delay

    case E_SLEEP:
    default:
        Serial.printf("Going to sleep...was awake for %ld millis", millis());
        delay(LONG_PRESS_DELAY);
        mode = E_LONG_PRESS;
        break;

    // sorry, I am lazy and just copypasted the anove

    case E_LONG_PRESS:
        // read last known receiver info from EEPROM
        EEPROM.get(0, recv);
        if (valid_peer(&recv)) {
            // send SKIP message to last known address
            Serial.printf(line, "Sending STOP to %02X:%02X:%02X:%02X:%02X:%02X (chan %d)...",
                    recv.mac[0], recv.mac[1], recv.mac[2], recv.mac[3], recv.mac[4], recv.mac[5], recv.channel);

            WifiEspNow.addPeer(recv.mac, recv.channel, nullptr);
            send_topic_text(recv.mac, "revspace/button/stop", "stop");

            mode = E_ACK;
        } else {
            mode = E_DISCOVER;
        }
        break;

        // yes, it's an infinite loop. It wil skip every 1.5 seconds as long as you hold the button
    }
}
