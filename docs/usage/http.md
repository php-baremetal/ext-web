---
eyebrow: 'ext-web · PHP API'
lede:    'serve_http() starts the server; each request is a Request event, and the first listener that returns a Response answers it.'
see_also:
  - { href: './websocket.md', meta: 'PHP API' }
  - { href: '../reference/request-lifecycle.md', meta: 'Reference' }
  - { href: '../recipes/json-api.md', meta: 'Recipe' }
prev: { label: 'Overview', href: '../overview.md' }
next: { label: 'WebSocket', href: './websocket.md' }
---

# HTTP

## serve_http()

<!-- @method name="serve_http(int $port = 80): bool" returns="bool" -->

<!-- @params -->
<!-- @param name="$port" type="int" default="80" -->
TCP port, 1 to 65535; `\ValueError` otherwise.
<!-- @endparam -->
<!-- @endparams -->

Starts the one HTTP server of the firmware and routes every path and method (`GET`, `POST`, `PUT`, `PATCH`,
`DELETE`, `HEAD`, `OPTIONS`) to the `Request` listeners. Returns `true`; a second call with the same port
returns `true` and does nothing. Throws `\Exception` when:

- the server already runs on another port (`serve_http(): the server already runs on port ..`);
- there is no network interface yet
  (`serve_http(): no network interface (turn the network option on, or connect with ext-wifi first)`);
- the server cannot start, or its routes cannot be registered.

Call it from the top-level script, after the `Request` listener is registered and the network is up.

## Request

`Baremetal\Http\Request` extends `Baremetal\Event`. Register for it with
`Events::listen(Request::class, ...)`; the listener receives one object per request.

| Property | Type | Content |
| --- | --- | --- |
| `$method` | `string` | `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD` or `OPTIONS`. |
| `$path` | `string` | The path without the query string, e.g. `/api/items`; at most 255 bytes, longer is truncated. |
| `$query` | `string` | The raw query string without `?`; at most 255 bytes. |
| `$body` | `string` | The whole request body (`''` when there is none). |
| `$ip` | `string` | The client's IP address (an IPv4 client is shown as plain IPv4). |
| `$headers` | `array` | The captured request headers, name to value (see below). |

<!-- @method name="Request::json(): mixed" returns="mixed" visibility="public" -->

`json_decode($this->body, true)`: the body as an associative array, or `null` when it is not valid JSON.

<!-- @method name="Request::header(string $name): ?string" returns="?string" visibility="public" -->

One captured header by name, case-insensitively, or `null`.

Only these request headers are captured, under these spellings: `Host`, `Content-Type`, `User-Agent`,
`Accept`, `Cookie`, `Authorization`, `X-Requested-With`, `Accept-Language`. Other headers do not reach PHP.

## Response

`Baremetal\Http\Response` is a plain object; the reply is built from its public properties when the listener
returns it.

| Property | Type | Default |
| --- | --- | --- |
| `$status` | `int` | `200`. Outside 100 to 599 it is sent as 500. |
| `$body` | `string` | `''` |
| `$contentType` | `string` | `'text/html; charset=UTF-8'` (at most 127 bytes are sent) |
| `$headers` | `array` | `[]`: name to value, or name to a list of values. |

<!-- @method name="new Response(int $status = 200, string $body = '', string $contentType = 'text/html; charset=UTF-8')" visibility="public" -->

Static helpers, each returning a new `Response`:

| Method | Status | Content type |
| --- | --- | --- |
| `Response::ok(string $body = '')` | 200 | `text/html; charset=UTF-8` |
| `Response::html(string $body)` | 200 | `text/html; charset=UTF-8` |
| `Response::text(string $body)` | 200 | `text/plain; charset=UTF-8` |
| `Response::json(mixed $data)` | 200 | `application/json`, body `json_encode($data)` (`''` if encoding fails) |
| `Response::notFound(string $body = 'Not Found')` | 404 | `text/plain; charset=UTF-8` |
| `Response::noContent()` | 204 | empty body |

<!-- @method name="Response::header(string $name, string $value, bool $replace = true): static" returns="static" visibility="public" -->

Sets a response header on this object and returns it, so calls chain. `Content-Type` sets `$contentType`.
With `$replace` false, an existing header of the same name is kept and this value is sent as well. Throws
`\ValueError` for a name that is not a valid token, for `Content-Length` or `Transfer-Encoding` (the server
writes those), and for a value containing CR, LF or NUL.

<!-- @code-block language="php" label="headers" -->
```php
<?php
use Baremetal\Http\Response;

return Response::json(['ok' => true])
    ->header('Cache-Control', 'no-store')
    ->header('Set-Cookie', 'a=1; Path=/')
    ->header('Set-Cookie', 'b=2; Path=/', replace: false);
```
<!-- @endcode-block -->

Writing `$headers` directly also works; at send time invalid or reserved headers, and any beyond 16, are
dropped with a warning.

## Answering

The `Request` listeners run in registration order:

- the first one that returns an object answers; if it is not a `Response` the client gets 500
  `handler did not return a Response`;
- a listener that returns anything else (`null`, `void`, a string) passes the request to the next one;
- if none returns an object, the client gets 404 `Not Found`;
- if no `Request` listener is registered at all, the client gets 503 `Service Unavailable`;
- if a listener throws, the client gets 500 `Internal Server Error` and the exception follows `on-error`.

A `HEAD` request reaches the listeners with `$method` `'HEAD'`; whatever they return, only the status line
and headers are sent, with the `Content-Length` of the body.

## Superglobals

Each request also fills the usual superglobals, so existing request-handling code works:

| Superglobal | Content |
| --- | --- |
| `$_GET` | The query string, parsed like `parse_str()`. |
| `$_POST` | The body parsed the same way, only for `Content-Type: application/x-www-form-urlencoded`; otherwise empty (read JSON with `$r->json()`). |
| `$_REQUEST` | `$_GET` merged with `$_POST`, `$_POST` winning. |
| `$_SERVER` | The startup entries (`SCRIPT_FILENAME`, `SERVER_ADDR`, ...) plus `REQUEST_METHOD`, `REQUEST_URI`, `QUERY_STRING`, `REMOTE_ADDR`, `SERVER_SOFTWARE` (`phpbm`), `SERVER_PROTOCOL` (`HTTP/1.1`), `CONTENT_LENGTH` when there is a body, `CONTENT_TYPE`, and `HTTP_*` for the other captured headers. |

`$_COOKIE` and `$_FILES` are not filled; read `$r->header('Cookie')` for cookies.
