# WiFi connecting — remaining gaps

**Status: the display flow ships; these three are what's left.** (updated
2026-07-27) Settings → WiFi scans, lists, takes a passphrase and connects,
confirmed working on the unit. What that flow does and why it's shaped that way is
in the code and in `docs/reasoning/2026-07-27-13h06-navigator-carries-no-payload.md`
+ `2026-07-27-13h06-2-runtime-wifi-credentials.md` — not repeated here.

- **Wrong-password feedback is indirect.** A bad passphrase surfaces as three
  connect attempts and then "own access point", never "wrong password".
  `WIFI_EVENT_STA_DISCONNECTED` carries a reason code that `NetworkManager` drops
  on the floor; plumbing it through would let the screen say what actually
  happened. This is the one of the three with ship-quality weight for RA2-395 — a
  technician who mistypes a key currently gets a 30-second silence and a wrong
  answer.
- **No hidden-network entry.** Nameless scan results are filtered out of the list
  and there's no "join other network" affordance to type an SSID by hand.
- **Web UI has no WiFi page.** Still only the generic settings rows for
  `wifi.ssid` / `wifi.password`. Note that a web page must *not* simply call
  `NetworkManager::ConnectToStation()` — it tears down the fallback AP, which
  would strand a client that reached the unit over that AP.
