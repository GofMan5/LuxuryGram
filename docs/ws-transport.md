# WebSocket MTProto transport

When direct TCP to the Telegram DCs is blocked, LuxuryGram connects over
`wss://kws{N}[-1].web.telegram.org/apiws`, the endpoint Telegram's own web
clients use. It runs inside the normal MTP session racing; there is no local
proxy process and nothing to configure.

Setting: Settings → LuxuryGram → Other → Network → WebSocket transport
(`webSocketTransport` in `tdata/luxury_settings.json`: `off`, `auto`
(default), `always`). The row below it shows the live status of the active
account, and "WebSocket log" opens the last 200 transport events.

Code: the Qt-free logic (endpoints, HTTP upgrade, RFC 6455 framing, route
memory, log ring) is `mtproto/details/mtproto_websocket_logic.*`, covered by
`tools/tests/ws_transport_regressions.py`. The socket is
`mtproto/details/mtproto_websocket_socket.*`; `TcpConnection` creates it for
connections marked as WebSocket, so MTProto obfuscation, framing and timeouts
are the ones TCP already uses. The socket buffer sizes are applied once the
TCP connection is up, when Qt has a socket engine to take them.

## Endpoint facts (live-probed 2026-10-28)

- `GET /apiws` with `Sec-WebSocket-Protocol: binary` returns `101` with
  `Sec-WebSocket-Accept` and `Sec-WebSocket-Protocol: binary`, both
  - over TLS to `149.154.167.220:443` with SNI = Host =
    `kws{1..5}.web.telegram.org` or `kws{1..5}-1.web.telegram.org`, and
  - over TLS directly to DC IPs on 443 (149.154.175.50, 149.154.167.51,
    149.154.174.100, 149.154.170.100, 2001:67c:4e8:f002::a,
    2001:b28:f23d:f001::a) with the same SNI and Host.
- Without SNI the server returns 404. SNI is mandatory and DNS is not
  needed: the client dials IPs and sets the SNI. Certificates are verified
  with Qt's defaults; SSL errors are never ignored.
- `/apiws_test` returns 101 for the test DCs.
- The payload is the MTProto byte stream `TcpConnection` already produces:
  the 64-byte obfuscated2 prefix, then obfuscated frames. Each socket write
  becomes one masked binary WS frame. Reads are a byte stream assembled from
  frame payloads.
- The `-1` host serves the media cluster of a DC. If a front refuses it, the
  socket retries once without the suffix.

## When WebSocket is not used

No WS candidates are added when any of these holds: the mode is Off; any
proxy is set in the app (MTProto, SOCKS5, HTTP); "use system proxy" is set
and the OS reports a real proxy for `kws2.web.telegram.org:443`, asked on
the session thread at each connect, only after every other gate passed (with no system proxy configured WS
stays available); the DC is a temporary (special) or CDN one; the session belongs to the keys
destroyer; the DC has no kws front (production 1..5 and 203, test 1..3 have
one). The status line then says why. Connections made in such a round never
update the route memory, whichever racer wins or stalls.

## Candidates

For each connection attempt, in addition to the usual TCP/HTTP racers (or
instead of them, see below), the session appends WS racers, de-duplicated by
IP: the pinned front `149.154.167.220`, the DC's IPv4 from the DC options (or
a built-in address when the options have none), and the IPv6 when IPv6 is
enabled. All use port 443. WS racers have priority 0 and TCP has 1, so TCP
wins whenever it connects within the usual 2 s wait for a better
connection. The connect timeout is raised to at least the WS delay plus the
6 s WS handshake timeout while WS racers are present.

## Route memory

Per `(dc, media, test)` the client remembers which route last worked. Times
are the constants in the logic header.

| route | Auto mode decision |
|---|---|
| Unknown | TCP and WS race at once |
| Direct | TCP first, WS starts 2 s later |
| WebSocket, WS cooling down | TCP only |
| TCP stall window open (WS not cooling down) | WS only |
| WebSocket, direct re-probe due | TCP and WS race at once |
| WebSocket, otherwise | WS only |

