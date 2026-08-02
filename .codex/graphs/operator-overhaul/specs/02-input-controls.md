# Input and per-tab controls

Global contract:

- Left/Right short: previous/next tab, including correct wrap from TX.
- Back short: open Settings; when Settings is open, close it; when TX is armed or
  active, disarm/stop first. Back long: exit.
- Input repeat is accepted only for list/page navigation and never for OK, Back,
  arming, lock, dump, or transmit.
- Sound and vibration are controlled only in Settings.
- Every tab shows its current primary and secondary button actions.

Per-tab contract:

- RF: Up/Down selects Survey, Sweep, Peak. Sweep/Peak short OK starts or cancels.
  Long OK locks/unlocks only a completed valid RF candidate while idle. Long
  Left/Right changes coarse band only while idle.
- Wi-Fi/BLE: Up/Down browses every stored observation; short OK restarts; long OK
  locks/unlocks the selected observation, not an invisible strongest item.
- GPS: Up/Down changes detail page; short OK retries when stale/unavailable or
  sets/clears a mark when a fresh position exists. Long OK has no hidden action.
- TX: Up/Down chooses candidate/presets while armed; short OK arms; long OK starts
  only after preflight; Back stops/disarms; leaving the tab disarms.
- Info: Up/Down changes information page; no false lock action.
- Settings: Up/Down selects, OK changes/action; rows scroll through all items.

Test every key/type combination in disarmed, armed, active, scanning, empty,
result, running sweep, completed sweep, fresh GPS, and stale GPS states.
