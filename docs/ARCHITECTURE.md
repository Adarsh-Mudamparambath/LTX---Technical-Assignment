# FlightViz — how it works and why

A walkthrough of the whole project: what it is, how the pieces fit, every
decision that needed judgement, and how each claim was verified.

---

## 1. What this is (and what it is not)

A **Windows desktop application** in C++20 that loads a flight-data CSV,
displays and exports it, and draws the flight on a 2D map and a 3D globe.

It is **not** an XR/VR/AR project. There is no headset, no OpenXR, no stereo
rendering, no head tracking. The "3D globe" is a textured sphere rendered with
ordinary OpenGL onto a flat window — the same 3D maths any desktop map
application uses. The only hardware requirement is a GPU that supports
OpenGL 3.3, which is essentially every machine made since ~2010.

---

## 2. The stack, and why each piece is there

| Layer | Choice | Why |
|---|---|---|
| Language | C++20 | Assignment requires C++; `std::optional` and structured bindings are used |
| Framework | **Qt 6.8.3 LTS** | Assignment names `QTableView`; Qt also supplies the GL context, PNG decoding and SQLite |
| Graphics | **OpenGL 3.3 core profile** | Assignment says "OpenGL windows". Hand-written GLSL, not Qt Quick or Qt 3D |
| Build | **CMake 4.4** + Ninja | Assignment: "Use of CMake … carries extra weight" |
| Compiler | MinGW-w64 GCC 13.1 | No Visual Studio on the machine; ~2 GB toolchain instead of ~7 GB |
| JSON | **nlohmann/json 3.11.3** | GeoJSON output (Task-3) |
| Geodesy | **GeographicLib 2.4** | True WGS84 geodesic (Task-8) |

Both third-party libraries are pulled in by **`FetchContent`**, so there is
nothing to install by hand — `cmake` downloads and builds them.

### Qt modules, and what each one actually does here

- `Qt6::Core` — `QTime` parsing, file I/O, `qToBigEndian` for the binary records
- `Qt6::Gui` — `QImage` decodes the MBTiles PNG tiles (the assignment's
  "appropriate image loading library")
- `Qt6::Widgets` — `QTableView`, buttons, splitter, file dialogs
- `Qt6::OpenGL` / `Qt6::OpenGLWidgets` — `QOpenGLWidget`, shaders, textures
- `Qt6::Sql` — SQLite driver; an `.mbtiles` file *is* a SQLite database

Two of those are why the project needs no extra dependencies for things you
might expect to: **Qt reads the tile database and decodes the tile images.**
No separate SQLite, no stb_image.

---

## 3. Source layout

```
CMakeLists.txt                 one target, Qt + two fetched libraries
cmake/Dependencies.cmake       FetchContent for nlohmann/json, GeographicLib

src/model/FlightRecord.h       the row struct (std::optional members)
src/model/CsvLoader.*          RFC 4180 parser + 12-hour time parsing
src/model/FlightTableModel.*   QAbstractTableModel behind the QTableView

src/io/GeoJsonWriter.*         Task-3
src/io/BinaryWriter.*          Task-4

src/map/Mercator.h             Web Mercator <-> latitude/longitude
src/map/MBTilesReader.*        SQLite tile archive reader
src/map/FlightTrack.*          Task-7: time ordering + date-line unwrapping
src/map/GeodesicPath.*         Task-8: WGS84 geodesic

src/gl/LineBuffer.h            shared polyline VAO/VBO
src/gl/Map2DWidget.*           2D slippy map
src/gl/Globe3DWidget.*         3D textured globe

src/ui/MainWindow.*            buttons, table, the two GL panes, Task-6 wiring
src/main.cpp                   entry point, GL format, --selftest
```

The split is deliberate: `model/` and `io/` contain **no GL and no UI**, so
Tasks 1–4 are testable headlessly. That is what makes `--selftest` possible.

---

## 4. End-to-end data flow

