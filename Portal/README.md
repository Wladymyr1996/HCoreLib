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
HRestApi/       the shared routes, and registration for an application's own
HFactoryReset/  erase the configuration and restart, via a hook for what it cannot know
```

`HAuth` — the admin password and session keys — is deliberately **not** here. It
needs no radio, so it lives in HCoreLib proper where a host test can compile it.

## What an application provides

| | |
| --- | --- |
| the page | `HStaticUi::setPage(start, end, Gzip)` — its own `WebUi/`, packed by `HCoreLib/tools/packui.py` and embedded |
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
HStaticUi::registerRoutes(server); // /, /index.html, /* → 302
HWebServer::registerFallback();    // POST /* → 404
```

Get that order wrong and the symptom is a portal where the API answers with a
web page.

See [Docs/Configuring.md](../../Docs/Configuring.md) for the routes themselves
and how a phone finds the device.
