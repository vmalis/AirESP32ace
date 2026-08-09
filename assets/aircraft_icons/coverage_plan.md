# Aircraft icon coverage plan

This plan is for the direct-generated neon/Apple-like asset set in `assets/aircraft_icons/individual_direct`.

Principles:

- Use one folder per aircraft/category.
- Keep each final asset on a consistent canvas: `400×400` and map `50×50`.
- Draw the airplane/helicopter content to scale inside the canvas.
- Use specific models where they are common, recognizable, politically/operationally important, or likely to appear in flight APIs.
- Use generic silhouettes for the long tail of aircraft type codes we cannot reasonably cover one-by-one.
- Avoid variant explosion: `Boeing 737`, not separate `737-800`, `737 MAX 8`, etc.
- Keep current aliases where useful, e.g. `COMAC C909 / ARJ21`.

## Civil fixed-wing specific set

The target is 60 specific civil fixed-wing assets:

- 50 broad airline / cargo / government / utility aircraft.
- 10 private / business / GA aircraft, as previously discussed.

### Core airline, cargo, regional, government, and utility aircraft — 50

| # | Slug | Label | Status |
|---:|---|---|---|
| 1 | `boeing-737` | Boeing 737 | Done |
| 2 | `airbus-a320` | Airbus A320 | Done |
| 3 | `airbus-a321` | Airbus A321 | Done |
| 4 | `airbus-a319` | Airbus A319 | Done |
| 5 | `embraer-e-jet` | Embraer E-Jet / E170-E195 | Done |
| 6 | `bombardier-crj` | Bombardier CRJ | Done |
| 7 | `airbus-a220` | Airbus A220 | Done |
| 8 | `boeing-787` | Boeing 787 | Done |
| 9 | `airbus-a350` | Airbus A350 | Done |
| 10 | `airbus-a330` | Airbus A330 | Done |
| 11 | `boeing-777` | Boeing 777 | Done |
| 12 | `boeing-767` | Boeing 767 | Done |
| 13 | `boeing-757` | Boeing 757 | Done |
| 14 | `boeing-747` | Boeing 747 | Done |
| 15 | `airbus-a380` | Airbus A380 | Done |
| 16 | `airbus-a340` | Airbus A340 | Done |
| 17 | `atr-72` | ATR 72 | Done |
| 18 | `dash-8` | De Havilland Canada Dash 8 / Q400 | Done |
| 19 | `saab-340` | Saab 340 | Done |
| 20 | `fokker-100` | Fokker 100 | Done |
| 21 | `bae-146` | BAe 146 / Avro RJ | Done |
| 22 | `md-80` | McDonnell Douglas MD-80 | Done |
| 23 | `boeing-717` | Boeing 717 | Done |
| 24 | `boeing-727` | Boeing 727 | Done |
| 25 | `boeing-707` | Boeing 707 | Done |
| 26 | `md-11` | McDonnell Douglas MD-11 | Done |
| 27 | `dc-10` | McDonnell Douglas DC-10 | Done |
| 28 | `lockheed-l-1011` | Lockheed L-1011 TriStar | Done |
| 29 | `airbus-a300` | Airbus A300 | Done |
| 30 | `airbus-a310` | Airbus A310 | Done |
| 31 | `concorde` | Concorde | Done |
| 32 | `comac-c919` | COMAC C919 | Done |
| 33 | `comac-c909` | COMAC C909 / ARJ21 | Done |
| 34 | `sukhoi-superjet` | Sukhoi Superjet | Done |
| 35 | `irkut-mc-21` | Irkut MC-21 | Done |
| 36 | `tupolev-tu-204` | Tupolev Tu-204 | Done |
| 37 | `tupolev-tu-154` | Tupolev Tu-154 | Done |
| 38 | `yakovlev-yak-42` | Yakovlev Yak-42 | Done |
| 39 | `ilyushin-il-96` | Ilyushin IL-96 | Done |
| 40 | `ilyushin-il-76` | Ilyushin IL-76 | Done |
| 41 | `ilyushin-il-62` | Ilyushin IL-62 | Done / regenerated |
| 42 | `antonov-an-24` | Antonov An-24 | Done |
| 43 | `antonov-an-148` | Antonov An-148 | Done |
| 44 | `antonov-an-124` | Antonov An-124 | Done |
| 45 | `antonov-an-225` | Antonov An-225 | Done |
| 46 | `dhc-6-twin-otter` | DHC-6 Twin Otter | Done |
| 47 | `cessna-208` | Cessna 208 Caravan | Done |
| 48 | `pilatus-pc-12` | Pilatus PC-12 | Done |
| 49 | `beechcraft-king-air` | Beechcraft King Air | Done |
| 50 | `let-l-410` | LET L-410 | Done |

