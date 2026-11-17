#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "platform/hal.h"

// Wi-Fi access point. The SSID is SFOS- plus the last two MAC bytes.
class WiFiManager {
public:
    WiFiManager();

    bool setupAP();
    const char* getSSID() const { return _apSSID; }

private:
    char _apSSID[32];
};

#endif // WIFI_MANAGER_H
