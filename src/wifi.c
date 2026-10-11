// Wi-Fi link for the Pico W.
//
// Normal use: the box joins a saved Wi-Fi network and listens on WIFI_PORT. A
// client that connects over TCP sends exactly the same command bytes it would
// send over USB and gets the same reply bytes back: both routes call
// handle_command() in main_device.c. To be found without typing an address,
// the box answers the UDP broadcast "MM-DISCOVER" on the same port with
// "MM-PICO <model number>".
//
// First use: a box with no saved network hosts its own open network called
// "MultiMetrics-Setup-XXXX" at 192.168.4.1. Everything above works there too,
// so the app can connect, send the name and password of the real network
// (op codes WIFI_SET_NAME, WIFI_SET_PASSWORD, WIFI_SAVE), and the box restarts
// onto it. The onboard LED blinks while the setup network is up.
//
// The box also falls back to the setup network when it cannot get onto the
// saved one (moved, password changed, router off), and keeps retrying the
// saved network in the background while nobody is using the setup one.
//
// The saved name and password live in the last 4 KB of flash, outside the
// firmware image, so firmware updates leave them alone. flash.py can write
// the same area when it flashes a board.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "dhcpserver/dhcpserver.h"
#include "mm.h"
#include "wifi.h"

// Layout shared with wifi_page() in flash.py.
typedef struct {
    char magic[4];      // "MMWF"
    char ssid[33];      // zero terminated
    char password[65];  // zero terminated, empty for an open network
} wifi_settings_t;

#define WIFI_SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

#define JOIN_SECONDS        30  // give up on the saved network after this long...
#define REJOIN_SECONDS      15  // ...asking the chip to try again this often
#define SETUP_RETRY_SECONDS 120 // while hosting setup, retry the saved network this often
#define RETRY_JOIN_SECONDS  20  // ...for this long each time

static const wifi_settings_t *settings =
    (const wifi_settings_t *)(XIP_BASE + WIFI_SETTINGS_OFFSET);

static bool chip_ok = false;
static bool have_settings = false;
static int mode = WIFI_MODE_OFF;
static int seconds_in_mode = 0;
static int join_limit = JOIN_SECONDS;
static absolute_time_t next_check;

static struct tcp_pcb *client = NULL;
static dhcp_server_t dhcp_server;

static char pending_ssid[33];
static char pending_password[65];
static bool restart_pending = false;
static absolute_time_t restart_at;

static void set_mode(int new_mode)
{
    mode = new_mode;
    seconds_in_mode = 0;
}

// ------------------------------------------------------------ saved network

static void start_join(void)
{
    uint32_t auth = settings->password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    cyw43_arch_wifi_connect_async(settings->ssid, settings->password, auth);
}

static void begin_joining(int limit_seconds)
{
    cyw43_arch_enable_sta_mode();
    // Power saving lets the chip sleep between beacons, which adds up to
    // ~100 ms to a reply. The host expects answers within a few milliseconds.
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    start_join();
    join_limit = limit_seconds;
    set_mode(WIFI_MODE_JOINING);
}

static bool link_up(void)
{
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

// ------------------------------------------------------------ setup network

static void start_setup_network(void)
{
    uint8_t mac[6] = {0};
    char name[33];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    // The last two bytes of the address keep two boxes in one room apart.
    snprintf(name, sizeof(name), "MultiMetrics-Setup-%02X%02X", mac[4], mac[5]);

    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_ap_mode(name, NULL, CYW43_AUTH_OPEN);

    // Hand out addresses so a computer that joins can talk to 192.168.4.1.
    ip_addr_t ip, mask;
    ip4_addr_set_u32(ip_2_ip4(&ip), PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS));
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &ip, &mask);
    set_mode(WIFI_MODE_SETUP);
}

static void stop_setup_network(void)
{
    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_disable_ap_mode();
    wifi_led_put(true);
}

