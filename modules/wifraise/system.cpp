/* Pico2 OTA system, launching:
 - wifi
 - ota_server
 - udp server
 - main
*/

#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "dhcpserver.h"
#include "dnsserver.h"

#include "ota_server.hpp"
#include "udp_server.hpp"
#include "fraise.h"

UDPServer udp;

const int STA_MAX_TRIES = 2;
int sta_num_tries = 0;
int sta_error_codes[STA_MAX_TRIES]{0};

extern void loop();
extern void setup();
extern void setup1();

volatile int run_core1_setup = 0;

static void core1_task() {
    multicore_lockout_victim_init(); // allow core0 to lockout core1, for flash programming purpose.
    //while(!run_core1_setup) sleep_ms(10);
    setup1();
}

void set_led(bool l) {
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, l ? 1 : 0);
	//gpio_put(LED_PIN, l ? 1 : 0);
}

static bool connect_sta() {
    int err;
    if (sta_num_tries >= STA_MAX_TRIES) return false;
    err = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
    sta_error_codes[sta_num_tries++] = err;
    return err == PICO_OK;
}

static void connect_ap() {
    cyw43_arch_disable_sta_mode();
    const char *ap_name = "picow_test";
    const char *password = "password";
    cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

    #if LWIP_IPV6
    #define IP(x) ((x).u_addr.ip4)
    #else
    #define IP(x) (x)
    #endif
    ip4_addr_t mask, gw;
    IP(gw).addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    IP(mask).addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
    #undef IP

    // Start the dhcp server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &gw, &mask);

    // Start the dns server
    dns_server_t dns_server;
    dns_server_init(&dns_server, &gw);
}

int main() {
    stdio_init_all();

    // Init core1 (with multicore_lockout_victim_init()) before ota_server
    // else watchdog will trigger after 16.7s!
    multicore_launch_core1(core1_task);

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    sleep_ms(100);
    if (!connect_sta()) {
        sleep_ms(500);
        if (!connect_sta()) connect_ap();
    }
    /*if(1) {
        cyw43_arch_enable_sta_mode();
        while (sta_num_tries < STA_MAX_TRIES) {
            if(connect_sta()) break;
            if(sta_num_tries >= STA_MAX_TRIES) {
                connect_ap();
                break;
            } else sleep_ms(50);
        }
    } else connect_ap();*/

    if (ota_server_init()) return 1;

    udp.setup(4343);
    run_core1_setup = 1;
    sleep_ms(50);
    setup();

    while (true) {
    #if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
    #endif
        ota_server_service();
        loop();
    }
    return 0;
}

void fraise_putchar(char c) {
    static char line[64];
    static int count = 0;
    if(c == '\n') {
        line[count] = 0;
        udp.send_text(line, count);
        count = 0;
        return;
    }
    if(count < 64) line[count++] = c;
}

bool fraise_putbytes(const char* data, uint8_t len) { // returns true on success
    udp.send_bytes(data, len);
    return true;
}

void wifi_print_status() {
    fraise_printf("l wifi sta %s %s\n", WIFI_SSID, WIFI_PASSWORD);
    fraise_printf("l wifi tries %d errs:", sta_num_tries);
    for (int i = 0; i < sta_num_tries; i++) {
        fraise_printf(" %d", sta_error_codes[i]);
    }
    printf("\n");
}