```
NZ5_Flightdata.csv
      |
      v
 CsvLoader::load()                 -> std::vector<FlightRecord>
      |                               (std::optional for blank cells)
      +--> FlightTableModel        -> QTableView                  [Task-1,2]
      |
      +--> GeoJsonWriter           -> flight.geojson              [Task-3]
      |
      +--> BinaryWriter            -> 2 x 3000-byte .bin          [Task-4]
      |      (two std::async pipelines)
      |
      +--> track::fromRecords()    -> std::vector<GeoPoint>
      |         |
      |         +--> Map2DWidget::setTrack()     yellow line      [Task-7]
      |         +--> Globe3DWidget::setTrack()   yellow line
      |
      +--> geodesic::between(first, last)
                |
                +--> setGeodesic()               red line         [Task-8]

World_10M.mbtiles
      |
      v
 MBTilesReader (Qt SQLite)
      +--> Map2DWidget   per-tile textures, lazily              [Task-5]
      +--> Globe3DWidget one stitched 4096x4096 Mercator texture

 Map2DWidget <--- viewChanged / setView ---> Globe3DWidget       [Task-6]
```

---

## 5. Task by task

### Task-0 — get the CSV

The published Zoho sheet offers no CSV export and blocks downloads, so the 470
rows were read out of the page's own data cache and written to
`data/NZ5_Flightdata.csv`. The result was **verified by SHA-256 against the
live sheet** — all 470 data rows are byte-exact.

### Task-1 — CSV into a vector of structs, shown in a QTableView

`FlightRecord` mirrors the eight CSV columns. The four numeric columns that can
be blank are `std::optional<int>`:

```cpp
QTime              time;
double             latitude, longitude;
std::optional<int> course, speedKts, speedMph, altitudeFeet;
QString            reportingFacility;
```

183 of the 470 rows have a blank `SpeedKts`. In the table these render as a
grey em-dash, **not** as `0` — the distinction between "no reading" and "zero
knots" is exactly what `std::optional` is for, and it survives all the way to
the GeoJSON, where it becomes JSON `null`.

`CsvLoader::splitLine()` is a proper RFC 4180 parser (handles quoted fields and
doubled quotes), not a naive `split(',')`.

### Task-2 — sort by Time

The source renders every time as 12-hour with an AM/PM suffix. `QTime` is
parsed with the `h:mm:ss AP` format, so sorting is a plain ascending compare on
a 24-hour clock value. `stable_sort` keeps equal times in file order.

### Task-3 — GeoJSON with ISO times

Each record becomes a `Point` feature; a `LineString` of the whole trajectory
is appended so the file drops straight into any GeoJSON viewer.

The data carries **no date**, so the `time` property is an ISO 8601 *local
time* — `"15:44:57"` — rather than a full timestamp. Stating that assumption is
better than inventing a date.

The export sorts internally, so the output is time-ordered regardless of
whether the user pressed **Sort** first.

### Task-4 — two filtered binary files

The `outputstruct` sheet specifies 15 bytes, MSB first:

```
uint8_t  hour, minute, sec
int32_t  latitude     MSB first,  value / 8.381903e-8
int32_t  longitude    MSB first,  value / 8.381903e-8
uint16_t speedKts     MSB first
uint16_t altitudeFeet MSB first
```

Three things matter here:

1. **`#pragma pack(1)` is load-bearing.** With default alignment the `int32_t`
   members pad to a 4-byte boundary, the struct becomes 16 bytes, and each file
   comes out 3200 bytes instead of 3000. A `static_assert` pins the size.
2. **Big-endian.** Every multi-byte field goes through `qToBigEndian`. x86 is
   little-endian, so without this the bytes come out reversed.
3. **The scale factor `8.381903e-8` is 180/2³¹** — the standard "semicircle"
   encoding, which maps ±180° onto the full range of a signed 32-bit integer.

**Interpretation call:** "Dump the data (first 200 rows)" was read as *filter
first, then take 200*. That is the only reading consistent with the stated
3000-byte requirement. The filters match 250 and 221 rows respectively, so both
files fill exactly.

