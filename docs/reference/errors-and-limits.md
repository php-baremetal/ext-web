---
eyebrow: 'ext-web · Reference'
lede:    'Every exception and warning ext-web raises, and the fixed sizes of the server behind them.'
see_also:
  - { href: '../usage/http.md', meta: 'PHP API' }
  - { href: './request-lifecycle.md', meta: 'Reference' }
prev: { label: 'Request lifecycle and timeouts', href: './request-lifecycle.md' }
next: { label: 'A JSON API', href: '../recipes/json-api.md' }
---

# Errors and limits

ext-web throws plain `\Exception` when the server cannot start and `\ValueError` for invalid arguments; it
defines no exception classes of its own.

## Exceptions

| Message | Thrown by | Cause |
| --- | --- | --- |
| `serve_http(): the server already runs on port ..` | `serve_http()` | The server was started on another port, by an earlier `serve_http()` or by `serve_ws()` (port 80). |
| `serve_http(): no network interface (turn the network option on, or connect with ext-wifi first)` | `serve_http()` | No network interface exists yet. |
| `serve_ws(): no network interface (turn the network option on, or connect with ext-wifi first)` | `serve_ws()` | Same. |
| `serve_http(): the HTTP server failed to start`, `serve_ws(): the HTTP server failed to start` | both | The platform server did not start (port in use, no memory). |
| `serve_http(): cannot register the HTTP routes` | `serve_http()` | The routes could not be added. |
| `serve_ws(): cannot register the WS handler` | `serve_ws()` | The path is already registered, or every route slot is used. |

`\ValueError` is thrown by `serve_http()` for a port outside 1 to 65535, and by `Response::header()` for an
invalid name (`must be a valid header name`), a header the server sets (`is set by the server`:
`Content-Length`, `Transfer-Encoding`), or a value with CR, LF or NUL.

## Warnings

Raised while the response is copied out, after the listener returned:

- `Response header "..." dropped (invalid or set by the server)`: a bad entry written directly into
  `$headers`.
- `Response header "..." dropped (more than 16 headers)`
- `Response Content-Type dropped (contains CR, LF or NUL)`

## Limits

| Limit | Value | Source |
| --- | --- | --- |
| Requests handled at once | 1 | one server task |
| `$path`, `$query` | 255 bytes each, truncated | ext-web |
| Captured request headers | the 8 listed in [HTTP](../usage/http.md) | esp32-web-hal |
| Request header block | 2048 bytes, else 431 | esp32-web-hal |
| Request body | `post_max_size`, else 413; held in RAM | esp32-web-hal |
| Body receive time | 10 s + 1 s per 16 KB, else 408 | esp32-web-hal |
| Response headers | 16 (plus `Content-Type`) | ext-web |
| Response `Content-Type` | 127 bytes | ext-web |
| WebSocket endpoints | 5 next to `serve_http()` (12 route slots, 7 used by HTTP methods) | esp32-web-hal |
| Tracked WebSocket connections | 16 | esp32-web-hal |
| Inbound frame wait for queue room | 2 s, then dropped | esp32-web-hal |
| Stuck outbound send | dropped after 2 s | esp32-web-hal |
| Open sockets | the `esp_http_server` default; the least recently used is closed when full | esp32-web-hal |
| Event queue | 32 messages, shared with all events | ext-events |

The server never stops once started: there is no `stop` function, and it lives until the board restarts.
