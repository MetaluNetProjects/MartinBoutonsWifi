// Palantir main

#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "udp_server.hpp"
#include "fraise.h"
#include "cpuload.h"
#include "free_heap.h"
#include "system.h"
#include "math.h"

bool do_print_led = false;

void blink(int ledPeriod) {
	static absolute_time_t nextLed;
	static bool led = false;

	if(time_reached(nextLed)) {
		set_led(led = !led);
		nextLed = make_timeout_time_ms(ledPeriod);
		if(do_print_led) fraise_printf("led %d\n", led ? 1 : 0);
	}
}

void setup1() {
}

const int NB_BUTTONS = 25;

float buttons[NB_BUTTONS];
bool last_buttons[NB_BUTTONS];

void setup() {
    for(int i = 0; i < NB_BUTTONS; i++) {
        buttons[i] = 0.0;
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
    }
    ip_addr_t ip;
    IP4_ADDR(&ip, 192, 168, 5, WIFI_IPADDR_D);
    netif_set_ipaddr(netif_default, &ip);
}

void buttons_update() {
    static absolute_time_t next_time;
    static const float thres = 5.0;
    if(!time_reached(next_time)) return;
    next_time = make_timeout_time_ms(1);
    for(int i = 0; i < NB_BUTTONS; i++) {
        bool on = (gpio_get(i) == 0);
        if(on && buttons[i] < thres) {
            buttons[i] += 1.0;
            if(buttons[i] >= thres && !last_buttons[i]) {
                last_buttons[i] = true;
                fraise_printf("b %d 1\n", i);
            }
        }
        if((!on) && buttons[i] > 0.0) {
            buttons[i] -= 1.0;
            if(buttons[i] <= 0.0 && last_buttons[i]) {
                last_buttons[i] = false;
                fraise_printf("b %d 0\n", i);
            }
        }
    }
}

void loop() {
    blink(250);
    buttons_update();
}

void fraise_receivebytes(const char* data, uint8_t len) {
	char command = fraise_get_uint8();
	switch(command) {
		case 103: fraise_printf("l free ram: %ld\n", getFreeHeap()); break;
		case 120: wifi_print_status(); break;
	}
}

extern void print_version(); // from version.cpp

void fraise_receivechars(const char *data, uint8_t len){
	if(data[0] == 'E') { // Echo
		fraise_printf("E%s\n", data + 1);
	} else if(data[0] == 'V') { // Version
		print_version();
	} else fraise_printf("unknown %d\n", data[0]);
}