### Private, business, and GA aircraft — 10

| # | Slug | Label | Status |
|---:|---|---|---|
| 1 | `gulfstream-g650` | Gulfstream G650 | Done |
| 2 | `global-7500` | Bombardier Global 7500 | Done |
| 3 | `falcon-8x` | Dassault Falcon 8X | Done |
| 4 | `cessna-citation` | Cessna Citation family | Done |
| 5 | `phenom-300` | Embraer Phenom 300 | Done |
| 6 | `pilatus-pc-24` | Pilatus PC-24 | Done |
| 7 | `hondajet` | HondaJet | Done |
| 8 | `cirrus-vision` | Cirrus Vision Jet | Done |
| 9 | `learjet-75` | Learjet 75 | Done |
| 10 | `cessna-172` | Cessna 172 | Done |

### Cargo / logistics supplement

The core 50 already includes the major cargo silhouettes: `boeing-757`, `boeing-767`, `boeing-777`, `boeing-747`, `airbus-a300`, `airbus-a330`, `md-11`, `dc-10`, `atr-72`, `cessna-208`, `ilyushin-il-76`, and `antonov-an-124`.

Add these if we want better FedEx/UPS/DHL/feeder coverage without relying only on generics:

| # | Slug | Label | Why it matters | Status |
|---:|---|---|---|---|
| 1 | `cessna-408-skycourier` | Cessna 408 SkyCourier | Modern FedEx feeder aircraft | Done |
| 2 | `atr-42` | ATR 42 | FedEx/DHL feeder turboprop class | Done |
| 3 | `embraer-120` | Embraer EMB 120 Brasilia | Regional feeder/cargo and older commuter aircraft | Done |
| 4 | `beechcraft-1900` | Beechcraft 1900 | Small cargo/commuter feeder aircraft | Done |
| 5 | `dornier-228` | Dornier 228 | Utility/regional/cargo aircraft | Done |
| 6 | `dc-8` | Douglas DC-8 freighter | Legacy cargo aircraft still useful for API/historical sightings | Done |
| 7 | `convair-580` | Convair 580 | Older regional cargo turboprop fallback | Done |
| 8 | `swearingen-metro` | Fairchild Swearingen Metro | Small cargo feeder aircraft | Done |
| 9 | `dornier-328` | Dornier 328 | Regional turboprop/jet-family fallback for older API sightings | Done |

### Private / business supplement

The 10 private/business/GA aircraft above are enough for a first pass, but if we want better real-world activity coverage, add these before adding more rare ultra-long-range jets:

| # | Slug | Label | Why it matters | Status |
|---:|---|---|---|---|
| 1 | `challenger-300-family` | Bombardier Challenger 300 / 350 / 3500 | Very common super-midsize business jet family | Done |
| 2 | `praetor-family` | Embraer Praetor 500 / 600 | Current high-activity midsize/super-midsize Embraer jets | Done |
| 3 | `gulfstream-g550` | Gulfstream G550 | Large active long-range Gulfstream fleet | Done |
| 4 | `citation-latitude` | Cessna Citation Latitude | Common midsize Citation; useful if `cessna-citation` is too generic | Done |

