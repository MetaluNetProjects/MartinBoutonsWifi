// UDP server handling Fraise and table messages

#pragma once
#include <functional>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "fraise.h"

class UDPServer {
private:
    using TableCallback = std::function<void(int)>;
    struct udp_pcb* server = NULL;
    struct udp_pcb* client = NULL;
    int server_bound = -1;
    int port;
    char fraise_buffer[128];
    int fraise_buflen = 0;
    enum class MessageType {fraise_bytes = 0, fraise_text, system, table_start, table_cont, table_end};
    enum class SystemId {ret_port = 0, ping};
    ip_addr_t last_sender_addr;// = nullptr;
    u16_t return_port = 0;
    u16_t alt_return_port = 0;
    struct TableDef {
        bool started = false;
        uint8_t *data = nullptr;
        int len = 0;
        TableCallback start_callback = nullptr;
        TableCallback end_callback = nullptr;
    };
    static const int NUM_TABLES = 32;
    TableDef tables[NUM_TABLES];

    void receive(struct udp_pcb *upcb, struct pbuf *p, 
            const ip_addr_t* addr,  // Address of sender
            u16_t port)             // Sender port 
    {
        // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
        // can use this method to cause an assertion in debug mode, if this method is called when
        // cyw43_arch_lwip_begin IS needed
        cyw43_arch_lwip_check();

        int n = 0;
        //last_sender_addr = addr;
        ip_addr_set(&last_sender_addr, addr);
        return_port = port;
        //if(!alt_return_port) 
        alt_return_port = port;
        MessageType command = (MessageType)pbuf_get_at(p, n++);
        switch(command) {
        case MessageType::fraise_bytes:
        case MessageType::fraise_text:
            {
                fraise_buflen = pbuf_copy_partial(p, fraise_buffer,
                    MIN(p->tot_len - 1, sizeof(fraise_buffer) - 1), 1);
                fraise_buffer[fraise_buflen] = 0;
                if(command == MessageType::fraise_bytes) {
                    fraise_init_get_buffer(fraise_buffer, fraise_buflen);
                    fraise_receivebytes(fraise_buffer, fraise_buflen);
                } else fraise_receivechars(fraise_buffer, fraise_buflen);
                /*struct pbuf *retp = pbuf_alloc(PBUF_TRANSPORT, 1, PBUF_RAM);
                uint8_t *retbuf = (uint8_t *)retp->payload;
                retbuf[0] = command;
                udp_sendto(server, retp, addr, port);
                pbuf_free(retp);*/
            }
            break;
        case MessageType::system:
            if (n < p->tot_len) {
                SystemId id = (SystemId)pbuf_get_at(p, n++);
                switch(id) {
                case SystemId::ret_port: {
                    if(n < p->tot_len - 1)
                        alt_return_port = (pbuf_get_at(p, n++) << 8) + pbuf_get_at(p, n++);
                        struct pbuf *retp = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);
                        uint8_t *retbuf = (uint8_t *)retp->payload;
                        retbuf[0] = (uint8_t)MessageType::system;
                        retbuf[1] = (uint8_t)SystemId::ret_port;
                        retbuf[2] = alt_return_port >> 8;
                        retbuf[3] = alt_return_port & 255;
                        udp_sendto(server, retp, addr, port);
                        pbuf_free(retp);
                    }
                    break;
                case SystemId::ping: {
                        struct pbuf *retp = pbuf_alloc(PBUF_TRANSPORT, 2, PBUF_RAM);
                        uint8_t *retbuf = (uint8_t *)retp->payload;
                        retbuf[0] = (uint8_t)MessageType::system;
                        retbuf[1] = (uint8_t)SystemId::ping;
                        udp_sendto(server, retp, addr, port);
                        pbuf_free(retp);
                    }
                    break;
                }
            }
            break;
        case MessageType::table_start:
        case MessageType::table_cont:
        case MessageType::table_end:
            if (p->tot_len > 4) {
                int table_num = pbuf_get_at(p, n++);
                int offset = (pbuf_get_at(p, n++) << 8) + pbuf_get_at(p, n++);
                if(table_num >= NUM_TABLES || tables[table_num].data == nullptr) break;
                if(!tables[table_num].started && command != MessageType::table_start) break;
                tables[table_num].started = (command != MessageType::table_end);
                if(command == MessageType::table_start && tables[table_num].start_callback)
                    tables[table_num].start_callback(table_num);
                int max_len = MAX(tables[table_num].len - offset, 0);
                max_len = MIN(p->tot_len - n, max_len);
                int sent = pbuf_copy_partial(p, tables[table_num].data + offset, max_len, n);
                if(command == MessageType::table_end && tables[table_num].end_callback)
                    tables[table_num].end_callback(table_num);

                struct pbuf *retp = pbuf_alloc(PBUF_TRANSPORT, 3, PBUF_RAM);
                uint8_t *retbuf = (uint8_t *)retp->payload;
                retbuf[0] = (uint8_t)command;
                retbuf[1] = sent >> 8;
                retbuf[2] = sent & 255;
                udp_sendto(server, retp, addr, port);
                pbuf_free(retp);
            }
            break;
        }
        pbuf_free(p);
        return_port = alt_return_port;
    }

    static void server_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, 
            const ip_addr_t* addr,  // Address of sender
            u16_t port)             // Sender port 
    {
        UDPServer *server = (UDPServer*)arg;
        server->receive(upcb, p, addr, port);
    }

    int init_server(int p) {
        int err;
        port = p;
        // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
        // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
        // these calls are a no-op and can be omitted, but it is a good practice to use them in
        // case you switch the cyw43_arch type later.
        cyw43_arch_lwip_begin();
        if(! server) {
            printf("server is NULL! Recreating it\n");
            server = udp_new();
            udp_recv(server, server_recv, NULL);
            if(! server) {
                printf("server is still NULL! Aborting\n");
                cyw43_arch_lwip_end();
                return 0;
            }
        }
        err = udp_bind(server, IP4_ADDR_ANY, port);
        if(err == ERR_OK) {
            printf("server bound\n");
        } else {
            printf("server bind error %d %s\n", err, err == ERR_USE ? "already bound (ERR_USE)":"");
        }
        cyw43_arch_lwip_end();
        return err == ERR_OK;
    }

    void send_any(const char* data, uint8_t len, MessageType type) {
        if((!server) || (!client) /*|| (!last_sender_addr)*/ || (!return_port)) return;
        cyw43_arch_lwip_begin();
        struct pbuf *retp = pbuf_alloc(PBUF_TRANSPORT, len + 1, PBUF_RAM);
        ((char*)retp->payload)[0] = (char)type;
        memcpy((char*)retp->payload + 1, data, len);
        udp_sendto(client, retp, &last_sender_addr, return_port);
        pbuf_free(retp);
        cyw43_arch_lwip_end();
    }

public:

    void setup(int port) {
        server = udp_new();
        client = udp_new();
        udp_recv(server, server_recv, this);
        server_bound = init_server(port);
    }

    void disconnect() {
        udp_disconnect(server);
    }

    void send_bytes(const char* data, uint8_t len) {
        send_any(data, len, MessageType::fraise_bytes);
    }

    void send_text(const char* data, uint8_t len) {
        send_any(data, len, MessageType::fraise_text);
    }

    void set_table(int table_num, uint8_t *data, int len) {
        if(table_num >= NUM_TABLES) return;
        tables[table_num].data = data;
        tables[table_num].len = len;
    }
    void set_table_callbacks(int table_num, TableCallback start_cb, TableCallback end_cb) {
        if(table_num >= NUM_TABLES) return;
        tables[table_num].start_callback = start_cb;
        tables[table_num].end_callback = end_cb;
    }
};
