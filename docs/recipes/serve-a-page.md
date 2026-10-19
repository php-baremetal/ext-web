---
eyebrow: 'ext-web · Recipe'
lede:    'The board opens its own Wi-Fi access point and serves a page and a form from PHP; join the network and open the address it prints.'
see_also:
  - { href: '../usage/http.md', meta: 'PHP API' }
  - { href: 'package:php-baremetal/ext-wifi', meta: 'Extension', label: 'ext-wifi' }
prev: { label: 'A JSON API', href: './json-api.md' }
next: { label: 'WebSocket broadcast', href: './websocket-broadcast.md' }
---

# A page over Wi-Fi

A Waveshare ESP32-S3-Zero (N8R8) has no Ethernet: `ext-wifi` starts an access point first, which gives the
server a network interface, then `serve_http()` serves a page with a form.

## The project

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/wifi-page",
  "type": "project",
  "repositories": [
    { "type": "composer", "url": "https://repository.php-baremetal.com", "canonical": true }
  ],
  "require": {
    "php-baremetal/conductor": "^1.0",
    "php-baremetal/ext-events": "^1.0",
    "php-baremetal/ext-web": "^1.0",
    "php-baremetal/ext-wifi": "^1.0"
  },
  "config": {
    "allow-plugins": { "php-baremetal/conductor": true },
    "platform": { "php": "8.5.11" }
  },
  "extra": {
    "baremetal": {
      "sdk": "esp32@^1.0",
      "board": "waveshare-esp32-s3-zero-n8r8",
      "runtime": "event-driven",
      "storage": "embedded"
    }
  }
}
```
<!-- @endcode-block -->

The `network` option stays off: on a Wi-Fi board the script brings the network up with ext-wifi.

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
use Baremetal\Events;
use Baremetal\Http\Request;
use Baremetal\Http\Response;

const AP_SSID = 'php-baremetal';
const AP_PASS = 'baremetal';            // 8 characters or more for WPA2

if (!wifi_ap_start(AP_SSID, AP_PASS)) {
    exit("cannot start the access point\n");
}

$name = 'world';

Events::listen(Request::class, function (Request $r) use (&$name): Response {
    if ($r->path !== '/') {
        return Response::notFound();
    }
    if ($r->method === 'POST' && isset($_POST['name'])) {
        $name = trim((string) $_POST['name']) ?: 'world';
    }
    $safe = htmlspecialchars($name);
    return Response::html(<<<HTML
        <!doctype html>
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <h1>Hello, {$safe}</h1>
        <form method="post"><input name="name" placeholder="your name"><button>Set</button></form>
        <p>{$r->ip} via {$r->header('User-Agent')}</p>
        HTML);
});

serve_http(80);
printf("join '%s' (password '%s') and open http://%s/\n", AP_SSID, AP_PASS, wifi_ap_ip());
```
<!-- @endcode-block -->

## How it works

- `wifi_ap_start()` creates the network; the board runs a DHCP server on it and has the address printed by
  `wifi_ap_ip()` (the ESP-IDF default is `192.168.4.1`).
- The form posts `application/x-www-form-urlencoded`, so `$_POST['name']` is filled for that request.
- `$name` survives between requests: the script is resident. It is lost at restart.
- `htmlspecialchars()` matters here as on any server: the page echoes what visitors type.

To join an existing network instead, replace `wifi_ap_start()` with `wifi_connect($ssid, $password)` and
print `wifi_ip()`; see [ext-wifi](package:php-baremetal/ext-wifi).
