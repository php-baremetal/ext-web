---
eyebrow: 'ext-web · Recipe'
lede:    'Every open browser tab gets the state once a second over a WebSocket; any tab can send a command, acknowledged to that tab only.'
see_also:
  - { href: '../usage/websocket.md', meta: 'PHP API' }
  - { href: 'package:php-baremetal/ext-events', meta: 'Extension', label: 'ext-events' }
prev: { label: 'A page over Wi-Fi', href: './serve-a-page.md' }
next: { label: 'No next page', href: '#' }
---

# WebSocket broadcast

Same board and project `composer.json` as [A page over Wi-Fi](./serve-a-page.md). A timer from ext-events
drives the broadcast; the page connects back to `/ws`.

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
use Baremetal\Event;
use Baremetal\Events;
use Baremetal\Http\Message;
use Baremetal\Http\Request;
use Baremetal\Http\Response;

final class Beat extends Event {}

if (!wifi_ap_start('php-baremetal', 'baremetal')) {
    exit("cannot start the access point\n");
}

const PAGE = <<<'HTML'
<!doctype html>
<meta charset="utf-8"><title>Live</title>
<pre id="state">connecting...</pre>
<button id="bump">Bump</button>
<script>
  const ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onmessage = e => document.getElementById('state').textContent = e.data;
  document.getElementById('bump').onclick = () => ws.send(JSON.stringify({ op: 'bump' }));
</script>
HTML;

$state = ['tick' => 0, 'count' => 0];

Events::listen(Request::class, fn (Request $r): Response =>
    $r->path === '/' ? Response::html(PAGE) : Response::notFound());

Events::listen(Message::class, function (Message $m) use (&$state): void {
    $cmd = json_decode($m->text, true);
    if (($cmd['op'] ?? null) === 'bump') {
        $state['count']++;
    }
    $m->reply(json_encode(['ack' => $cmd['op'] ?? null, 'client' => $m->client]));
});

Events::listen(Beat::class, function () use (&$state): void {
    $state['tick']++;
    $clients = ws_broadcast(json_encode($state));
    if ($state['tick'] % 10 === 0) {
        echo "tick {$state['tick']}: {$clients} client(s)\n";
    }
});

every(1000, Beat::class);
serve_ws('/ws');
serve_http(80);

echo "join 'php-baremetal' and open http://", wifi_ap_ip(), "/\n";
```
<!-- @endcode-block -->

Open the page in two tabs: both update every second, and a click in one shows the new count in both on the
next tick, while only the clicking tab sees the `ack`.

## How it works

- `serve_ws('/ws')` adds the endpoint; the handshake never reaches PHP. Each frame becomes a `Message`, and
  `reply()` answers only its sender.
- `ws_broadcast()` queues one frame for every connected client and returns at once. A tab that cannot take
  data at that moment misses this frame, so each frame carries the whole state rather than a change.
- The `Beat`, `Message` and `Request` listeners never run at the same time, so `$state` needs no locking.
- Frames, requests and timer ticks share one 32-message queue: keep the broadcast rate in the tens per
  second at most.
