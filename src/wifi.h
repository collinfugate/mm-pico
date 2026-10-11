// Network link for boards with the Wi-Fi chip (Pico W).
// Only compiled when the build defines MM_WIFI; see CMakeLists.txt.
#ifndef MM_WIFI_H
#define MM_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_PORT 6060          // TCP for commands, UDP for discovery

void wifi_init(void);           // start the chip and begin joining the saved network
void wifi_poll(void);           // call from the main loop
void wifi_led_put(bool on);     // the Pico W onboard LED hangs off the Wi-Fi chip
void wifi_status(uint8_t *out); // fills 8 bytes: link state, IP address, settings present

#endif
