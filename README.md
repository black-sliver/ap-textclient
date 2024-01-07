# AP Text Client

This is an [Archipelago Multiworld](https://github.com/ArchipelagoMW/Archipelago) text client.

## How does it work

It compiles to WASM+JS and runs in your browser.

## How to use it

You can build it (or download a release) and host it on `http://localhost:8000`
using `serve.py`. Or visit an URL that hosts it.

Use `/connect <host>` command to connect to an AP server.

## How to build it

see `build-*.sh`

## Local storage

* The client will store a random ID in local storage to be able to replace the
  previous connection from the same client (crash or lost connectivity).
  This ID is only shared between the client and the AP server `/connect`ed to.
* The client stores a cache of item and location names to reduce traffic.
* The client may store the last 24hrs of output to a log.
* The http server hosting the client does not store any of that.
* The local storage can be cleared at any time through browser features.

## Attribution

Based on [ap-soeclient](https://github.com/black-sliver/ap-soeclient/).
Check [the source](https://github.com/black-sliver/ap-textclient/) for all referenced projects.

Binary distributions may include or link to

* [OpenSSL](https://github.com/openssl/openssl)
* [ASIO](https://github.com/chriskohlhoff/asio)
* [WebSocketPP](https://github.com/zaphoyd/websocketpp)
* [Curl's CA Extract](https://curl.se/docs/caextract.html)
* [nlohmann::json](https://github.com/nlohmann/json)
* [valijson](https://github.com/tristanpenman/valijson)
