# WebUi — the shared configuration page

The half of a Hatynka device's configuration page that is the same on every
device in the family.

```
core.css     the design system: bar, LED, chip, panels, fields, buttons, modal
core.js      HCore: transport, link LED, admin session, factory reset, i18n, polling
default.htm  a working page built from both — the starting point for a new device
```

The first two are never served on their own: an application links both from its
own `index.htm`, and [`../tools/packui.py`](../tools/packui.py) folds the lot into
the one HTML file the device serves. The third is not shipped at all — it is the
example you copy from.

An application is then only what its device actually *is*: its readings, its
settings, its words. `HTHP/WebUi/app.js` is around 300 lines because everything
below is not in it.

## Start here

**Open [`default.htm`](default.htm) in a browser.** It is a complete page built
on both files: hello world, two live readings, a settings panel, the whole shell,
and a catalogue of every class `core.css` offers, drawn so you can see it.

With no device to talk to it still draws — the LED goes red, the readings show
dashes, and everything else works exactly as it will on the real thing. Type an
address into its Device panel and the same page starts talking to a device.

To start an application from it, copy it to your `WebUi/index.htm`, repoint the
two links at `../HCoreLib/WebUi/`, move its script block into `WebUi/app.js`, and
delete the Components panel. The file says the same thing at the top of itself.

## What HCore does for you

- **Transport.** `HCore.api()` — one place for every request, so every one of
  them is timed out. `fetch` has none of its own, and an access point can vanish
  mid-request when somebody walks off with the thing.
- **Link LED.** Green reachable, yellow trying, red gone — driven by whether
  requests come back, not by anything the device says about its own radio. From
  a browser those are the same question. A 401 keeps it green: that is the
  device talking, and blaming the network for a password would be a lie.
- **The administrator session.** The chip, the modal, and all three ways into
  it — signing in, choosing the first password on a device that has none, and
  changing an existing one. Changing it signs you straight back in, because the
  device invalidates every key it has issued when the password changes,
  including the one that just made the change.
- **Losing the link signs you out.** A key belongs to a session on a device the
  page can no longer reach, and the likely reasons the link dropped — reboot,
  sleep, reset — are all reasons the key is already dead. When the link returns
  HCore asks what this browser is now rather than assuming, so a device that came
  back factory-reset correctly reports `first`.
- **Factory reset.** Confirm, a timed progress bar, then "Device reset." and a
  Close button — and the page stops asking. The device is erasing its filesystem
  and rebooting into Normal, where there is no server to answer; retrying there
  would report a failure for something that worked.
- **Firmware version** in the footer, from `GET /api/info`.
- **Translation.** A dictionary of its own for the chrome, `addStrings()` for
  the application's, and `setLanguage()` so an unsaved select takes effect
  immediately.
- **One poll timer**, stopped while the tab is hidden. One, not many: the device
  serving the page is an access point running off a battery.
- **Where to talk.** Same-origin when the device served the page; the `?device=`
  query, `localStorage`, or `config.json` when it did not — see *Development
  panel* below.

## Using it from an application

```html
<link rel="stylesheet" href="../HCoreLib/WebUi/core.css">
<link rel="stylesheet" href="app.css">
...
<!-- Core first: app.js reads HCore at parse time, and neither has `defer`. -->
<script src="../HCoreLib/WebUi/core.js"></script>
<script src="app.js"></script>
```

```js
HCore.addStrings({ en: { … }, uk: { … } });   // your words; same id overrides core's
HCore.onRender(render);                        // redraw whatever you own
HCore.onStatus(setStatus);                     // where a short sentence goes
HCore.begin(start);                            // resolve the device, then boot
```

`start` is *kept*, not called and forgotten: pointing the development panel at a
different device re-runs it, and only the application knows what its boot is.

### The whole surface