### Civil fixed-wing generic fallbacks

| # | Slug | Label | Use when API type is unknown or unsupported | Status |
|---:|---|---|---|---|
| 1 | `generic-narrowbody-twinjet` | Generic narrow-body twinjet | 737/A320-like unknowns | Done |
| 2 | `generic-widebody-twinjet` | Generic wide-body twinjet | 777/787/A330/A350-like unknowns | Done |
| 3 | `generic-four-engine-widebody` | Generic four-engine wide-body | 747/A340/A380/IL-96-like unknowns | Done |
| 4 | `generic-regional-jet` | Generic regional jet | E-Jet/CRJ/C909-like unknowns | Done |
| 5 | `generic-trijet` | Generic trijet | 727/DC-10/MD-11/Tu-154-like unknowns | Done |
| 6 | `generic-turboprop` | Generic twin turboprop | ATR/Dash/An-24/Saab-like unknowns | Done |
| 7 | `generic-single-prop` | Generic single prop | C172/C208/PC-12-like unknowns | Done |
| 8 | `generic-business-jet` | Generic business jet | private jet unknowns | Done |
| 9 | `generic-light-single-piston` | Generic light single piston | Cessna 172 / Piper Cherokee / Cirrus-like unknowns | Done |
| 10 | `generic-light-twin-piston` | Generic light twin piston | Baron / Seneca / small twin unknowns | Done |
| 11 | `generic-light-turboprop` | Generic light single turboprop | Caravan / PC-12-like unknowns | Done |
| 12 | `generic-utility-stol` | Generic utility/STOL aircraft | bush plane / floatplane / island utility unknowns | Done |
| 13 | `generic-small-cargo-feeder` | Generic small cargo feeder | SkyCourier / Beech 1900 / Metro-like unknowns | Done |

## Civil helicopters

Target: 20 specific civil/common rotorcraft plus 5 generic fallbacks.

### Specific civil/common helicopters — 20

| # | Slug | Label | Status |
|---:|---|---|---|
| 1 | `robinson-r44` | Robinson R44 | Done |
| 2 | `robinson-r22` | Robinson R22 | Done |
| 3 | `bell-206` | Bell 206 JetRanger | Done |
| 4 | `bell-407` | Bell 407 | Done |
| 5 | `bell-429` | Bell 429 | Done |
| 6 | `airbus-h125` | Airbus H125 / AS350 | Done |
| 7 | `airbus-h130` | Airbus H130 | Done |
| 8 | `airbus-h135` | Airbus H135 | Done |
| 9 | `airbus-h145` | Airbus H145 | Done |
| 10 | `airbus-h160` | Airbus H160 | Done |
| 11 | `sikorsky-s-76` | Sikorsky S-76 | Done |
| 12 | `sikorsky-s-92` | Sikorsky S-92 | Done |
| 13 | `leonardo-aw109` | Leonardo AW109 | Done |
| 14 | `leonardo-aw119` | Leonardo AW119 | Done |
| 15 | `leonardo-aw139` | Leonardo AW139 | Done |
| 16 | `leonardo-aw169` | Leonardo AW169 | Done |
| 17 | `md-500` | MD 500 | Done |
| 18 | `md-902` | MD Explorer / MD 902 | Done |
| 19 | `mil-mi-8` | Mil Mi-8 / Mi-17 civil/common | Done |
| 20 | `kamov-ka-32` | Kamov Ka-32 coaxial | Done |

### Civil helicopter generic fallbacks — 5

| # | Slug | Label | Status |
|---:|---|---|---|
| 1 | `generic-light-piston-helicopter` | Generic light piston helicopter | Done |
| 2 | `generic-single-turbine-helicopter` | Generic single turbine helicopter | Done |
| 3 | `generic-twin-engine-helicopter` | Generic twin-engine executive/EMS helicopter | Done |
| 4 | `generic-heavy-utility-helicopter` | Generic heavy utility helicopter | Done |
| 5 | `generic-coaxial-helicopter` | Generic coaxial helicopter | Done |

