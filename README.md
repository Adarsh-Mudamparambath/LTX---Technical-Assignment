# FlightViz — LTX AE Programming Assignment

C++20 / Qt 6 / OpenGL application for the NZ5 flight-data assignment.

## Status

| Task | Description | State |
|---|---|---|
| 0 | Download `NZ5_Flightdatar` as CSV | done — `data/NZ5_Flightdata.csv` (470 rows) |
| 1 | CSV → `vector<FlightRecord>`, `std::optional` for NULLs, QTableView | done |
| 2 | Button: sort by `Time` (12-hour AM/PM source) and refresh the view | done |
| 3 | Button: export sorted data as GeoJSON, Time in ISO format | done |
| 4 | Button: two filtered binary files, 15-byte MSB-first struct, `std::async` | done |
| 5 | 2D map + 3D globe OpenGL windows textured from `World_10M.mbtiles` | done |
| 6 | Mouse manipulators, 2D/3D views synchronised on lat/lon | done |
| 7 | Trajectory as a connected yellow line | done |
| 8 | Geodesic between first and last fix as a red line | done |

## Getting the application

**To just run it** — download `FlightViz-win64-portable.zip` from the
[Releases](../../releases) page, unzip anywhere, and double-click
`flightviz.exe`. Nothing to install: the Qt runtime, the flight data and the
map tiles are all inside. See `RUN-ME.txt` in the zip for a short tour.

Requires 64-bit Windows and a GPU supporting OpenGL 3.3.