| | |
| --- | --- |
| `api(path, options)` | `{status, ok, data}`, never throws. `{method, body, auth, timeout}` |
| `units.temperature(c, unit)` | `{value, unit, decimals}`; `'fahrenheit'` or Celsius |
| `units.pressure(hPa, unit)` | `{value, unit, decimals}`; `'mmHg'` or hPa |
| `t(id)` | a word, in the current language, falling back to English then to `id` |
| `addStrings({lang: {id: text}})` | merge a dictionary in; later wins |
| `setLanguage(lang)` | draw in this language from now on, and redraw |
| `isAdmin()` | is a key held |
| `checkAuth()` | ask the device what this browser is |
| `openPasswordChange()` | the change-password modal, for your own button |
| `loadInfo()` | firmware version into the footer |
| `poll(task, ms)` | the page's one recurring request; replaces any previous |
| `render()` | redraw the chrome, then call your `onRender` |
| `begin(start)` | resolve the device, wire the shell, run `start` |
| `state` | `base`, `key`, `auth`, `link`, `lang`, `fw` — read, do not write |
| `$`, `show`, `setText`, `on`, `setDisabled`, `setLoading` | null-tolerant DOM |

Every DOM helper tolerates a missing element, and so does everything HCore draws.
`packui.py` deletes the development panel from the release build, and a sibling
device may simply have no factory reset — a shared layer that assumes markup is
a shared layer that only works in the app it was written for.

### The markup it drives

All optional; leave out what the device does not have.

| Element | Is |
| --- | --- |
| `#linkDot` | the link LED |
| `#authChip`, `#authChipText` | the session chip; the whole label is the button |
| `#modal`, `#modalTitle`, `#modalText`, `#modalError` | the password dialog |
| `#passLabel`, `#passInput`, `#modalCancel`, `#modalSubmit` | its field and buttons |
| `#changePassButton` | opens the dialog in change mode; put it where it belongs |
| `#dangerPanel`, `#dangerTitle`, `#dangerText`, `#resetButton` | the reset panel |
| `#resetModal`, `#resetTitle`, `#resetText` | the reset dialog |
| `#resetProgress`, `#resetBar` | its timed bar |
| `#resetCancel`, `#resetConfirm`, `#resetClose` | its buttons |
| `#devPanel`, `#deviceInput`, `#deviceButton`, `#deviceState` | development only |
| `#fwVersion` | the footer version |

### The API it expects

`GET /api/auth` · `POST /api/auth` · `POST /api/setAdminPassword` ·
`POST /api/factoryReset` · `GET /api/info` — served by `Portal/HRestApi` and
`HAuth`. Anything else is the application's own.

## Development panel

`#devPanel` answers "which device?" when the page was opened as a file instead of
served by one. It is the only thing in the UI marked **debug**, and it cannot
reach a user: `packui.py` deletes the section, and a page served by a device
always talks to whatever origin served it.

Because the shared files sit outside the application's folder, serve the
**repository root** rather than `WebUi/`:

```bash
python -m http.server 8000            # from the repo root
# then http://localhost:8000/WebUi/index.htm
```

A `file://` page cannot read `config.json` — the origin is opaque, so the browser
blocks the fetch — which is exactly why the remembered address and the typed one
exist. Cross-origin calls to the device itself are fine: the API answers
`Access-Control-Allow-Origin: *` and handles the preflight.

## Rules this layer is built to

- **Nothing from the internet.** No CDN, no web fonts, no npm. The device serving
  this page has no route to one, and a phone joined to its access point has
  nowhere else to look.
- **No build step and no ES modules.** A page opened as a file has an opaque
  origin and a browser refuses to load a module from one. A classic script and a
  single global is the only shape that works both from `file://` and from the
  device.
- **The library never assumes the application.** What only a device knows arrives
  as a string, a hook, or markup that may not be there — the same rule the
  firmware side of HCoreLib is built to.
- **Readable in the release.** `packui.py` strips comments and blank lines and
  nothing else. The saving is small next to gzip, and a released file that can
  still be read is worth more than the bytes.
