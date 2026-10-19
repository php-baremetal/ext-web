---
eyebrow: 'ext-web · Recipe'
lede:    'A small JSON API over wired Ethernet: list, read and add items kept in a PHP array, with proper status codes.'
see_also:
  - { href: '../usage/http.md', meta: 'PHP API' }
  - { href: '../reference/request-lifecycle.md', meta: 'Reference' }
prev: { label: 'Errors and limits', href: '../reference/errors-and-limits.md' }
next: { label: 'A page over Wi-Fi', href: './serve-a-page.md' }
---

# A JSON API

A Waveshare ESP32-S3-ETH on wired Ethernet. The SDK brings the W5500 up (DHCP) before PHP starts because the
`network` option is on, so `serve_http()` can run at once. `on-error` is `continue`, so a bug in one handler
answers 500 without stopping the server.

## The project

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/json-api",
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
      "options": { "network": "on", "on-error": "continue", "request-timeout": 10 }
    }
  }
}
```
<!-- @endcode-block -->

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
use Baremetal\Events;
use Baremetal\Http\Request;
use Baremetal\Http\Response;

$items = [];
$nextId = 1;

function error(int $status, string $message): Response
{
    return new Response($status, json_encode(['error' => $message]), 'application/json');
}

Events::listen(Request::class, function (Request $r) use (&$items, &$nextId): Response {
    if ($r->path === '/items' && $r->method === 'GET') {
        return Response::json(array_values($items));
    }

    if ($r->path === '/items' && $r->method === 'POST') {
        $data = $r->json();
        if (!is_array($data) || !is_string($data['name'] ?? null)) {
            return error(422, 'expected {"name": string}');
        }
        $item = ['id' => $nextId++, 'name' => $data['name']];
        $items[$item['id']] = $item;
        $created = Response::json($item);
        $created->status = 201;
        return $created->header('Location', "/items/{$item['id']}");
    }

    if (preg_match('#^/items/(\d+)$#', $r->path, $m) && $r->method === 'GET') {
        return isset($items[(int) $m[1]])
            ? Response::json($items[(int) $m[1]])
            : error(404, 'no such item');
    }

    return error(404, 'not found');
});

serve_http(80);
echo "json-api on http://", $_SERVER['SERVER_ADDR'] ?? '?', "/items\n";
```
<!-- @endcode-block -->

## Try it

<!-- @code-block language="bash" label="from your computer" -->
```bash
curl -s http://192.168.1.50/items
curl -s -X POST -H 'Content-Type: application/json' -d '{"name":"sensor"}' http://192.168.1.50/items
curl -s http://192.168.1.50/items/1
```
<!-- @endcode-block -->

Use the address the board prints at boot (`phpbm: network w5500 ...`) instead of `192.168.1.50`.

## Notes

- `$items` lives in RAM for the whole run and is lost at restart. Persist it with `ext-store` if needed.
- A request body is parsed into `$_POST` only for form posts; for JSON, `$r->json()` is the way.
- `preg_match()` is part of the PHP core build, so routing with patterns needs no extra package.
