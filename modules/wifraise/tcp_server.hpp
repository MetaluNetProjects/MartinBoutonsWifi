// UDP server handling Fraise and table messages

#pragma once
#include <functional>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "fraise.h"

class TCPServer {
private:
    using TableCallback = std::function<void(int)>;
    struct tcp_pcb *server_pcb = nullptr;
    struct tcp_pcb *client_pcb = nullptr;
    enum class MessageType : uint8_t {fraise_bytes = 0, fraise_text = 1, system = 2, table = 3, unknown = 255, };

    static inline char mark[] = "MESSAGE_START";
    struct Header {
        MessageType msgtype;
        uint8_t num; // table_num
        uint16_t pad;
        uint32_t len; // table message len
    } header;
    int header_count = 0;

    MessageType message_type{MessageType::unknown};

    struct TableDef {
        uint8_t *data = nullptr;
        int len = 0;
        TableCallback start_callback = nullptr;
        TableCallback end_callback = nullptr;
    };
    static const int NUM_TABLES = 32;
    TableDef tables[NUM_TABLES];
    int table_write_id = 0;
    int table_write_count = 0;
    int table_write_msglen = 0;

    void reset_receive() {
        header_count = 0;
        message_type == MessageType::unknown;
    }

    void receive_pbuf_table(struct pbuf *p, int offset, bool init) {
        if(init) {
            table_write_count = 0;
            table_write_id = header.num;
            if(table_write_id >= NUM_TABLES || tables[table_write_id].data == NULL) {
                reset_receive();
                return;
            }
            if(tables[table_write_id].start_callback)
                tables[table_write_id].start_callback(table_write_id);
            table_write_msglen = header.len;
            //fraise_printf("tcp table write init %d len %d\n", table_write_id, table_write_msglen);
        }

        int bytes_to_read = MAX(tables[table_write_id].len - table_write_count, 0);
        bytes_to_read = MAX(MIN(p->tot_len - offset, bytes_to_read), 0);
        int sent = pbuf_copy_partial(p, tables[table_write_id].data + table_write_count, bytes_to_read, offset);
        table_write_count += sent;
        if(table_write_count >= tables[table_write_id].len || table_write_count >= table_write_msglen) {
            if(tables[table_write_id].end_callback) 
                tables[table_write_id].end_callback(table_write_id);
            fraise_printf("tcpt %d %d\n", table_write_id, table_write_count);
            reset_receive();
        }
    }

    void receive_pbuf_data(struct pbuf *p, int offset) {
        int n = offset;
        bool init = false;
        if(message_type == MessageType::unknown) {
            message_type = header.msgtype;
            init = true;
            //fraise_printf("tcp message type %d\n", message_type);
        }
        switch(message_type) {
        case MessageType::table:
            receive_pbuf_table(p, n, init);
            break;
        default:
            reset_receive();
        }
    }

    void receive_pbuf(struct pbuf *p) {
        int n = 0;
        while(n < p->tot_len && (header_count < sizeof(mark))) {
            char c = pbuf_get_at(p, n++);
            if(c == mark[header_count]) {
                header_count++;
            }
            else header_count = 0;
            //if(header_count == sizeof(mark)) fraise_printf("tcp mark seen\n");
        }
        while(n < p->tot_len && (header_count < sizeof(mark) + sizeof(header))) {
            ((char*)&header)[header_count++ - sizeof(mark)] = pbuf_get_at(p, n++);
            message_type = MessageType::unknown;
            //if(header_count == sizeof(mark) + sizeof(header)) fraise_printf("tcp header OK\n");
        }
        if(n < p->tot_len && header_count == sizeof(mark) + sizeof(header)) {
            receive_pbuf_data(p, n);
        }
    }

    err_t client_close() {
        err_t err;
        if (client_pcb) {
            tcp_poll(client_pcb, NULL, 0);
            tcp_sent(client_pcb, NULL);
            tcp_recv(client_pcb, NULL);
            tcp_err(client_pcb, NULL);
            err = tcp_close(client_pcb);
            if (err != ERR_OK) {
                fraise_printf("tcp client close failed %d calling abort\n", err);
                tcp_abort(client_pcb);
                err = ERR_ABRT;
            }
            client_pcb = NULL;
        }
        return err;
    }

    void server_close() {
        if (server_pcb) {
            err_t err = tcp_close(server_pcb);
            if(err == ERR_OK) {
                server_pcb = NULL;
            } else {
                fraise_printf("tcp server close error %d\n", err);
            }
        }
    }

    err_t server_accept(struct tcp_pcb *cli_pcb, err_t err) {
        if (err != ERR_OK || cli_pcb == NULL) {
            return ERR_VAL;
        }
        fraise_printf("tcp client connected\n");
        client_pcb = cli_pcb;
        tcp_arg(client_pcb, this);
        tcp_sent(client_pcb, server_sent_s);
        tcp_recv(client_pcb, server_recv_s);
        //tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
        tcp_err(client_pcb, server_err_s);

        return ERR_OK; //tcp_server_send_data(arg, state->client_pcb);
    }

    err_t server_recv(struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        if(!p) {
            fraise_printf("tcp client disconnected\n");
            client_close();
            return ERR_OK;
        }
        cyw43_arch_lwip_check();
        if (p->tot_len > 0) {
            fraise_printf("tcp_server_recv %d err %d\n", p->tot_len, err);
            tcp_recved(tpcb, p->tot_len);
            receive_pbuf(p);
        }
        pbuf_free(p);
        return ERR_OK;
    }

    err_t server_sent(struct tcp_pcb *tpcb, u16_t len) {
        fraise_printf("tcp_server_sent %u\n", len);
        return ERR_OK;
    }

    void server_err(err_t err) {
        fraise_printf("tcp client err %d\n", err);
        client_pcb = NULL; // the client has already been freed
    }

    static err_t server_accept_s(void *arg, struct tcp_pcb *client_pcb, err_t err) {
        return ((TCPServer*)arg)->server_accept(client_pcb, err);
    }
    static err_t server_sent_s(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return ((TCPServer*)arg)->server_sent(tpcb, len);
    }
    static err_t server_recv_s(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return ((TCPServer*)arg)->server_recv(tpcb, p, err);
    }
    static void server_err_s(void *arg, err_t err) {
        ((TCPServer*)arg)->server_err(err);
    }

public:

    void setup(int port) {
        err_t err;
        if(server_pcb != NULL) {
            fraise_printf("TCP already connected!\n");
            return;
        }
        struct tcp_pcb *pcb;
        if(!(pcb = tcp_new_ip_type(IPADDR_TYPE_ANY))) {
            fraise_printf("TCP couldn't create!\n");
            return;
        }
        err = tcp_bind(pcb, NULL, port);
        if(err != ERR_OK) {
            fraise_printf("TCP couldn't bind %d\n", err);
            return;
        }
        if(!(server_pcb = tcp_listen_with_backlog(pcb, 1))) {
            fraise_printf("TCP couldn't listen!\n");
            tcp_close(pcb);
            return;
        }
        tcp_arg(server_pcb, this);
        tcp_accept(server_pcb, server_accept_s);
        fraise_printf("TCP waiting for connection\n");
    }

    void disconnect() {
        client_close();
        server_close();
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
