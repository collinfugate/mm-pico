// Network link for boards with the Wi-Fi chip (Pico W).
// Only compiled when the build defines MM_WIFI; see CMakeLists.txt.
#ifndef MM_WIFI_H
#define MM_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_PORT 6060          // TCP for commands, UDP for discovery

// What the Wi-Fi side is doing; byte 6 of the status reply.
#define WIFI_MODE_OFF     0     // no Wi-Fi chip answered
#define WIFI_MODE_JOINING 1     // trying to get onto the saved network
#define WIFI_MODE_ONLINE  2     // on the saved network
#define WIFI_MODE_SETUP   3     // hosting its own "MultiMetrics-Setup" network

void wifi_init(void);           // start the chip, then join the saved network or host the setup one
void wifi_poll(void);           // call from the main loop
void wifi_led_put(bool on);     // the Pico W onboard LED hangs off the Wi-Fi chip
void wifi_status(uint8_t *out); // 8 bytes: link state, IP address (4), settings saved, mode, 0

// Changing the saved network. Text is zero terminated or runs to `max` bytes.
void wifi_set_name(const uint8_t *text, uint32_t max);
void wifi_set_password(const uint8_t *text, uint32_t max);
bool wifi_save(void);           // store both and restart; an empty name forgets the network

#endif
