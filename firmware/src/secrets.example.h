#pragma once

// Copy this file to secrets.h (same directory) and fill it in. secrets.h is git-ignored.
//
// There's no on-device provisioning yet, so these are compiled into the firmware. The ESP32 Wi-Fi
// stack also persists whatever it's given to flash, so flashing a build that still has these
// placeholders will overwrite credentials a previous firmware stored.

#define SHOPPING_LIST_WIFI_SSID "your-wifi-ssid"
#define SHOPPING_LIST_WIFI_PASSWORD "your-wifi-password"

// Base URL of the server on your LAN, no trailing slash. Plain HTTP: the device only ever talks to
// the server over the local network.
#define SHOPPING_LIST_SERVER_URL "http://192.168.1.50:8000"