## Military coverage

For military, the target is not only generic silhouettes. For each major role, use:

- 2 representative NATO/Western examples.
- 2 representative Russian/Soviet examples.
- 2 representative Chinese examples.
- 1 generic fallback for unknown or unsupported API type codes.

This gives enough detail for recognizable aircraft while avoiding hundreds of exact types.

### Fighter / attack aircraft — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `lockheed-f-16` | Lockheed Martin F-16 Fighting Falcon | NATO/Western |
| 2 | `lockheed-f-35` | Lockheed Martin F-35 Lightning II | NATO/Western |
| 3 | `sukhoi-su-27-family` | Sukhoi Su-27 / Su-30 / Su-35 family | Russian/Soviet |
| 4 | `mig-29` | Mikoyan MiG-29 | Russian/Soviet |
| 5 | `chengdu-j-10` | Chengdu J-10 | Chinese |
| 6 | `chengdu-j-20` | Chengdu J-20 | Chinese |
| 7 | `generic-fighter` | Generic fighter / attack jet | Generic fallback |

### Bombers / strike aircraft — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `boeing-b-52` | Boeing B-52 Stratofortress | NATO/Western |
| 2 | `northrop-b-2` | Northrop Grumman B-2 Spirit | NATO/Western |
| 3 | `tupolev-tu-95` | Tupolev Tu-95 Bear | Russian/Soviet |
| 4 | `tupolev-tu-160` | Tupolev Tu-160 Blackjack | Russian/Soviet |
| 5 | `xian-h-6` | Xi'an H-6 | Chinese |
| 6 | `xian-jh-7` | Xi'an JH-7 fighter-bomber | Chinese |
| 7 | `generic-bomber` | Generic bomber / strike aircraft | Generic fallback |

### Military helicopters — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `sikorsky-uh-60-black-hawk` | Sikorsky UH-60 Black Hawk | NATO/Western |
| 2 | `boeing-ah-64-apache` | Boeing AH-64 Apache | NATO/Western |
| 3 | `mil-mi-8` | Mil Mi-8 / Mi-17 Hip | Russian/Soviet |
| 4 | `kamov-ka-52` | Kamov Ka-52 Alligator | Russian/Soviet |
| 5 | `harbin-z-20` | Harbin Z-20 | Chinese |
| 6 | `changhe-z-10` | Changhe Z-10 | Chinese |
| 7 | `generic-military-helicopter` | Generic military helicopter | Generic fallback |

### Unmanned aircraft — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `general-atomics-mq-9` | General Atomics MQ-9 Reaper | NATO/Western |
| 2 | `northrop-rq-4` | Northrop Grumman RQ-4 Global Hawk | NATO/Western |
| 3 | `orlan-10` | Orlan-10 UAV | Russian/Soviet |
| 4 | `kronstadt-orion` | Kronstadt Orion / Inokhodets | Russian/Soviet |
| 5 | `chengdu-wing-loong` | Chengdu Wing Loong | Chinese |
| 6 | `cai-hong-ch-4` | CASC Cai Hong CH-4 | Chinese |
| 7 | `generic-uav` | Generic UAV / drone | Generic fallback |

### Military transports — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `lockheed-c-130` | Lockheed C-130 Hercules | NATO/Western |
| 2 | `boeing-c-17` | Boeing C-17 Globemaster III | NATO/Western |
| 3 | `ilyushin-il-76-military` | Ilyushin IL-76 military transport | Russian/Soviet |
| 4 | `antonov-an-124-military` | Antonov An-124 military/heavy transport | Russian/Soviet |
| 5 | `xian-y-20` | Xi'an Y-20 | Chinese |
| 6 | `shaanxi-y-9` | Shaanxi Y-9 | Chinese |
| 7 | `generic-military-transport` | Generic military transport | Generic fallback |