`std::async(std::launch::async, …)` runs the two pipelines
(filter → sort → serialise → write) concurrently on separate threads.

### Task-5 — 2D map and 3D globe from MBTiles

`World_10M.mbtiles` is a SQLite database: 256×256 PNG tiles, Web Mercator,
zoom 1–6, `scheme=tms`. Three things had to be handled:

- It declares `minzoom=0` but holds **nothing at z=0**, so the reader takes its
  range from `MIN/MAX(zoom_level)` rather than trusting the metadata.
- Rows beyond roughly ±79° are **absent** (z=4 has 192 of 256 tiles), so a
  missing tile is a normal result, not an error.
- MBTiles rows are **TMS** (counted from the south); the renderer addresses
  tiles **XYZ**-style (from the north). `MBTilesReader::tile()` flips
  `row = 2^z - 1 - y`. Getting this wrong flips the world upside down.

**2D map** — a real slippy map. The camera is (centre lat, centre lon,
fractional zoom); the visible tile range is computed per frame and tiles are
uploaded to textures lazily and cached.

**3D globe** — rather than resampling the imagery to equirectangular, the whole
z=4 level is stitched into one 4096×4096 Mercator texture and **the fragment
shader does the Mercator lookup per pixel**:

```glsl
float u    = (vGeo.x + 180.0) / 360.0;
float latR = radians(clamp(vGeo.y, -85.05112878, 85.05112878));
float v    = 0.5 + log(tan(PI/4.0 + latR/2.0)) / (2.0*PI);
```

That avoids a resampling pass entirely. The date line gets two coincident
vertex columns (−180 and +180) so nothing interpolates across the seam.

**On the word "windows":** Task-5 says "Create 2D and 3D OpenGL windows", which
could be read as two top-level windows. The mock-up on the Instructions sheet
settles it — it draws a single outer window with the four buttons across the
top and three panes side by side, labelled **Table View | 2D Map | 3D Globe**,
with "Synchronized Views" arrows between the last two. That is exactly the
layout implemented here, down to the button order. "Windows" means viewports.

### Task-6 — synchronised views

Each view emits `viewChanged(lat, lon, zoom)` after any mouse manipulation;
`MainWindow` forwards it to the other via `setView(..., silent = true)`. The
`silent` flag suppresses the receiving view's own signal — **that is what
breaks the feedback loop.** No re-entrancy guard, no `blockSignals()`.

Latitude, longitude *and* zoom stay in step, because the globe derives its
camera distance from the same fractional zoom the map uses for its tile
pyramid.

The globe's latitude is clamped to the **Mercator cut-off (±85.051129°)**, not
to the pole. The 2D map cannot centre beyond that, so clamping both to the same
limit is what makes agreement guaranteed rather than approximate.

### Task-7 — yellow trajectory

Built from a **time-sorted copy**, never the table's current row order — a
trajectory only means anything chronologically.

**The hard part is the antimeridian.** This route crosses the date line, so
longitude jumps between +180 and −180 partway along. Drawn naively, every
crossing streaks a line back across the entire map.
`toUnwrappedWorldXY()` steps to the *nearest image* of each successive
longitude, producing a continuous run of world-x that may leave the 0..1
square; the renderer then draws the strip at x−1, x and x+1 so whichever world
copy is on screen gets the line.

This also exposed a bug in Task-5: tile x was clamped to `0..2^z-1`, so panning
across the date line ran out of tiles and left a dead band. The lookup now
wraps with `((x % n) + n) % n` while the quad stays at its unwrapped position.

On the globe the track sits at radius 1.004, just above the surface, so the
sphere's own depth values hide the half of the path around the back.

### Task-8 — red geodesic

