# Changelog

## 1.0.3

- Docs: requests and WebSocket frames take a small event slot of ext-events 1.1.0 (`events-small`), no
  longer a place in a 32-message queue; a request with no free slot is answered 503 and counted in
  `Events::stats()`.

## 1.0.2

- Fixed: Reflection described `Response::text()` and `Response::html()` with an optional `$body`, but both
  require it; it also gave `Response::notFound()` an empty default instead of `'Not Found'`.
- Fixed: the static factories `ok()`, `text()`, `html()`, `json()`, `notFound()` and `noContent()` now declare
  their return type `Baremetal\Http\Response`, and `json()` declares `mixed $data`.

## 1.0.1

- Changed: the author's contact email.
- Docs: projects use `"sdk": "esp32@^2.0"`.

## 1.0.0

Initial release.