### Tanker / aerial refueling — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `boeing-kc-135` | Boeing KC-135 Stratotanker | NATO/Western |
| 2 | `boeing-kc-46` | Boeing KC-46 Pegasus | NATO/Western |
| 3 | `ilyushin-il-78` | Ilyushin IL-78 Midas | Russian/Soviet |
| 4 | `tupolev-tu-16-tanker` | Tupolev Tu-16 / Badger tanker derivative | Russian/Soviet |
| 5 | `xian-y-20u` | Xi'an Y-20U tanker | Chinese |
| 6 | `xian-h-6u` | Xi'an H-6U tanker | Chinese |
| 7 | `generic-tanker` | Generic aerial refueling tanker | Generic fallback |

### AWACS / AEW&C — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `boeing-e-3` | Boeing E-3 Sentry | NATO/Western |
| 2 | `boeing-e-7` | Boeing E-7 Wedgetail | NATO/Western |
| 3 | `beriev-a-50` | Beriev A-50 Mainstay | Russian/Soviet |
| 4 | `beriev-a-100` | Beriev A-100 Premier | Russian/Soviet |
| 5 | `shaanxi-kj-500` | Shaanxi KJ-500 | Chinese |
| 6 | `xian-kj-2000` | Xi'an KJ-2000 | Chinese |
| 7 | `generic-awacs` | Generic AWACS / AEW&C aircraft | Generic fallback |

### Maritime patrol / surveillance — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `boeing-p-8` | Boeing P-8 Poseidon | NATO/Western |
| 2 | `lockheed-p-3` | Lockheed P-3 Orion | NATO/Western |
| 3 | `ilyushin-il-38` | Ilyushin IL-38 May | Russian/Soviet |
| 4 | `tupolev-tu-142` | Tupolev Tu-142 Bear-F/J | Russian/Soviet |
| 5 | `shaanxi-y-8-mpa` | Shaanxi Y-8 maritime patrol variant | Chinese |
| 6 | `shaanxi-y-9-mpa` | Shaanxi Y-9 maritime patrol variant | Chinese |
| 7 | `generic-maritime-patrol` | Generic maritime patrol aircraft | Generic fallback |

### Trainer / light combat trainer — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `bae-hawk` | BAE Hawk | NATO/Western |
| 2 | `boeing-t-7` | Boeing-Saab T-7 Red Hawk | NATO/Western |
| 3 | `yakovlev-yak-130` | Yakovlev Yak-130 | Russian/Soviet |
| 4 | `aero-l-39` | Aero L-39 Albatros | Russian/Soviet / Eastern bloc |
| 5 | `hongdu-jl-8` | Hongdu JL-8 / K-8 | Chinese |
| 6 | `guizhou-jl-9` | Guizhou JL-9 | Chinese |
| 7 | `generic-trainer` | Generic military trainer jet | Generic fallback |

### Special / uncommon but API-visible military forms — 7

| # | Slug | Label | Type |
|---:|---|---|---|
| 1 | `bell-boeing-v-22` | Bell Boeing V-22 Osprey tiltrotor | NATO/Western |
| 2 | `lockheed-u-2` | Lockheed U-2 high-altitude reconnaissance | NATO/Western |
| 3 | `mil-mi-26` | Mil Mi-26 heavy-lift helicopter | Russian/Soviet |
| 4 | `tupolev-tu-214r` | Tupolev Tu-214R surveillance aircraft | Russian/Soviet |
| 5 | `avics-ag600` | AVIC AG600 large amphibian | Chinese |
| 6 | `wz-7-soaring-dragon` | Guizhou WZ-7 Soaring Dragon UAV | Chinese |
| 7 | `generic-special-military` | Generic special military aircraft | Generic fallback |
