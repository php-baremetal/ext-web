---
eyebrow: 'Package · Extension'
lede:    'HTTP requests and WebSocket frames delivered as events in the event-driven runtime: a listener returns a Response, state stays in PHP variables. Neutral extension; conductor adds the web HAL for your platform.'
see_also:
  - { href: './usage/http.md', meta: 'PHP API' }
  - { href: './reference/request-lifecycle.md', meta: 'Reference' }
  - { href: 'package:php-baremetal/esp32-web-hal', meta: 'HAL', label: 'esp32-web-hal' }
  - { href: 'package:php-baremetal/ext-events', meta: 'Extension', label: 'ext-events' }
  - { href: 'package:php-baremetal/ext-wifi', meta: 'Extension', label: 'ext-wifi' }
prev: { label: 'No previous page', href: '#' }
next: { label: 'HTTP', href: './usage/http.md' }
---

# HTTP and WebSocket as events

ext-web puts an HTTP server in front of an `event-driven` script. `serve_http()` starts it; each request is
read in full by the server task, then delivered to PHP as a `Baremetal\Http\Request` event. The first
listener that returns a `Baremetal\Http\Response` answers it. `serve_ws()` adds a WebSocket endpoint: each
inbound frame is a `Baremetal\Http\Message` event, and `Message::reply()` or `ws_broadcast()` send frames
back.

Requests share the reactor with every other event (timers, buttons, sensor samples), one at a time on the PHP
task. The script is never restarted between requests, so a counter, a cache or an open object keeps its
value. For a fresh PHP state per request, as with PHP-FPM, use the SDK's `web-server` runtime instead, which
needs no extension.

## Requirements

| Requires | Why |
| --- | --- |
| `php-baremetal/web-hal` (virtual) | The HTTP server, its task and the hand-over of requests to the reactor. On ESP32 it is `php-baremetal/esp32-web-hal` (platform `esp32`, over `esp_http_server`). |
| `php-baremetal/ext-events` | The reactor queue, `Events::listen()`, and `Baremetal\Event`, which `Request` and `Message` extend. Its HAL (`esp32-events-hal`) is added the same way. |

You never require the HAL yourself. Conductor reads the platform from `extra.baremetal.sdk` (`esp32@^1.0` gives
`esp32`) and adds `php-baremetal/esp32-web-hal` and `php-baremetal/esp32-events-hal` while Composer resolves.
The PHP module is `web`.

The project also needs:

- the **`event-driven` runtime**: only its reactor delivers requests;
- a **network interface** when `serve_http()` runs: on a wired board (Ethernet), the SDK option
  `"network": "on"`; on a Wi-Fi board, `ext-wifi` and a `wifi_connect()` or `wifi_ap_start()` first.

## Install

<!-- @steps -->
- **Install conductor once per machine**

  `composer global require php-baremetal/conductor` keeps the plugin active during the first solve of a new
  project.

- **Create the project**

  `phpbm init my-web` writes `composer.json` with conductor, the package registry, and the SDK, board,
  runtime and storage you pick. Pick the `event-driven` runtime.

- **Require the extension**

  `composer require php-baremetal/ext-web`, plus `php-baremetal/ext-wifi` on a Wi-Fi board.

- **Build and flash**

  `phpbm flash` builds the firmware and flashes it; `phpbm monitor` opens the serial console.
<!-- @endsteps -->

The resulting `composer.json` for a Waveshare ESP32-S3-ETH, whose W5500 Ethernet the SDK brings up when the
`network` option is on:

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/my-web",
  "type": "project",
  "repositories": [
    { "type": "composer", "url": "https://repository.php-baremetal.com", "canonical": true }
  ],
  "require": {
    "php-baremetal/conductor": "^1.0",
    "php-baremetal/ext-events": "^1.0",
    "php-baremetal/ext-web": "^1.0"
  },
  "config": {
    "allow-plugins": { "php-baremetal/conductor": true },
    "platform": { "php": "8.5.11" }
  },
  "extra": {
    "baremetal": {
      "sdk": "esp32@^1.0",
      "board": "waveshare-esp32-s3-eth",
      "runtime": "event-driven",
      "storage": "embedded",
      "options": { "network": "on", "request-timeout": 10 }
    }
  }
}
```
<!-- @endcode-block -->

`ext-events` would come in through ext-web anyway; the `event-driven` runtime asks for it by name.

## A first script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
use Baremetal\Events;
use Baremetal\Http\Request;
use Baremetal\Http\Response;

$hits = 0;

Events::listen(Request::class, function (Request $r) use (&$hits): Response {
    $hits++;
    return match (true) {
        $r->method === 'GET' && $r->path === '/'      => Response::html('<h1>php-baremetal</h1>'),
        $r->method === 'GET' && $r->path === '/stats' => Response::json(['hits' => $hits]),
        default                                       => Response::notFound(),
    };
});

serve_http(80);
echo "serving on http://", $_SERVER['SERVER_ADDR'] ?? 'the board', "/\n";
```
<!-- @endcode-block -->

Routing is plain PHP (`match`, a router library): the extension has no router.

## PHP API at a glance

| Name | Kind | Meaning |
| --- | --- | --- |
| `serve_http(int $port = 80): bool` | function | Start the server; requests become `Request` events. |
| `serve_ws(string $path = '/ws'): bool` | function | Add a WebSocket endpoint; frames become `Message` events. |
| `ws_broadcast(string $data): int` | function | Queue a text frame to every connected WebSocket client. |
| `Baremetal\Http\Request` | class | An incoming request; extends `Baremetal\Event`. `json()`, `header()`. |
| `Baremetal\Http\Response` | class | The answer: status, body, content type, headers. Static helpers `ok()`, `text()`, `html()`, `json()`, `notFound()`, `noContent()`; `header()`. |
| `Baremetal\Http\Message` | class | An inbound WebSocket frame; extends `Baremetal\Event`. `reply()`. |

Startup failures throw `\Exception`, invalid arguments `\ValueError`. See
[Errors and limits](./reference/errors-and-limits.md).

## Dependencies

- Requires the virtual `php-baremetal/web-hal`, provided on ESP32 by
  [esp32-web-hal](package:php-baremetal/esp32-web-hal).
- Requires [ext-events](package:php-baremetal/ext-events) for the reactor and the event base class.
- Works with [ext-wifi](package:php-baremetal/ext-wifi) on Wi-Fi boards, which provides the network interface.

## Notes

- One request is handled at a time. While PHP handles one, the server task waits and serves nothing else, for
  at most `request-timeout` + 5 s. Keep handlers short.
- A handler runs with `max_execution_time` set to the SDK option `request-timeout` (30 s by default). Going over
  it is a fatal error: the program stops, that request gets 500, and every request after it gets 503 until
  the board restarts. See [Request lifecycle and timeouts](./reference/request-lifecycle.md).
- An uncaught exception in a handler answers 500 and then follows `on-error`: with the default `stop` the
  program stops too. Set `"on-error": "continue"` to keep serving.
- `$_GET`, `$_POST`, `$_REQUEST` and `$_SERVER` are filled for each request; `$_COOKIE` and `$_FILES` are not.
