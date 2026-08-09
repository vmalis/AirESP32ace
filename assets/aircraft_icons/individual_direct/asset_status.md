# Aircraft icon asset status

Updated: 2026-07-28

## Current scale policy

- `side_detail.png`: compressed visual detail scale; the smallest aircraft are about 80% of the largest.
- `top_map/*.png` and `top_map/*_50.png`: very compressed physical scale for map readability.
- Top-map formula: `display long edge = canvas × 0.88 × (max(length,wingspan) / 88.4m)^0.30`.
- Audit result: 112 completed slugs, 784 PNG files, 0 canvas failures, 0 scale failures.

## Created

112 slugs are created with the full 7-file set: side detail, three 400px top styles, and three 50px top styles.

- `airbus-a220`
- `airbus-a300`
- `airbus-a310`
- `airbus-a319`
- `airbus-a320`
- `airbus-a321`
- `airbus-a330`
- `airbus-a340`
- `airbus-a350`
- `airbus-a380`
- `airbus-h125`
- `airbus-h130`
- `airbus-h135`
- `airbus-h145`
- `airbus-h160`
- `antonov-an-124`
- `antonov-an-148`
- `antonov-an-225`
- `antonov-an-24`
- `atr-42`
- `atr-72`
- `bae-146`
- `bell-206`
- `bell-407`
- `bell-429`
- `beechcraft-1900`
- `beechcraft-king-air`
- `boeing-707`
- `boeing-717`
- `boeing-727`
- `boeing-737`
- `boeing-747`
- `boeing-757`
- `boeing-767`
- `boeing-777`
- `boeing-787`
- `bombardier-crj`
- `cessna-172`
- `cessna-208`
- `cessna-408-skycourier`
- `cessna-citation`
- `challenger-300-family`
- `cirrus-vision`
- `citation-latitude`
- `comac-c909`
- `comac-c919`
- `concorde`
- `convair-580`
- `dash-8`
- `dc-10`
- `dc-8`
- `dhc-6-twin-otter`
- `dornier-228`
- `dornier-328`
- `embraer-120`
- `embraer-e-jet`
- `falcon-8x`
- `fokker-100`
- `generic-business-jet`
- `generic-coaxial-helicopter`
- `generic-four-engine-widebody`
- `generic-heavy-utility-helicopter`
- `generic-helicopter`
- `generic-light-piston-helicopter`
- `generic-light-single-piston`
- `generic-light-turboprop`
- `generic-light-twin-piston`
- `generic-narrowbody-twinjet`
- `generic-regional-jet`
- `generic-small-cargo-feeder`
- `generic-single-prop`
- `generic-single-turbine-helicopter`
- `generic-trijet`
- `generic-turboprop`
- `generic-twin-engine-helicopter`
- `generic-utility-stol`
- `generic-widebody-twinjet`
- `global-7500`
- `gulfstream-g550`
- `gulfstream-g650`
- `hondajet`
- `ilyushin-il-62`
- `ilyushin-il-76`
- `ilyushin-il-96`
- `irkut-mc-21`
- `kamov-ka-32`
- `learjet-75`
- `leonardo-aw109`
- `leonardo-aw119`
- `leonardo-aw139`
- `leonardo-aw169`
- `let-l-410`
- `lockheed-l-1011`
- `md-11`
- `md-80`
- `md-500`
- `md-902`
- `mil-mi-8`
- `phenom-300`
- `pilatus-pc-12`
- `pilatus-pc-24`
- `praetor-family`
- `robinson-r22`
- `robinson-r44`
- `saab-340`
- `sikorsky-s-76`
- `sikorsky-s-92`
- `sukhoi-superjet`
- `swearingen-metro`
- `tupolev-tu-154`
- `tupolev-tu-204`
- `yakovlev-yak-42`

## Not created yet

### Civil fixed-wing core / private / logistics / generic

None. Civil fixed-wing, business/private, cargo/logistics supplement, light aircraft fallbacks, and generic fixed-wing fallbacks are complete.

### Civil/common helicopters

None. Civil/common helicopter specifics and generic helicopter fallbacks are complete.

### Military aircraft / rotorcraft / UAV

- `lockheed-f-16`
- `lockheed-f-35`
- `sukhoi-su-27-family`
- `mig-29`
- `chengdu-j-10`
- `chengdu-j-20`
- `generic-fighter`
- `boeing-b-52`
- `northrop-b-2`
- `tupolev-tu-95`
- `tupolev-tu-160`
- `xian-h-6`
- `xian-jh-7`
- `generic-bomber`
- `sikorsky-uh-60-black-hawk`
- `boeing-ah-64-apache`
- `kamov-ka-52`
- `harbin-z-20`
- `changhe-z-10`
- `generic-military-helicopter`
- `general-atomics-mq-9`
- `northrop-rq-4`
- `orlan-10`
- `kronstadt-orion`
- `chengdu-wing-loong`
- `cai-hong-ch-4`
- `generic-uav`
- `lockheed-c-130`
- `boeing-c-17`
- `ilyushin-il-76-military`
- `antonov-an-124-military`
- `xian-y-20`
- `shaanxi-y-9`
- `generic-military-transport`
- `boeing-kc-135`
- `boeing-kc-46`
- `ilyushin-il-78`
- `tupolev-tu-16-tanker`
- `xian-y-20u`
- `xian-h-6u`
- `generic-tanker`
- `boeing-e-3`
- `boeing-e-7`
- `beriev-a-50`
- `beriev-a-100`
- `shaanxi-kj-500`
- `xian-kj-2000`
- `generic-awacs`
- `boeing-p-8`
- `lockheed-p-3`
- `ilyushin-il-38`
- `tupolev-tu-142`
- `shaanxi-y-8-mpa`
- `shaanxi-y-9-mpa`
- `generic-maritime-patrol`
- `bae-hawk`
- `boeing-t-7`
- `yakovlev-yak-130`
- `aero-l-39`
- `hongdu-jl-8`
- `guizhou-jl-9`
- `generic-trainer`
- `bell-boeing-v-22`
- `lockheed-u-2`
- `mil-mi-26`
- `tupolev-tu-214r`
- `avics-ag600`
- `wz-7-soaring-dragon`
- `generic-special-military`