static bool setup_network_in_use(void)
{
    static uint8_t macs[32 * 6];
    int max_stations = 0;
    int stations = 0;
    if (client) return true;
    cyw43_wifi_ap_get_max_stas(&cyw43_state, &max_stations);
    if (max_stations <= 0 || max_stations > 32) return false;
    cyw43_wifi_ap_get_stas(&cyw43_state, &stations, macs);
    return stations > 0;
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

    char answer[32];
    int len = snprintf(answer, sizeof(answer), "MM-PICO %u%s", (unsigned) MODEL_NUMBER,
                       mode == WIFI_MODE_SETUP ? " SETUP" : "");
    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, (u16_t) len, PBUF_RAM);
    if (reply == NULL) return;
    memcpy(reply->payload, answer, (size_t) len);
    udp_sendto(pcb, reply, addr, port);
    pbuf_free(reply);
}

// Both listeners accept traffic from whichever network is up at the time.
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
}

// ------------------------------------------------------- changing the network

static void copy_text(char *dest, size_t dest_size, const uint8_t *text, uint32_t max)
{
    size_t len = 0;
    while (len < max && len < dest_size - 1 && text[len] != 0) len++;
    memset(dest, 0, dest_size);
    memcpy(dest, text, len);
}

void wifi_set_name(const uint8_t *text, uint32_t max)
{
    copy_text(pending_ssid, sizeof(pending_ssid), text, max);
}

void wifi_set_password(const uint8_t *text, uint32_t max)
{
    copy_text(pending_password, sizeof(pending_password), text, max);
}

bool wifi_save(void)
{
    size_t password_len = strlen(pending_password);
    if (password_len > 0 && (password_len < 8 || password_len > 63)) return false;

    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    wifi_settings_t *next = (wifi_settings_t *) page;
    memset(next, 0, sizeof(*next));
    if (pending_ssid[0]) {              // an empty name leaves the area blank: forget
        memcpy(next->magic, "MMWF", 4);
        memcpy(next->ssid, pending_ssid, sizeof(next->ssid));
        memcpy(next->password, pending_password, sizeof(next->password));
    }

    // Flash cannot be read while it is being written, and code runs from
    // flash, so nothing else may run for these few tens of milliseconds.
    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(WIFI_SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(WIFI_SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);

    if (memcmp(settings, page, sizeof(wifi_settings_t)) != 0) return false;
    // Restart shortly, after the reply has gone out, and come up on the new network.
    restart_at = make_timeout_time_ms(500);
    restart_pending = true;
    return true;
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
    start_servers();
    if (have_settings) {
        begin_joining(JOIN_SECONDS);
    } else {
        start_setup_network();
    }
    next_check = make_timeout_time_ms(1000);
}

void wifi_poll(void)
{
    if (!chip_ok) return;
    cyw43_arch_poll();
    if (restart_pending && time_reached(restart_at)) {
        watchdog_reboot(0, 0, 10);
        while (true) tight_loop_contents();
    }
    if (!time_reached(next_check)) return;      // the rest runs once a second
    next_check = make_timeout_time_ms(1000);
    seconds_in_mode++;

    switch (mode) {
        case WIFI_MODE_JOINING:
            if (link_up()) {
                set_mode(WIFI_MODE_ONLINE);
            } else if (seconds_in_mode >= join_limit) {
                start_setup_network();          // let someone fix the settings
            } else if (seconds_in_mode % REJOIN_SECONDS == 0) {
                start_join();
            }
            break;
        case WIFI_MODE_ONLINE:
            if (!link_up()) {                   // router restarted, out of range...
                start_join();
                join_limit = JOIN_SECONDS;
                set_mode(WIFI_MODE_JOINING);
            }
            break;
        case WIFI_MODE_SETUP:
            wifi_led_put(seconds_in_mode & 1);  // blinking = waiting to be set up
            if (have_settings && seconds_in_mode >= SETUP_RETRY_SECONDS) {
                if (setup_network_in_use()) {
                    seconds_in_mode = 0;        // someone is here; look again later
                } else {
                    stop_setup_network();
                    begin_joining(RETRY_JOIN_SECONDS);
                }
            }
            break;
        default:
            break;
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
    int itf = (mode == WIFI_MODE_SETUP) ? CYW43_ITF_AP : CYW43_ITF_STA;
    out[0] = (uint8_t) cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    memcpy(&out[1], netif_ip4_addr(&cyw43_state.netif[itf]), 4);
    out[5] = have_settings;
    out[6] = (uint8_t) mode;
}
