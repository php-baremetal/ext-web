---
eyebrow: 'ext-web · PHP API'
lede:    'serve_ws() adds a WebSocket endpoint on the HTTP server. Each inbound frame is a Message event; reply() answers one client, ws_broadcast() reaches all of them. Sends never block.'
see_also:
  - { href: './http.md', meta: 'PHP API' }
  - { href: '../recipes/websocket-broadcast.md', meta: 'Recipe' }
  - { href: '../reference/errors-and-limits.md', meta: 'Reference' }
prev: { label: 'HTTP', href: './http.md' }
next: { label: 'Request lifecycle and timeouts', href: '../reference/request-lifecycle.md' }
---

# WebSocket

## serve_ws()

<!-- @method name="serve_ws(string $path = '/ws'): bool" returns="bool" -->

<!-- @params -->
<!-- @param name="$path" type="string" default="'/ws'" -->
The exact path of the endpoint, e.g. `/ws` or `/live`.
<!-- @endparam -->
<!-- @endparams -->

Adds a WebSocket endpoint at `$path` and returns `true`. The endpoint shares the HTTP server: if
`serve_http()` has not run yet, `serve_ws()` starts the server on port 80, and a later `serve_http()` with
another port then throws. For another port, call `serve_http($port)` first. The order of the two calls does
not matter otherwise: the endpoint always matches before the catch-all HTTP route.

Throws `\Exception` when there is no network interface, when the server cannot start, and with
`serve_ws(): cannot register the WS handler` when the path is already taken or all route slots are used (at
most five endpoints next to `serve_http()`).

The handshake is accepted by the server without reaching PHP; the connection is then given an id.

## Message

`Baremetal\Http\Message` extends `Baremetal\Event`. Register for it with
`Events::listen(Message::class, ...)`; every frame of every endpoint is delivered to the same listeners.

| Property | Type | Content |
| --- | --- | --- |
| `$text` | `string` | The frame payload. |
| `$client` | `int` | The connection's id: positive, never reused while the board runs. `0` when the connection could not be tracked (more than 16 open). |
| `$binary` | `bool` | `true` for a binary frame (the payload is still in `$text`). |

Listener return values are ignored, apart from `false`, which stops the later `Message` listeners as for any
event. Empty frames are not delivered.

<!-- @method name="Message::reply(string $data): bool" returns="bool" visibility="public" -->

Queues a text frame to the client this message came from. Returns `false` when the client is gone, is not
tracked (`$client` 0), or the send could not be queued. The frame is dropped if the client cannot take data
when the server gets to it.

## ws_broadcast()

<!-- @method name="ws_broadcast(string $data): int" returns="int" -->

Queues one text frame for every connected client and returns how many clients were connected when it was
queued (0 when there are none or the queue is full). A client that cannot take data at send time is skipped
for this frame, so a slow client never stalls the others. Call it from any listener: a timer, a button, a
request.

<!-- @code-block language="php" label="echo and broadcast" -->
```php
<?php
use Baremetal\Event;
use Baremetal\Events;
use Baremetal\Http\Message;

final class Beat extends Event {}

Events::listen(Message::class, function (Message $m): void {
    $m->reply('echo: ' . $m->text);
});

Events::listen(Beat::class, function (): void {
    ws_broadcast(json_encode(['uptime' => hrtime(true) / 1e9]));
});

every(1000, Beat::class);
serve_ws('/ws');
serve_http(80);
```
<!-- @endcode-block -->

## Flow control

- **Inbound.** When the event queue is full, the server task waits up to 2 s for room before reading on, so
  TCP slows the sender; only then is the frame dropped.
- **Outbound.** `reply()` and `ws_broadcast()` hand the frame to the server task and return at once; replies
  and broadcasts are always text frames.
- **Closing.** Clients are not notified when the program stops; their connections end when the board
  restarts.
