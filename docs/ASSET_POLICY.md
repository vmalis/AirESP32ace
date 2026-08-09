# Asset and data policy

This public repository separates project-owned source material from content fetched for an individual installation.

## Included

- Project-created and AI-assisted city, aircraft, menu, metric, weather, startup, and launch artwork, including the aircraft source sheets used to derive device icons.
- Launch-agency artwork used by the built-in NASA, Roscosmos, and SpaceX views.
- Small device-ready assets required before the companion can connect.
- Country flags from the MIT-licensed [`flag-icons`](https://github.com/lipis/flag-icons) project; see `assets/flags/SOURCE.md`.
- Natural Earth geometry used to generate maps locally. Natural Earth data is public domain.
- Source code that generates, converts, installs, and caches device artwork.

## Not included

- API keys, OAuth/client secrets, Wi-Fi credentials, `sdkconfig`, or provisioned NVS images.
- Pre-rendered city, route, weather, launch-site, or ISS map caches.
- Downloaded airline/cargo logos.
- The proprietary watch logo/mark or commercial fonts.
- ESP-IDF, Waveshare SDK copies, fetched managed components, or build products.

## Runtime behavior

The macOS companion generates map bitmaps for the selected cities and routes, then sends them to the paired tracker. Those generated maps are installation-specific and are never source assets. Airline artwork is requested and cached locally only when needed. Users are responsible for complying with each service's terms and for deciding whether locally cached content may be retained.

Names and trademarks are used descriptively. Their inclusion in provider data does not imply endorsement or affiliation.
