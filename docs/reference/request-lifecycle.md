---
eyebrow: 'ext-web · Reference'
lede:    'From socket to listener and back: what the server task does, the time limit set by request-timeout, and why every request gets 503 once the program has stopped.'
see_also:
  - { href: '../usage/http.md', meta: 'PHP API' }
  - { href: './errors-and-limits.md', meta: 'Reference' }
  - { href: 'package:php-baremetal/esp32-web-hal', meta: 'HAL', label: 'esp32-web-hal' }
prev: { label: 'WebSocket', href: '../usage/websocket.md' }
next: { label: 'Errors and limits', href: './errors-and-limits.md' }
---

# Request lifecycle and timeouts

## One request

<!-- @steps -->
- **The server task reads it**

  The HTTP server runs on its own task (on ESP32 the `httpd` task, core 0, never calling PHP). It reads the
  request line, the captured headers and the whole body into memory, then posts the request to the event
  queue and waits.

- **The reactor delivers it**

  When the request's turn comes, the PHP task builds a `Request`, fills the superglobals, sets
  `max_execution_time` to `request-timeout` and runs the `Request` listeners.

- **The response goes back**

  The returned `Response` is copied out (status, content type, headers, body), the script's own
  `max_execution_time` is restored, and the server task sends the reply on the same connection.
<!-- @endsteps -->

The server task handles one request at a time: while it waits for PHP it serves nothing else. A request that
arrives while the event queue (32 messages, shared with every other event) is full is answered 503 at once.

## Status codes the server sends itself

| Situation | Status |
| --- | --- |
| Request header block over 2048 bytes | 431 |
| Body larger than `post_max_size` | 413 |
| Body not received within 10 s plus 1 s per 16 KB | 408 (400 if the connection failed) |
| Event queue full, or no memory for the request | 503 |
| No answer from PHP within `request-timeout` + 5 s | 503 |
| The program has stopped | 503 |

The answers PHP produces (200, 404, 500, 503 with no listener) are listed in [HTTP](../usage/http.md).

## request-timeout

`request-timeout` is an SDK option (seconds, 0 to 3600, default 30), set in `extra.baremetal.options`:

<!-- @code-block language="json" label="composer.json, extra.baremetal" -->
```json
{ "options": { "request-timeout": 10 } }
```
<!-- @endcode-block -->

It sets two limits:

| Limit | Value | Where |
| --- | --- | --- |
| `max_execution_time` of a `Request` listener | `request-timeout` | PHP task. `ini_get()` reports it inside the handler; `set_time_limit()` in a handler lasts for that request only. |
| How long the server task waits for the answer | `request-timeout` + 5 s | Server task. Then the client gets 503. |

With `0` there is no limit: a handler that never returns keeps the server task waiting for good.

The time is wall-clock time, counted from when the handler starts; `sleep()` counts but is not interrupted,
and a long call inside C code is not interrupted either.

## When a handler goes over the limit

Going over `max_execution_time` is a fatal error, and in the `event-driven` runtime every fatal error stops
the program; the board restarts after `restart-delay` seconds.

- The request whose handler hit the limit gets **500**: the reply is prepared before the program stops.
  So does a request whose handler calls `exit()`.
- Requests already waiting, and every request that arrives until the restart, get **503** at once: when the
  program stops, the HAL marks the server as stopping and stops waiting for a reactor that no longer runs.
- WebSocket clients are not notified.

An uncaught exception is not fatal by itself: the request gets 500, then the SDK option `on-error` decides
(`stop`, the default, stops the program the same way; `continue` prints a warning and keeps serving).

## A request that arrives late

If the server task gave up on a request (503 after `request-timeout` + 5 s) while PHP was still busy with
other events, PHP skips that request when it finally reaches it: no listener runs for it.