Always mode offers WS only and ignores cooldowns. Off offers TCP only. A
decision with neither TCP nor WS is impossible: TCP is forced on.

| event | effect |
|---|---|
| TCP wins | route Direct |
| WS wins | route WebSocket, next direct re-probe in 5 min; each re-probe TCP loses doubles the interval up to 60 min |
| WS refused (HTTP status other than 101) | WS cooldown 30 min; a WebSocket route becomes Unknown |
| WS network failure before the first MTProto byte, or a WS connection that stalls | WS cooldown 30 s, 60 s, 120 s … up to 15 min; the second failure in a row turns a WebSocket route into Unknown |
| TCP connected but stalled | a Direct route becomes Unknown; TCP sits out the next rounds for 2 min, then 4, 8, 16 and at most 30 min on repeated stalls (the ladder restarts after a quiet 30 min). A WS failure or a WS stall inside the window brings TCP back at once |
| mode change, proxy turned on or off, network reachability change | everything is forgotten (switching from one proxy to another keeps the memory: no route is learned through a proxy) |

The TCP stall window exists for DPI that lets the TCP handshake through and
then drops the traffic: TCP connects first and wins every race on priority,
so without the window WS would never be tried.

Failures are counted per connection round, not per racer. A round of three
WS candidates that all fail is one failure (a refusal if any of them was
refused), recorded when the last WS racer of the round fails or the round
times out. A round that a winner ends records nothing for the losers, and
neither does one where any WS racer received MTProto bytes.

## Failure modes avoided

These come from the field log of the tg-ws-proxy reference implementation.

- **F1, pool runs dry under parallel media load.** There is no pre-opened
  pool and no per-DC cap: every MTProto connection opens its own WS on
  demand, so a burst of 5-10 media connections to one DC gets 5-10 sockets.
- **F2, "no fallback available".** There is no shared per-IP connecting lock.
  Every candidate is an independent racer; one failing never closes the
  connection while another candidate (TCP, WS, another address) is still
  racing. A route marked bad only delays or drops WS candidates for a while,
  and the decision can never leave zero candidates.
- **F3, reset right after the first packets on idle pooled sockets.** No
  socket is kept idle for later: a WS connection is opened right before the
  MTProto session uses it.
- **F4, long-lived WS dropped after ~92 s idle.** An established socket sends
  a WS ping after 30 s without writes and answers server pings. A close or
  reset after the first MTProto bytes arrived reconnects the session but does
  not count as a route failure; only failures before the first response do.
- **F5, a DC missing without manual redirect config.** Endpoints for every
  production DC 1..5 (main and media), DC 203 and the test DCs are built in.
  There is nothing to configure.

## Reading the log

Each line starts with `HH:MM:SS` and names the DC key, for example
`dc2 media`. Lines are mirrored to `log.txt` with a `WS:` prefix. Events:

- candidate start: `dc2 ws 149.154.167.220 sni=kws2.web.telegram.org`;
- upgraded, with the handshake time in ms;
- refused, with the HTTP status line;
- bad accept (the upgrade answer did not match the key, lacked `binary`,
  `Upgrade: websocket` or an `upgrade` token in `Connection`);
- TLS or socket error text, handshake timeout;
- route transitions: `dc2 media: route Direct -> WebSocket (ws/149.154.167.220)`;
- `mode changed: auto|off|always`, reachability reset.

Keys, auth data and payload bytes are never logged. "Copy logs" puts the
whole log on the clipboard for a bug report.

## Not implemented (deferred)

- A Cloudflare / HTTP/2 relay (`*.workers.dev` fronting) for networks that
  block `*.web.telegram.org` by SNI. Today such a network needs a proxy.
- Per-user custom WS endpoints.
