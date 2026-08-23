# Portal

The settings mode every Hatynka device shares: a network, a web server, a REST
API, a page, a way back to factory defaults.

A component of its own, like `Devices/`, and for the same reason — it is the
only part of HCoreLib that needs a radio and an HTTP server. A device with no
configuration portal builds none of this and does not inherit the mDNS
dependency either.

```
HNetwork/       access point or station, mDNS, the address the rest reports
HWebServer/     the server's lifetime, and the POST catch-all
HCaptiveDns/    answers every name with this device, so a phone prompts
HStaticUi/      serves the page the application hands it; redirects everything else
                  (and the favicon, which is the library's own - see below)
HRestApi/       the shared routes, and registration for an application's own
HFactoryReset/  erase the configuration and restart, via a hook for what it cannot know
```

`HAuth` — the admin password and session keys — is deliberately **not** here. It
needs no radio, so it lives in HCoreLib proper where a host test can compile it.

## What an application provides

| | |
| --- | --- |
| the page | `HStaticUi::setPage(start, end, Gzip)` — its own `WebUi/`, packed by `HCoreLib/tools/packui.py` and embedded |
| *not* the favicon | that one is the library's, and there is nothing to call |
| its routes | `HRestApi::add(method, uri, handler, auth)` between `begin()` and `finish()` |
| its caches | a hook on `HFactoryReset::onErase()` for anything outside `config/` |
| its identity | `HNETWORK_*` in `HCoreLibConfig.h` — SSID prefix, hostname, AP or station |

Nothing here knows what the device measures, what its screens are, or what its
settings mean. That is the line: identical behaviour in the library, everything
particular in the application.

## Registration order is matching order

The server matches routes in the order they were registered, so every wildcard
has to come after every exact path it would otherwise swallow:

```cpp
HRestApi::begin(server);          // /api/auth, /api/info, …
DeviceApi::registerRoutes();      // this device's exact paths
HRestApi::finish();               // OPTIONS *, /api/* → 404
DevicePage::install();
HStaticUi::registerRoutes(server); // /, /index.html, /favicon.*, /* → 302
HWebServer::registerFallback();    // POST /* → 404
```

Get that order wrong and the symptom is a portal where the API answers with a
web page.

**Count them.** `HWEBSERVER_MAX_ROUTES` caps the table, the library alone needs
sixteen, and what fails when it is too small is whatever registered *last* —
which is always a catch-all. The portal then serves its page and answers its API
perfectly while quietly never raising the sign-in prompt that makes anybody open
it. The breakdown is on the macro in
[HWebServer.hpp](HWebServer/HWebServer.hpp).

## The favicon

`HStaticUi/favicon.png` is embedded into this component and served at both
`/favicon.ico` and `/favicon.png`. It is the one asset the library ships rather
than the application, and deliberately so: the page is a particular device's,
but to a browser every device in the ecosystem is the same product, and the icon
on the tab says so. There is no setter and nothing to register.

The `.ico` spelling is the one that does the work — a browser asks for it
unprompted, so the icon appears on a page whose markup never mentions it,
including one written before this existed. Unlike the page, it is sent with a
`Cache-Control` that permits caching: it changes only with a firmware update,
and a day-old copy is still the right picture.

See [Docs/Configuring.md](../../Docs/Configuring.md) for the routes themselves
and how a phone finds the device.