**To build it from source** — see [Build](#build) below. You will need Qt 6.5+,
CMake and a C++20 compiler. This repository holds source only; build output is
deliberately not committed.

---

**[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** walks through the whole
design: how the pieces fit, every decision that needed judgement, how each
claim was verified, and a glossary of the domain concepts involved.

## Deliverables

`output/` holds the generated artifacts, produced by the committed source:

| File | What it is |
|---|---|
| `flight.geojson` | Task-3 export — 470 Point features plus a LineString, ISO 8601 times |
| `course_lt_220.bin` | Task-4, `Course < 220`, 200 records, exactly 3000 bytes |
| `altitude_37900_38000.bin` | Task-4, `37900 < AltitudeFeet <= 38000`, 3000 bytes |
| `app_window.png` | The running application — table, 2D map and 3D globe |
| `pane_map2d.png` | The 2D map pane's own framebuffer |
| `pane_globe.png` | The 3D globe pane's own framebuffer |
| `selftest.txt` | `--selftest` report for Tasks 1–4, 7 and 8 |

## Before building

`data/World_10M.mbtiles` (~90 MB) is **not committed** — it ships with the
assignment, and GitHub warns above 50 MB. Copy it in before building:

```
data/World_10M.mbtiles
```

Without it CMake prints a warning and the app still runs; the map and globe
panes just come up empty. Everything else in the repo is self-contained.

## Layout

```
  CMakeLists.txt
  cmake/Dependencies.cmake     FetchContent: nlohmann/json, GeographicLib
  data/NZ5_Flightdata.csv      Task-0 output
  data/World_10M.mbtiles       supplied separately (see above)
  src/
    model/FlightRecord.h       the row struct (std::optional members)
    model/CsvLoader.*          RFC 4180 parser, 12-hour time parsing
    model/FlightTableModel.*   QAbstractTableModel behind the QTableView
    io/GeoJsonWriter.*         Task-3
    io/BinaryWriter.*          Task-4
    map/Mercator.h             Web Mercator <-> lat/lon
    map/MBTilesReader.*        SQLite tile archive reader (Qt6::Sql)
    map/FlightTrack.*          Task-7, time-ordered path + date-line unwrap
    map/GeodesicPath.*         Task-8, WGS84 geodesic via GeographicLib
    gl/LineBuffer.h            shared polyline VAO/VBO for both overlays
    gl/Map2DWidget.*           Task-5, 2D slippy map
    gl/Globe3DWidget.*         Task-5, textured globe
    ui/MainWindow.*            button row + table | map | globe
    main.cpp                   entry point and --selftest
```

## Task-5 notes

`World_10M.mbtiles` is a SQLite archive: 256×256 PNG tiles, Web Mercator,
zoom 1–6, `scheme=tms`. It declares `minzoom=0` but holds nothing at z=0, so
the reader takes its zoom range from `MIN/MAX(zoom_level)` rather than the
metadata. Rows beyond roughly ±79° are absent (z=4 has 192 of 256 tiles), so
missing tiles are a normal result: the 2D map lets its clear colour show
through and the globe texture fills gaps with a pale ice tone.

**Tile addressing.** MBTiles rows are TMS (counted from the south); the
renderer addresses tiles XYZ-style, so `MBTilesReader::tile()` flips
`row = 2^z - 1 - y`.

**2D map.** A proper slippy map: the camera is (centre lat, centre lon,
fractional zoom), the visible tile range is computed per frame, and tiles are
uploaded to textures lazily and cached. Drag to pan, wheel to zoom.

**3D globe.** Rather than resampling the imagery to equirectangular, the whole
z=4 level is stitched into one 4096×4096 Mercator texture and the fragment
shader does the Mercator lookup per pixel from the interpolated longitude and
latitude. That avoids a resampling pass and any seam fix-up; the date line
gets two coincident vertex columns (−180 and +180) so nothing interpolates
across it. Drag to rotate, wheel to zoom.

Both widgets expose the same `centerLatitude()/centerLongitude()/zoom()` and a
`setView(..., silent)` slot, which is the hook Task-6 needs.

## Task-6 notes

Each view emits `viewChanged(lat, lon, zoom)` after any mouse manipulation;
`MainWindow` forwards it to the other view via `setView(..., silent = true)`.
The `silent` flag suppresses the receiving view's own signal, which is what
stops the two from bouncing updates back and forth — no re-entrancy guard or
blocked-signal juggling needed.

Latitude, longitude *and* zoom are kept in step. That works because the globe
derives its camera distance from the same fractional zoom value the map uses
for its tile pyramid, so "zoomed in" means the same thing in both.

The globe's centre latitude is clamped to the Mercator cut-off (±85.051129°)
rather than to the pole. The 2D map physically cannot centre beyond that, so
clamping both to the same limit is what lets the two views always agree.

The **Synchronise views** checkbox turns the coupling off so the views can be
driven independently; re-enabling it snaps the globe back onto the map's
camera. The status bar shows the shared centre as it moves.

## Task-7 notes

`map/FlightTrack.*` turns the records into the path both views draw. It always
works from a **time-sorted copy**, not the table's current row order — a
trajectory only means anything in time order, and the table may be in file
order.

Sorted by time the flight runs **Los Angeles → Auckland**:

```
first  10:34:03    33.9354, -118.4232   FlightAware ADS-B (KSNA)
last   22:39:24   -37.0119,  174.7867   FlightAware ADS-B (AKL / NZAA)
```

The intermediate fixes march monotonically between the two, so clock-time
order is genuinely chronological here despite the data carrying no date.

**The antimeridian.** This route sits on the date line, so longitude jumps
between +180 and −180 partway along. `toUnwrappedWorldXY()` steps to the
nearest image of each successive longitude, giving a continuous run of world
x that may leave the 0..1 square; the renderer then draws the strip at x−1, x
and x+1 so whichever world copy is on screen gets the line. Without this the
path would draw a spurious streak back across the entire map at every
crossing.

That also exposed a gap in Task-5's map: tile x was clamped to `0..2^z-1`, so
panning across the date line ran out of tiles and left a dead band. The tile
lookup now wraps (`((x % n) + n) % n`) while the quad stays at its unwrapped
position, so the map repeats east-west the way a slippy map should. Y is still
clamped — there is nothing beyond the poles.

On the globe the track is drawn at radius 1.004, just above the surface, so
the sphere's own depth values hide the half of the path that is around the
back.

Line width is requested as 2px via `glLineWidth`. Core-profile drivers are
free to clamp that to 1px; the line still renders, just thinner.

## Task-8 notes

The shortest path is a **true WGS84 geodesic**, computed with Karney's
algorithm via GeographicLib, not a spherical great circle. On this route the
two differ by about 19 km:

```
WGS84 geodesic          10466.401 km
spherical great circle  10485.637 km
```

The app's figure was checked against an independent Vincenty inverse solution
computed outside the program: both give **10466.401 km**.

`InverseLine()` solves the inverse problem once, then the line is sampled at
257 equal arc-length steps. The interpolation matters: a geodesic is a curve
on a Mercator map, not a straight line, so drawing just the two endpoints
joined would be wrong. On the globe it is also what keeps the line following
the surface instead of cutting a chord through it.

Both overlays share one `LineBuffer` + flat-colour shader path in each view
(`gl/LineBuffer.h`); only colour and vertex data differ. The geodesic draws
first and sits fractionally lower on the globe (radius 1.003 vs the track's
1.004), so where the two run together the actual flight path stays on top.

### Sync test

`FLIGHTVIZ_SYNCTEST=<file>` drives both views with synthesized press/move/
release and wheel events — the real `mousePressEvent`/`mouseMoveEvent`
handlers, not `setView()` called behind their back — and reports whether the
cameras stayed in step:

```
OK   after seeding the map at 0,0 zoom 3
OK   after dragging the 2D map (globe should follow)
OK   after dragging the 3D globe (map should follow)
OK   after wheel-zooming the 2D map
OK   after wheel-zooming the 3D globe
OK   with sync off the views move independently
OK   re-enabling sync snaps the globe back to the map
RESULT: PASS
```

## Capturing screenshots

`FLIGHTVIZ_GRAB=<dir>` makes the app save each GL pane's own framebuffer a few
seconds after startup:

```
set FLIGHTVIZ_GRAB=C:\fvbuild\out
flightviz.exe
```

This writes `pane_map2d.png` and `pane_globe.png`. Prefer it over a desktop
screenshot: it captures exactly what each widget rendered, at full device
resolution, with no window chrome or cropping guesswork.

## Smart App Control

Windows 11's Smart App Control blocks freshly linked, unsigned executables
with *"An Application Control policy has blocked this file."* If `flightviz.exe`
will not start, check:

```
(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy').VerifiedAndReputablePolicyState
```

`0` = off, `1` = enforcement, `2` = evaluation. It was switched off on this
machine so the build could be run and verified. Worth knowing before anyone
repeats that: **Smart App Control cannot be re-enabled without reinstalling
Windows.**

## Toolchain

Installed by this project's setup; nothing else is required.

- CMake 4.4.4
- Ninja
- MinGW-w64 13.1 (`C:\Qt\Tools\mingw1310_64`)
- Qt 6.8.3 for MinGW (`C:\Qt\6.8.3\mingw_64`)

Qt and MinGW were installed with [`aqtinstall`](https://github.com/miurahr/aqtinstall)
(`pip install aqtinstall`), which needs no Qt account:

```
aqt install-tool windows desktop tools_mingw1310 -O C:\Qt
aqt install-qt   windows desktop 6.8.3 win64_mingw -O C:\Qt
```

## Build

Build out of the source tree and keep the build path short — MinGW's `make`
trips over Windows' 260-character path limit, which is why Ninja plus a short
build directory is the supported combination.

```
cmake -S . -B C:\fvbuild -G Ninja ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 ^
      -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe ^
      -DCMAKE_BUILD_TYPE=Release
cmake --build C:\fvbuild
C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe --release C:\fvbuild\flightviz.exe
```

`windeployqt` only needs running once, to copy the Qt DLLs and the
`platforms/qwindows.dll` plugin next to the executable.

## Run

```
C:\fvbuild\flightviz.exe
```

The build stages `NZ5_Flightdata.csv` beside the executable, so the table is
populated at startup; **Load CSV** opens any other file.

### Self-test

Tasks 1–4 also run headlessly, which is what the build check uses:

```
flightviz.exe --selftest C:\fvbuild\NZ5_Flightdata.csv C:\fvbuild\out
```

It writes `out/selftest.txt` and exits non-zero on failure. Current output:

```
[Task-1] OK   470 records, 0 skipped, 183 rows with NULL SpeedKts
[Task-2] OK   first=10:34:03 last=22:39:24
[Task-3] OK   470 features, 223263 bytes
[Task-4] sizeof(OutputRecord)=15
[Task-4] OK   course_lt_220.bin -> 200 records, 3000 bytes
[Task-4] OK   altitude_37900_38000.bin -> 200 records, 3000 bytes
[Task-7] OK   470 track points, 33.9354,-118.4232 -> -37.0119,174.7867
[Task-8] OK   257 interpolated points, WGS84 geodesic 10466.401 km
RESULT: PASS
```

Tasks 5 and 6 need a window, so they are covered by `FLIGHTVIZ_GRAB` and
`FLIGHTVIZ_SYNCTEST` below rather than by `--selftest`.

## Notes on the task wording

**Time (Task-2).** The CSV renders every time as 12-hour with an AM/PM suffix
and carries no date. `CsvLoader` parses with the `h:mm:ss AP` format into a
`QTime`, so the sort is a plain ascending compare on a 24-hour clock value.

**ISO time (Task-3).** With no date in the source, the `time` property is an
ISO 8601 *local time* — `"15:44:57"` — rather than a full timestamp.

**15 bytes (Task-4).** `OutputRecord` is wrapped in `#pragma pack(1)`; without
it the `int32_t` members align to 4 bytes, the struct becomes 16 bytes and each
file comes out 3200 bytes instead of 3000. A `static_assert` pins the size.

**MSB first.** Every multi-byte field is stored big-endian via `qToBigEndian`.
Latitude and longitude are `round(degrees / 8.381903e-8)` — the usual
180/2³¹ semicircle encoding. Verified by decoding the written file: record 0 of
`course_lt_220.bin` reads back as `13:30:54, 20.2333, -139.3500, 497 kt,
36000 ft`, matching CSV row `1:30:54 PM,20.2333,-139.35,214,497,572,36000`.

**200 rows (Task-4).** Read as *filter first, then take the first 200*, because
that is the only reading that yields the required 3000 bytes. The filters match
250 and 221 rows respectively, so both files fill exactly. A NULL `SpeedKts` is
written as `0`.

**`std::async`.** The two pipelines (filter → sort → serialise → write) are
launched with `std::launch::async`, so the sorting and the file writing of the
two outputs overlap on separate threads.