A **true WGS84 geodesic** (Karney's algorithm via GeographicLib), not a
spherical great circle. On this route the two differ by about 19 km:

```
WGS84 geodesic          10466.401 km
spherical great circle  10485.637 km
```

`InverseLine()` solves the inverse problem once, then samples 257 points at
equal arc-length steps. **The interpolation matters:** a geodesic is a *curve*
on a Mercator map, not a straight line, so joining the two endpoints directly
would be visibly wrong. On the globe the same sampling keeps the line on the
surface instead of cutting a chord through it.

---

## 6. The flight itself

Sorted by time, the data runs **Los Angeles → Auckland**:

```
first  10:34:03    33.9354, -118.4232   FlightAware ADS-B (KSNA)
last   22:39:24   -37.0119,  174.7867   FlightAware ADS-B (AKL / NZAA)
```

The intermediate fixes march monotonically between the two, so clock-time order
really is chronological here despite the data carrying no date. This matters
because Task-8's geodesic is defined by the first and last point.

---

## 7. How every claim was verified

Nothing here rests on "it looked right".

| Claim | How it was checked |
|---|---|
| CSV is correct | SHA-256 of all 470 rows vs. the live sheet |
| Binary encoding | Decoded record 0 back: `13:30:54, 20.2333, -139.3500, 497 kt, 36000 ft` — matches CSV row `1:30:54 PM,20.2333,-139.35,214,497,572,36000` |
| Files are 3000 bytes | `git cat-file -s` on the committed blobs |
| Geodesic distance | Independent Vincenty solution computed outside the program: both give **10466.401 km** |
| Map/globe rendering | `FLIGHTVIZ_GRAB` saves each pane's own framebuffer — no screenshot cropping guesswork |
| View sync | `FLIGHTVIZ_SYNCTEST` drives both views with **synthesized mouse events** through the real event handlers, and checks agreement to 1e-9 |

Three verification hooks are built into the binary:

```bash
flightviz.exe --selftest <csv> <outdir>    # Tasks 1-4, 7, 8, headless
set FLIGHTVIZ_GRAB=<dir>                   # saves each GL pane's framebuffer
set FLIGHTVIZ_SYNCTEST=<file>              # exercises Task-6, pass/fail report
```

The sync test checks the **negative** case too: with synchronisation off the
views must genuinely diverge, so the test cannot pass by accident through some
shared-state coincidence.

---

## 8. Known limitations

1. **Line width** is requested as 2px via `glLineWidth`; core-profile drivers
   are free to clamp that to 1px. Proper thick lines would mean expanding the
   strips into triangles.
2. **`World_10M.mbtiles` is not in the repository** (~90 MB; GitHub warns above
   50 MB). It ships with the assignment. CMake warns if it is absent and the
   app still runs, with empty map and globe panes.
3. **Build directory must be outside the source tree** — MinGW's `make` hits
   Windows' 260-character path limit. Ninja plus a short build path is the
   supported combination.

---

## 9. Glossary — the domain concepts

Worth being able to explain these, since they are where the real decisions were.

**Web Mercator** — the projection every web map uses. Latitude is warped so
that the map is conformal (angles preserved), which makes the world a square.
It cannot represent the poles, so it is cut off at ±85.051129° — the latitude
where the square closes.

**MBTiles** — a SQLite database holding map tiles as image blobs, one row per
(zoom, column, row). A single-file alternative to a directory of PNGs.

**TMS vs XYZ** — two conventions for numbering tile rows. TMS counts from the
south, XYZ from the north. Mixing them flips the map vertically.

**Slippy map** — the standard pan/zoom map model: the world at zoom *z* is a
grid of 2^z × 2^z tiles of 256×256 pixels.

**Semicircle encoding** — storing an angle as `degrees / (180/2³¹)`, so ±180°
uses the full range of a signed 32-bit integer. The sheet's `8.381903e-8` is
that constant.

**Big-endian / MSB first** — most significant byte stored first. Network and
file formats commonly use it; x86 CPUs are little-endian, so a conversion is
required.

**Geodesic vs great circle** — the shortest path between two points. On a
*sphere* it is a great-circle arc. On the WGS84 *ellipsoid* (which is flattened
at the poles) it is a geodesic, and the two differ — by about 19 km on this
route.

**Antimeridian / date line** — longitude ±180°, where the coordinate wraps.
Any path crossing it needs explicit handling or it draws backwards across the
whole map.
