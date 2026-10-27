/*
   +----------------------------------------------------------------------+
   | php-baremetal: ext-web                                               |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

/*
 * Neutral web HAL: the surface the web source speaks, free of any HTTP-server backend. The facade
 * (php_web.c) talks only to this; the arch backing (on ESP32: bm_web_hal_esp32.c over esp_http_server)
 * runs the server task, parks each request in a web_slot_t and hands it to the reactor, then sends the
 * reply the reactor fills in. Another family provides its own bm_web_hal_<arch>.c.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define WEB_MAX_HDRS      10
#define WEB_HDR_BUF       2048   /* the request header limit of the server: all captured values fit */
#define WEB_MAX_RESP_HDRS 16

/* The request/response rendezvous. The server task (HAL) allocates one slot per request, fills the `in`
 * fields and posts it to the reactor via bm_events_post_http; the reactor (facade, web_deliver) fills the
 * `out` fields and calls bm_web_hal_response_ready(), after which it must not touch the slot again. If the
 * reactor does not answer in time the server task answers 503 and abandons the slot: the reactor's
 * bm_web_hal_response_ready() then frees it. Whoever finishes last frees the slot and its buffers. */
typedef struct {
    /* in -- filled by the server task */
    const char *method;
    char        path[256];
    char        query[256];
    char        ip[48];
    struct { const char *name; const char *value; } hdr[WEB_MAX_HDRS];   /* values point into hdr_buf */
    int         n_hdr;
    char        hdr_buf[WEB_HDR_BUF];
    char       *body;            /* or NULL */
    size_t      body_len;
    /* out -- filled by the reactor; every buffer is malloc'd and freed with the slot */
    int         status;
    char       *resp_body;
    size_t      resp_len;
    char        resp_ctype[128];
    char       *resp_hdrs;       /* n_resp_hdr pairs, each "name\0value\0" */
    size_t      resp_hdrs_len;
    int         n_resp_hdr;
} web_slot_t;

/* One inbound WebSocket frame: malloc'd by the server task, freed by the reactor after delivery. */
typedef struct {
    int32_t client;       /* the connection's id (never reused), for a reply */
    bool    binary;
    size_t  len;
    char    data[];       /* NUL-terminated payload */
} ws_msg_t;

/* Whether a network interface exists to serve on (it may still be waiting for its address). */
bool bm_web_hal_network_ready(void);

/* Start the one shared HTTP server on `port` if it is not already up (idempotent). Also prepares the
 * request/response rendezvous. Returns false if the server could not start. */
bool bm_web_hal_start(uint16_t port);

/* The longest the server task waits for the reactor before answering 503 (seconds, 0 = no deadline: it
 * waits until the reactor answers), and the largest request body it accepts (bytes, 0 = no limit; above it
 * the client gets 413). */
void bm_web_hal_set_limits(uint32_t wait_s, size_t max_body);

/* The platform's time limit for one HTTP request (seconds, 0 = no limit): handlers run with
 * max_execution_time = this value (esp32: the sdk option request-timeout). */
uint32_t bm_web_hal_request_timeout(void);

/* The port of the running server, 0 while it is not running. */
uint16_t bm_web_hal_port(void);

/* Register the catch-all HTTP route (every method). Each request is parked in a web_slot_t and posted
 * to the reactor; the reply the reactor fills in is sent back on the same socket (HEAD: headers only). */
bool bm_web_hal_register_http(void);

/* Register a WebSocket route at `path`; inbound frames are posted to the reactor as ws_msg_t. Re-orders
 * the HTTP catch-all after it so the exact WS route matches first. */
bool bm_web_hal_register_ws(const char *path);

/* Queue a text frame to one client (Message::reply); false if the client is gone or the queue is full.
 * The frame is dropped if the client closed meanwhile or cannot take data. */
bool bm_web_hal_ws_send(int32_t client, const char *data, size_t len);

/* Queue a text frame to every connected WS client; returns how many clients it was queued for. A client
 * whose send buffer is full is skipped, never blocking the server. */
int bm_web_hal_ws_broadcast(const char *data, size_t len);

/* Whether the server task already answered 503 for this slot (the reactor then skips the handler). */
bool bm_web_hal_slot_abandoned(const web_slot_t *slot);

/* Called by the reactor once it has filled a slot's `out` fields: releases the waiting server task, or
 * frees the slot if the server task abandoned it. The slot must not be used afterwards. */
void bm_web_hal_response_ready(web_slot_t *slot);
