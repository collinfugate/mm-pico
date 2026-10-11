// Wi-Fi link for the Pico W.
//
// The box joins an existing Wi-Fi network and listens on WIFI_PORT. A client
// that connects over TCP sends exactly the same command bytes it would send
// over USB and gets the same reply bytes back: both routes call
// handle_command() in main_device.c.
//
// To be found without typing an address, the box answers the UDP broadcast
// "MM-DISCOVER" on the same port with "MM-PICO <model number>".
//
// The network name and password are not compiled in. flash.py writes them to
// the last 4 KB of flash when it flashes the board, and this file only reads
// them. Firmware updates leave that area alone.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "mm.h"
#include "wifi.h"

// Layout written by wifi_page() in flash.py.
typedef struct {
    char magic[4];      // "MMWF"
    char ssid[33];      // zero terminated
    char password[65];  // zero terminated, empty for an open network
} wifi_settings_t;

#define WIFI_SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - 4096)
#define REJOIN_SECONDS 15

static const wifi_settings_t *settings =
    (const wifi_settings_t *)(XIP_BASE + WIFI_SETTINGS_OFFSET);

static bool chip_ok = false;
static bool have_settings = false;
static bool servers_up = false;
static struct tcp_pcb *client = NULL;
static absolute_time_t next_check;
static int seconds_down = 0;

static void start_join(void)
{
    uint32_t auth = settings->password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    cyw43_arch_wifi_connect_async(settings->ssid, settings->password, auth);
}

// ---------------------------------------------------------------- commands (TCP)

static void wifi_reply(const void *data, uint32_t len)
{
    if (client) tcp_write(client, data, len, TCP_WRITE_FLAG_COPY);
}

static void forget_client(struct tcp_pcb *pcb)
{
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (client == pcb) client = NULL;
}

static err_t on_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void) arg; (void) err;
    if (p == NULL) {                    // the other side closed the connection
        forget_client(pcb);
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        return ERR_OK;
    }
    uint8_t buf[64] = {0};              // same size as the USB command buffer
    pbuf_copy_partial(p, buf, sizeof(buf), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    handle_command(buf, wifi_reply);
    tcp_output(pcb);                    // send the reply now, do not wait to batch it
    return ERR_OK;
}

static void on_error(void *arg, err_t err)
{
    (void) arg; (void) err;
    client = NULL;                      // lwIP has already freed the connection
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void) arg;
    if (err != ERR_OK || pcb == NULL) return ERR_VAL;
    if (client) {                       // one client at a time; the newest wins
        struct tcp_pcb *old = client;
        forget_client(old);
        tcp_abort(old);
    }
    client = pcb;
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, on_receive);
    tcp_err(pcb, on_error);
    return ERR_OK;
}

// --------------------------------------------------------------- discovery (UDP)

static void on_discover(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
    (void) arg;
    static const char question[] = "MM-DISCOVER";
    bool asked = p->tot_len >= sizeof(question) - 1 &&
                 pbuf_memcmp(p, 0, question, sizeof(question) - 1) == 0;
    pbuf_free(p);
    if (!asked) return;

    char answer[24];
    int len = snprintf(answer, sizeof(answer), "MM-PICO %u", (unsigned) MODEL_NUMBER);
    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, (u16_t) len, PBUF_RAM);
    if (reply == NULL) return;
    memcpy(reply->payload, answer, (size_t) len);
    udp_sendto(pcb, reply, addr, port);
    pbuf_free(reply);
}

static void start_servers(void)
{
    struct tcp_pcb *listener = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (listener && tcp_bind(listener, IP_ANY_TYPE, WIFI_PORT) == ERR_OK) {
        listener = tcp_listen_with_backlog(listener, 1);
        if (listener) tcp_accept(listener, on_accept);
    }
    struct udp_pcb *finder = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (finder && udp_bind(finder, IP_ANY_TYPE, WIFI_PORT) == ERR_OK) {
        udp_recv(finder, on_discover, NULL);
    }
    servers_up = true;
}

// ------------------------------------------------------------------- public API

void wifi_init(void)
{
    chip_ok = (cyw43_arch_init() == 0);
    if (!chip_ok) return;               // e.g. this image flashed onto a plain Pico

    have_settings = memcmp(settings->magic, "MMWF", 4) == 0 &&
                    settings->ssid[0] != 0 &&
                    settings->ssid[sizeof(settings->ssid) - 1] == 0 &&
                    settings->password[sizeof(settings->password) - 1] == 0;
    if (!have_settings) return;         // USB still works; flash.py sets Wi-Fi up

    cyw43_arch_enable_sta_mode();
    // Power saving lets the chip sleep between beacons, which adds up to
    // ~100 ms to a reply. The host expects answers within a few milliseconds.
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    start_join();
    next_check = make_timeout_time_ms(1000);
}

void wifi_poll(void)
{
    if (!chip_ok) return;
    cyw43_arch_poll();
    if (!have_settings || !time_reached(next_check)) return;

    next_check = make_timeout_time_ms(1000);
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
        seconds_down = 0;
        if (!servers_up) start_servers();
    } else if (++seconds_down >= REJOIN_SECONDS) {
        seconds_down = 0;               // out of range, wrong password, router restarted...
        start_join();
    }
}

void wifi_led_put(bool on)
{
    if (chip_ok) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

void wifi_status(uint8_t *out)
{
    memset(out, 0, 8);
    if (!chip_ok) return;
    out[0] = (uint8_t) cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    const ip4_addr_t *ip = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
    memcpy(&out[1], ip, 4);
    out[5] = have_settings;
}
