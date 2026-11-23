# Static IP: optional tick box + typed-address suggestions

Branch: `static-ip-suggest`

## What changed

The **Network** tab now has a *Use a static IP* tick box. Ticking it reveals
IP address / Gateway / Subnet mask / DNS server. As the address is typed, the
gateway, mask and DNS are suggested and filled in when the fields are still
empty (or still hold the previous suggestion); anything the operator has typed
themselves is never overwritten. The same behaviour is on the AP-fallback setup
page (`/settings`).

| File | Change |
|---|---|
| `EVDIpSuggest.h` | **new** — one implementation of the rules: a C++ copy (`evdIpContext`, `evdSuggestedIp/Gateway/Mask/Dns`, `evdCheckStaticIp`) and a JS copy injected into both pages (`evdSuggest`, `evdIpCheck`). |
| `EVDCTRL.h` | Network tab: tick box, four fields, live hint line, "Suggest from current network" button. `loadNet()` fills them once from `/api/network/state`. Save posts `static/ip/gateway/subnet/dns`. Placeholder `{{IPSUGGEST}}` for the injected script. |
| `EVDNTW.h` | Setup page: same fields, same suggestion behaviour, saved values first, suggestion only for what was never saved. |
| `EVDWiFi.h` | `/api/network/state` adds `ip_static`, `sugg{ip,gw,sn,dns,from}`. `/save` **refuses** a static configuration that cannot work (400 + reason) instead of storing it and rebooting into an unreachable board. |
| `EVDRoutes.h` | `/` builds the page as a `String` so the suggestion script can be injected from the live network context. |
| `EVDCTR.ino` | Includes `EVDIpSuggest.h` and the page source; file list updated. |
| `EVDCTRL_GZ.h` | Regenerated with `gen_gz.py`. |

## Where the suggestion comes from

1. The board's current DHCP lease (`WiFi.localIP/subnetMask/gatewayIP/dnsIP`) —
   the network's own opinion of what is valid here.
2. The saved gateway/mask/DNS, which survive a failed join.
3. A classful guess: `/24`, gateway `.1`, DNS = gateway.

The typed address only decides the network number: `192.168.100.240` suggests
gateway `192.168.100.1`, `10.0.0.7` suggests `10.0.0.1`. The address itself is
kept exactly as typed.

Validation (both in the page and on the board): IPv4 syntax, unicast range,
contiguous mask, gateway and DNS inside the subnet, address ≠ gateway.

## Testing

- `arduino-cli compile` for `esp32:esp32:esp32s3` (core 3.3.0, custom
  partition scheme): **builds**, 1 569 683 B flash, 96 172 B RAM.
- `.test/ipsuggest.test.cjs` — 38 assertions over the shipped JS (extracted from
  `EVDIpSuggest.h`): /24 and /16 leases, partial typing, saved values, no context,
  /32, every validation message, and both pages' script syntax. **38 passed.**
- `.test/dom.test.cjs` — jsdom drives the rendered pages: tick box reveals the
  fields, typing fills gateway/mask/DNS, an edited gateway survives further
  typing, off-subnet is flagged and save refuses, a valid configuration is
  posted with all four values. Runs with `node .test/render.cjs && node .test/dom.test.cjs`.

Not tested on hardware: no board was flashed. `/settings` and the dashboard were
rendered and driven in jsdom, not on an ESP32.
