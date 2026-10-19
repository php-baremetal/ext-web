# ext-web

An HTTP and WebSocket server for the `event-driven` runtime. `serve_http()` starts the server, and every
request reaches PHP as a `Baremetal\Http\Request` event that a listener answers with a
`Baremetal\Http\Response`. `serve_ws()` adds a WebSocket endpoint whose inbound frames arrive as
`Baremetal\Http\Message` events; `Message::reply()` and `ws_broadcast()` send frames back. The script stays
resident, so ordinary PHP variables keep their state between requests.

ext-web is platform-neutral: it requires the virtual `php-baremetal/web-hal` and `ext-events`, and conductor
adds the HAL for your SDK platform (`php-baremetal/esp32-web-hal` on ESP32). It needs a network interface:
the `network` option on a wired board, or `ext-wifi` on a Wi-Fi board.

## Install

In a project created with `phpbm init`, with the `event-driven` runtime:

```bash
composer require php-baremetal/ext-web
```

On a Wi-Fi board also require `php-baremetal/ext-wifi` and connect (or start an access point) before
`serve_http()`.

## Example

```php
<?php
use Baremetal\Events;
use Baremetal\Http\Request;
use Baremetal\Http\Response;

$hits = 0;

Events::listen(Request::class, function (Request $r) use (&$hits): Response {
    return match ($r->path) {
        '/'     => Response::html('<h1>hello from PHP</h1>'),
        '/hits' => Response::json(['hits' => ++$hits]),
        default => Response::notFound(),
    };
});

serve_http(80);
```

## Documentation

- [Overview](docs/overview.md): requirements, install, a first project, notes.
- [HTTP](docs/usage/http.md): `serve_http()`, `Request`, `Response`.
- [WebSocket](docs/usage/websocket.md): `serve_ws()`, `Message`, `ws_broadcast()`.
- [Request lifecycle and timeouts](docs/reference/request-lifecycle.md): status codes, superglobals, `request-timeout`, 503 on stop.
- [Errors and limits](docs/reference/errors-and-limits.md)
- Recipes: [a JSON API](docs/recipes/json-api.md), [a page over Wi-Fi](docs/recipes/serve-a-page.md), [WebSocket broadcast](docs/recipes/websocket-broadcast.md).

## License

MIT.
