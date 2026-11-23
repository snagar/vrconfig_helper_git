# vrconfig_helper

X-Plane plugin for VR, drawn with [Dear ImGui](https://github.com/ocornut/imgui)
through [ImgWindow](https://github.com/slgoldberg/ImgWindow).

One fixed-size window (520 x 730) whose content changes with the page:
**Home** offers the actions **Hotspot** (edits the aircraft's teleport hotspots),
**Extract manipulators to vrconfig** (adds the cockpit OBJ manipulators to the
vrconfig file with default VR settings) and **VRConfig Manipulator Updater**
(changes or converts each manipulator block, for any aircraft). Questions and
forms open as popups on top of the page. **About** (top right) shows the version, a
drawn logo (a VR headset whose right lens is a knob, over a teleport ring), and
"Designed by Snagar.Dev, Coded by Claude.Ai, License: Free" and "Inspired by the
"VR Tools" plugin, by jrKok", the Font Awesome icon credit and the Dear ImGui / ImgWindow credit (see Credits). At the bottom of the Home page, in black, a note asks developers
who publish an aircraft made with the tool to credit it.

## Using it in X-Plane

* Menu: **Plugins > VRConfig Helper > Toggle Window** (shows or hides the window)
* Pages: **Home** -> **Hotspot** / **Extract manipulators to vrconfig** /
  **VRConfig Manipulator Updater**; the **Home** button (top left) goes back.
* The pages write vrconfig files: while one page has unsaved changes, Home
  disables the other pages' buttons and says why (save or cancel first).
* Signature: every Save adds one comment line right after the `A` / `1100` /
  `VRCONFIG` header - `# Made with VRConfig Helper - by Snagar.Dev` for a file the
  plugin created, `# Modified with VRConfig Helper - by Snagar.Dev` for an existing
  one. Added once only (an existing signature line is kept as it is), never to a
  file without a valid header (`VrConfigFile::AddSignature`, called by `Save`).
* Command: `vrconfig_helper/toggle_main_window` - can be bound to a key, joystick or
  VR controller button.
* Opened while in VR, the window appears in VR; it follows X-Plane when
  entering/leaving VR.

## Layout

```
CMakeLists.txt
src/
  vrconfig_helper.cpp     plugin entry points, command, menu, VR handling, aircraft-loaded reset
  SharedInfo.h       the plugin's version, name, description, About texts, log limits,
                     window size (hpvr::info) - the one place to change the version
  PluginWindow.h/.cpp the only window (ImgWindow subclass): pages Home / Hotspot / Manip / Updater
  Fonts.h/.cpp       shared font atlas: fonts::regular (+ icons), fonts::fontawsome
  Log.h/.cpp         logging (loguru back-end, LOG_* macros)
  vrconfig_helper.sym     exported symbols (XPlugin* only)
  ui/                hpvr_colors.hpp (named colours), Theme.h (colour roles),
                     Widgets (ui_help_marker, ui_hold_button, ui_popup_yes_no, ...)
  vrconfig/          VrConfigFile: reads / writes <aircraft>_vrconfig.txt, line-preserving
  hotspot/           SpotEditor (edit session, add/delete, save, backup, restore), SpotPage (the page)
  manip/             ManipTypes (manip_types.ini), ObjManips (finds / reads OBJ files),
                     ObjScanner (background thread: .acf object list + OBJ files, progress, abort), ManipEditor (edit session,
                     ticks, save, .extract.bak backup), ManipPage (the page);
                     Updater page: ManipActions (manip_actions.ini), ManipBlock (one block: read,
                     apply settings, check typed text), ManipUpdater (edit session, .updater.bak),
                     AircraftFinder (.acf files, background search), UpdaterPage (the page)
  util/              FileUtil: ReadAll, CopyFileSafely, WriteFileSafely (temporary file + rename),
                     Utf8 / PathFromUtf8 (paths as UTF-8 on every system);
                     StopThread: background thread that can be asked to stop
  xp/                Aircraft: user aircraft path, pilot head position (X-Plane datarefs)
libs/
  manip_types.ini    manipulator types (shipped next to the plugin)
  manip_actions.ini  ACTION conversions and block settings (shipped next to the plugin)
  XPSDK.zip          X-Plane SDK (unpacked by CMake into the build folder), or
  xpsdk/             the SDK already unpacked (used first when present)
  imgui/             Dear ImGui v1.92.8     (kept in the repo - not fetched)
  ImgWindow/         ImgWindow v2.4.1       (kept in the repo - not fetched)
  loguru/            loguru 2.2 (locally patched preamble - kept in the repo)
  fonts/             IconsFontAwesome5.h, fa-solid-900.inc/.ttf (kept in the repo)
```

## Build

The same commands on Linux, Windows and macOS:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
cmake --install build --prefix "<X-Plane>/Resources/plugins"
```

Result, in the X-Plane plugin layout:
`build/vrconfig_helper/<lin_x64|win_x64|mac_x64>/vrconfig_helper.xpl` plus the two
`.ini` rule files in `build/vrconfig_helper/`. On Linux each build is also copied into
the X-Plane folders listed in `VRCH_DEPLOY_XP_ROOTS` (turn off with `-DVRCH_DEPLOY=OFF`).

Requirements:

* **All:** CMake 3.20+, a C++20 compiler with `std::format`.
* **Linux:** GCC 13+ or Clang 17+, OpenGL headers (`sudo apt install libgl-dev`).
  ImGui, ImgWindow, loguru, libstdc++ and libgcc are linked statically; the plugin
  needs only `libGL.so.1` and glibc (the version of the system it was built on, or newer).
* **Windows:** Visual Studio 2022 (MSVC), 64-bit (`-G "Visual Studio 17 2022" -A x64`).
  The C++ runtime is linked into the plugin (`/MT`).
* **macOS:** Xcode 15+. One file for Apple Silicon and Intel (`x86_64;arm64`),
  macOS 13.3 or newer (`std::format`).

**X-Plane SDK:** CMake uses, first found: `-DXPSDK_DIR=<folder>`, `libs/xpsdk/`, or
`libs/XPSDK.zip` (unpacked into `<build>/xpsdk_unpacked/`; the folder name inside the
zip does not matter). The SDK zip must contain `Libraries/Win/XPLM_64.lib` and
`Libraries/Mac/XPLM.framework`. Only the X-Plane 11 API levels are enabled
(`XPLM200`-`XPLM303`), so the plugin loads in X-Plane 11.50+ and 12.

## Releases (GitHub Actions)

`.github/workflows/release.yml` builds Linux (inside Ubuntu 22.04, GCC 13), Windows
(MSVC) and macOS (Universal 2, signed ad hoc), each as Release and Debug:

* **Every push / pull request:** build only; the zips are kept as workflow artifacts.
* **A tag:** also a GitHub Release with `vrconfig_helper-<tag>.zip` (the plugin
  folder for all three systems, LICENSE, the text user guide) and
  `vrconfig_helper-<tag>-debug.zip`. `v1.0.0` / `…-release` = release;
  `b…` / `…-beta…` and `t…` / `…-test…` = pre-release; other tags = build only.
  The release text is `notes.md` when it exists.

**macOS users:** the plugin is not notarized by Apple. If macOS blocks it, run once
in Terminal: `xattr -cr "<X-Plane>/Resources/plugins/vrconfig_helper"`.

## Rendering

ImgWindow is built with `IMGWINDOW_USE_PANEL_GRAPHICS` and **without** `XPLM440`:
on X-Plane 12.4.4+ it draws through Panel Graphics (Vulkan/Metal), on older
versions it falls back to OpenGL. One binary covers X-Plane 11.10+ and 12.
Do not add `XPLM440` to the build unless you want to drop older versions.

## Hotspot screen

Opened by the **Hotspot** button. Reads `<aircraft>_vrconfig.txt` (next to the
`.acf`) and lists every `BEGIN_TELEPORT_HOTSPOT` block in a drop-down.

* **Use my current head position** stores the pilot head position (X, Y, Z) and
  heading exactly as they are (`sim/graphics/view/pilots_head_*`); pitch and roll
  are set to 0 (neutral). Disabled while teleported outside
  (`sim/graphics/VR/teleport_on_ground`).
* **Go to hotspot** (purple) moves the camera to the selected hotspot's position and
  heading ("focus to the hotspot location"), also when Live view is off; the
  hotspot itself is not changed. Only inside the cockpit and not while teleported
  outside.
* **SITTING / STANDING** is metadata: written to the file, it never changes the
  stored position.
* Arrow buttons move the hotspot: tap = 1 cm / 1 degree, hold = keeps moving,
  faster the longer you hold. **Forward / Back / Left / Right follow the direction
  the camera looks** (`pilots_head_psi`; outside the cockpit or while teleported
  outside: the hotspot's heading). **Up / Down** are vertical. The teleport box
  (`AABB`) always moves with the hotspot.
* Heading directions, relative to the aircraft: **N** = nose (0), **E** = right
  wing (90), **S** = tail (180), **W** = left wing (270). The direction the hotspot
  faces is highlighted (white frame).
* **Live view (WYSIWYG)** (on by default, remembered until X-Plane quits): every
  move, turn, direction button and Revert also puts the camera on the hotspot
  (writes `sim/graphics/view/pilots_head_x/y/z/psi`, and `pilots_head_the/phi` for
  the hotspot's pitch and roll).
  Only inside the cockpit (`sim/graphics/view/view_is_external` = 0) and not while
  teleported outside; otherwise the checkbox shows "Live view (paused)".
* **Rename** changes the name (rest of the `BEGIN_TELEPORT_HOTSPOT` line); an
  empty name becomes the next free "Hotspot N".
* **Add** opens a popup: name (empty = next free "Hotspot N"), SITTING / STANDING,
  and the start position - the current head position or a copy of the selected
  hotspot (position and heading); pitch and roll start at 0. The teleport box takes
  the shape of an existing hotspot (same type first; default: a 0.6 x 0.6 m area
  0.45..0.75 m below the position).
  New hotspots are appended after the last teleport hotspot of the file.
* **Delete** marks the hotspot (✕, "will be deleted"); it is removed from the file
  on Save. **Undo delete** / **Cancel changes** bring it back. A new, unsaved
  hotspot is removed at once.
* **Refresh** reads the file again (asks first when there are unsaved changes).
* An aircraft without a vrconfig file gets **Create the file and add a hotspot**;
  the file (header `A` / `1100` / `VRCONFIG`) is written on Save, without a backup.
* Values can also be typed in the *New* column. **Pitch / Roll** share one row
  (always shown, so the layout does not move).
* **Adv** (purple, next to *Revert this hotspot*) opens the *Advanced* popup:
  * **Pitch and roll** fine tuning (`PRESET_THE` nose up/down, -90..90;
    `PRESET_PHI` sideways, -180..180): tap = 1 degree, hold = keeps turning,
    **0** resets. With Live view on, the camera follows.
  * **Copy a value to other hotspots**: pick X, Y, Z, Heading, Pitch or Roll; the
    value starts as the selected hotspot's value and can be typed (↺ takes it
    back). Tick the target hotspots (**All** / **None**); each line shows
    current → new. **Apply** makes it an unsaved change of each ticked hotspot
    (X/Y/Z move the teleport box too). Deleted or unreadable hotspots cannot be
    ticked.
  * Unsaved changes show in the popup too: an orange ● after the hotspot name and
    after changed pitch / roll, orange values and ● in the target list, and the
    unsaved-changes count next to **Close**.
* Colours: orange = unsaved change, green = saved in this X-Plane run
  (always with a symbol: ● / ✓).
* **Commas between the numbers** (`AABB 0.39, 0.64, 0.58`, written by some tools)
  make a hotspot read-only. It can still be picked: **Remove the commas** (orange)
  makes it editable at once; the commas are removed from its lines on Save (an
  unsaved change like any other; only those lines change, with single spaces).
* **File notes**: an orange ⚠ *N* button next to **Explain** lists remarks about the
  file - misspelled or unknown keywords (e.g. `EGIN_TELEPORT_HOTSPOT`: neighbouring
  stray lines are one note), hotspot / manipulator lines outside a block,
  `END_...` without `BEGIN_...`, hotspot names used more than once.

Saving rules (also in the **Explain** popup):

* Changes stay in memory until **Save** or **Cancel changes**, also when the
  window is closed. Loading or reloading an aircraft discards them.
* **Save** writes all changes; only the changed lines of the file are rewritten
  (comments, layout, manipulator blocks stay as they are), via a temporary file +
  rename. If the file was changed by another program since it was read, you are
  asked before it is overwritten.
* **Backup:** the first Save of an aircraft in an X-Plane run copies the file to
  `<aircraft>_vrconfig.txt.hotspot.bak`; later saves in that run keep it. The first save
  in the next run replaces it. **Restore backup** puts it back (repeatable).
* **Last save:** every Save, on any screen, also copies the saved file to
  `<aircraft>_vrconfig.txt.last_save` (`VrConfigFile::Save`), replaced each time. An
  aircraft update that replaces the vrconfig file leaves it alone. Backups made
  by v1.0 were named `.bak`, `.manip.bak`, `.manip.upd.bak`; **Restore backup**
  still finds them (`RestorePath()`).
* **In VR** (`sim/graphics/VR/enabled`): the headset sets the camera. An aqua note takes
  the heading row's place (same height, the page does not move): the move buttons now
  run X-Plane's view commands while held (`sim/general/forward`, `backward`, `left`,
  `right`, `up`, `down`, as the arrow keys and `,` / `.`), so they move *you*, not the
  hotspot; **Use my current head position** stores where you are. Turning, Go to
  hotspot and Live view are off. The note warns that VR positions may not match 2D
  (its (?) suggests defining hotspots in 2D); the Adv popup notes that pitch and roll
  change the values but not your head in real time.
* Files that use `REF_POINT_ACF` (X-Plane 12): the head position is converted with
  `sim/aircraft/weight/acf_cgY/Z_original` (feet). Not yet verified in X-Plane.

## Extract manipulators screen

Opened by **Extract manipulators to vrconfig**. Step 1 of the VR setup: every ticked
manipulator gets a `BEGIN_MANIP` block with default settings; **VRConfig Manipulator
Updater** then customises each one.

The page has two numbered steps; step 2 is greyed out until step 1 is done
(the step badge is blue for "do this now", a green check when done):

1. **Read the aircraft files.** The active `.acf` lists the objects the aircraft
   uses (`P _obja/<n>/_v10_att_file_stl <path>`, relative to its `objects/`
   folder), so other variants in the same folder (e.g. `Cessna_172SP_G1000.acf`)
   are never read. `<acf name>_cockpit.obj` is added when the `.acf` does not
   list it (X-Plane 10/11 aircraft find their cockpit object by that name).
   **Read aircraft files** reads them in a worker thread (progress bar + red
   **Abort**), then the file list opens: a tick per file, its folder in grey, how
   many manipulators it has; files without any (or missing) are greyed out.
   **Use selected files** confirms; **Change files** / **Read again** later.
   Asked again every time an aircraft is loaded. The worker only reads files (no
   X-Plane, ImGui or log calls); the page logs its results; leaving the plugin or
   loading an aircraft stops and joins it.
2. **Pick the manipulators to extract**: types, **Extract to vrconfig** and the
   result (below). Extract reads the chosen files again first (same progress bar)
   when they or `manip_types.ini` changed; the choice of files is kept.

* **Manipulator types** come from `manip_types.ini` (plugin folder), read again
  for every read (parsed only when its hash changed). Shown by category in a
  small scrolling area, with the number found, e.g. "cmd (28)"; types not in
  these files are greyed out. Types found in the files that the ini neither
  defines nor ignores are listed in orange ("Not in manip_types.ini: ...").
  Click a category name to tick / clear its row.
* **The count next to a type is a button**: it opens the list of that type's
  manipulators in the chosen files (name, OBJ file), each with a tick - all
  ticked at first, **All** / **None**, **OK** / **Cancel**. Unticked ones are left
  out of Extract (also when extracting again) until the aircraft is loaded; the
  button then shows "picked/found" in orange, e.g. "26/28".
* **`manip_types.ini`** - see *Rule files* below. Comments start with `;` (`#`
  lines are vrconfig template lines):

  ```ini
  [file]
  version  = 1                       ; checked by the plugin

  [defaults]
  deg_per_action = 5                 ; {deg} when no rule matches
  deg_rule = selector : 30           ; words of the tooltip / command name, first match wins
  deg_rule = alt altitude* : 10      ; '*' = starts with

  [command_knob]                     ; OBJ type without "ATTR_manip_"
  category = knob                    ; knob | switch | lever | yoke
  label    = cmd                     ; {up_down} / {left_right} are drawn as arrows
  params   = 3                       ; parameters before the tooltip (cursor = 1)
  refs     = 2 3                     ; the commands / datarefs among them (their number =
                                     ; how many a BEGIN_MANIP line of this type has)
  refs_are = commands                ; commands | datarefs (default datarefs)
  checked  = yes                     ; ticked when the page opens
  extract  = yes                     ; no: known to the Updater page, not offered here
  line     = DEG_PER_ACTION {deg}    ; block content; {deg} {name} {file}
  help     = Knob that runs ...      ; the type's (?) help text; several lines = paragraphs

  [ignore]
  types = device keyframe none noop wheel
  ```

  The shipped file has 14 extracted types (`drag_axis_pix` included: 7 parameters,
  the dataref is the 7th) and 6 button-like types with `extract = no` (`command`,
  `push`, `toggle`, `radio`, `delta`, `wrap`), each with a help text. A broken
  entry is skipped and reported on the page (red) and in the log.
* **Groups:** new blocks are written at the end of the file sorted by group - the
  path of their first command / dataref without its last part
  (`sim/autopilot/altitude_up` -> `sim/autopilot`) - with one comment before each
  group and the usual comment above each block:

  ```
  # --- sim/autopilot ---
  # ALT - cockpit.obj, extracted by VRConfig Helper
  BEGIN_MANIP command_knob sim/autopilot/altitude_up sim/autopilot/altitude_down
  ```

  Blocks without a path come first, without a group comment. Blocks already in
  the file stay where they are; a later Extract starts its own groups at the end.
* "The same manipulator" = same type and same commands / datarefs (a `CMND=` /
  `DREF=` prefix in the file is ignored). Several click areas of one control are
  merged.
* **Extract to vrconfig** fills the **Result**, grouped by type (fold arrow,
  group checkbox, count). Each row's checkbox = written on Save:
  * new manipulators start ticked (*added*, appended at the end of the file with
    a comment line: name, OBJ file);
  * ones already in the file start unticked (*exists*); ticking one replaces its
    block in place (*overridden*, the comment above it stays). **Override
    existing** ticks all of them;
  * **All** / **None** tick or untick everything; an unticked new one is
    *skipped* and stays unticked when you extract again (until the aircraft is
    loaded); ticked rows of an earlier extraction stay when other types are
    extracted;
  * saved rows turn green (*✓ saved*).
* Saving works like the Hotspot page: nothing written before **Save**;
  **Cancel changes** unticks everything; the first Save in an X-Plane run backs
  the file up to `<aircraft>_vrconfig.txt.extract.bak` (separate from the hotspot
  `.hotspot.bak`); **Restore backup**; aircraft load / reload discards unsaved changes.
  An aircraft without a vrconfig file gets one (header `A` / `1100` / `VRCONFIG`)
  on Save.
* Not yet handled here: two different controls with the same type and commands
  (e.g. pilot and copilot yokes) are one entry. The Updater page tells them apart
  by their tooltip.

## VRConfig Manipulator Updater screen

Opened by **VRConfig Manipulator Updater**. Changes the manipulator blocks
(`BEGIN_MANIP ... END_MANIP`) of a vrconfig file: their VR settings, or a
conversion to another type with `ACTION`. Hotspots are not read or shown here.
Two numbered steps; step 2 is greyed out until step 1 is done.

1. **Choose the aircraft.** The drop-down lists the `.acf` files of the loaded
   aircraft's folder first (blue), the loaded one at the top marked *(loaded)*.
   **List all aircraft** adds every `.acf` under `<X-Plane>/Aircraft`, found in a
   background thread (folder count, red **Abort**; `liveries`, `objects`, `plugins`
   ... folders are skipped); aircraft of other folders are red. Picking one shows
   a red warning that fades in for 1 s and out for 1 s (X-Plane reads that file
   only when that aircraft loads); another variant of the loaded folder shows a
   grey note instead. While the chosen file has unsaved updates the drop-down is
   locked (save or cancel first).
2. **Read vrconfig and pick the manipulators.** **Read vrconfig** lists the blocks
   of `<acf name>_vrconfig.txt`; the aircraft's OBJ files (from the `.acf`, like the
   Extract page) are read in the background (progress + **Abort**). Then:
   * a tick per block type with its count and a (?) help marker (the type's
     `help` from `manip_types.ini`), all ticked; **All types** / **None**;
   * **Find**: part of a name, command, dataref or tooltip;
   * **Group** drop-down (next to Find): *All groups* or one group - the path of a
     block's first command / dataref without its last part (`sim/autopilot`,
     `laminar/A333/autopilot`, `AirbusFBW`), with its block count;
   * the list: the block's name is a button (orange ● = unsaved update, ✕ = to be
     removed, green ✓ = saved in this run), its type (`→ axis` when an `ACTION`
     converts it) and its most important tag: *⚠ duplicate* (same type,
     references and tooltip as an earlier block - X-Plane can't tell them apart),
     *⚠ not in OBJ* (no such manipulator in the OBJ files - X-Plane may show an
     error for it when the aircraft loads), *⚠ no END_MANIP* (cannot be edited),
     then *hold/spring* (`HOLD_MANIP`), *default* (no settings), *hand edited*, *CMND=*;
   * an orange ⚠ *N notes* button lists manipulator lines outside any block (e.g.
     a stray `YOKE_MANIP_TRANSLATE`).

**Update manipulator** popup (a block's name):

* **Before (saved)** and **After (written on Save)**, read-only, each with
  **Copy** (the clipboard is the plugin's own: copy and paste inside the plugin).
* **Change**: **ACTION** - *none* or the conversions `manip_actions.ini` offers for
  this type: ✓ = Laminar's own aircraft convert this type that way, orange ⚠ = not
  confirmed; *(X-Plane 12)* when it needs it. Its values are filled in from the
  OBJ manipulator when the OBJ files were read (e.g. `axis_knob` -> `ACTION
  axis_knob <min> <max> <step> <dataref>` from the OBJ's own min / max / click),
  else from the block's commands / datarefs and the defaults. A `step` has a
  **Reverse** tick (flips its sign: the knob works the other way round). Then the
  settings meant for the resulting type, each a tick with its value(s) and a help
  marker that explains every value: Degrees per action, Switch threshold,
  **Hold / spring-loaded (HOLD_MANIP)**, Wraps around, Reversed, No VR feedback
  graphics; for `drag_xy` a **Realistic yoke** choice (none / rotating column or
  stick / sliding yoke / side stick) with its 16 / 14 / 4 values. Lines the plugin
  does not know (comments, other directives) are kept as they are.
* Changes keep the file as it is: an unchanged line is written back exactly as
  read, a changed value rewrites only its line, a removed setting drops its line,
  a new `ACTION` goes first and other new settings last, with the block's own
  indent (a tab when it has none).
* **Edit the After text myself (advanced)** - opt-in, on the user's responsibility:
  the After box becomes free text (**Paste** too). It is checked as you type:
  errors block **Apply** (not exactly one block from `BEGIN_MANIP` to `END_MANIP`,
  something nested, no type); warnings allow **Apply anyway** (unknown type,
  directive or ACTION, wrong number of values, text where a number belongs,
  commas, a setting twice, two yoke modes, a setting not meant for the type,
  another manipulator than before, not in the OBJ files). The block then stays
  text (*hand edited*): opening it again goes straight to the text. Switching the
  tick off asks before your text is thrown away. A block the Change area cannot
  show (a directive twice) opens as text.
* **Apply** keeps the change in memory, **Remove** marks the block for removal
  (**Keep the block** undoes it), **Revert** undoes this block's change, **Close**.

Saving: nothing is written before **Save**; **Cancel changes** undoes every
update (after a question listing them). The first Save of the file in an X-Plane
run copies it to `<aircraft>_vrconfig.txt.updater.bak` (apart from the hotspot
and Extract backups; the first save in a later run replaces it); **Restore
backup**. If the file was changed by another program, **Save anyway** applies
the updates to the blocks of the file as it is now (matched by type, references
and tooltip). Removing a block also removes the "extracted by VRConfig Helper"
comment the Extract page wrote above it, and its group comment when no block is
left under it. Updates of another aircraft's file are
kept until Save or Cancel, also when an aircraft is loaded; those of the loaded
aircraft (before or after the load) are discarded like on the other pages.

* **`manip_actions.ini`** - see *Rule files* below:

  ```ini
  [file]
  version   = 1

  [action axis_knob]                 ; ACTION axis_knob <params>
  label     = Knob on a dataref (axis_knob)
  from      = command_knob axis_knob drag_axis drag_axis_pix   ; offered for these types
  confirmed = command_knob axis_knob ; Laminar's aircraft convert these this way (✓)
  version   = 12                     ; needs X-Plane 12 (empty = any)
  help      = In VR the manipulator becomes a knob ...
  param     = min : number : 0.0 : Lowest value the dataref is set to.
  param     = dataref : dataref :  : The dataref to change.
  prefill   = axis_knob : 2 3 4 6    ; from that OBJ type: its parameters (1 = cursor, - = none)
  reverse   = step                   ; this param gets a Reverse tick (flips its sign)

  [setting JOYSTICK_MANIP]           ; a block line; no param = a flag
  label = Realistic yoke: side stick (JOYSTICK_MANIP)
  for   = drag_xy                    ; types or categories (knob switch lever yoke); empty = all
  group = Realistic yoke             ; only one of a group per block; the group's caption
  help  = ...
  param = pitch_min : number : -30.0 : Controller tilt for full nose down / up, degrees.
  ```

  Parameter kinds: `number`, `dataref`, `command`, `text`. Every `[setting]`
  keyword (and `ACTION`) is a line that belongs inside a block - the vrconfig
  reader uses that to report stray lines. The shipped file has 9 ACTIONs
  (`axis_knob`, `axis_switch_up_down`, `axis_switch_left_right`, `command_axis`,
  `command_knob`, `command_switch_up_down2`, `command_switch_left_right2`,
  `drag_axis`, `drag_xy`) and 9 settings, with help texts from Laminar's vrconfig
  specification and the conversions seen in Laminar's own aircraft.

The vrconfig reader also understands: a tooltip after the references on a
`BEGIN_MANIP` line (`drag_xy <roll> <pitch> Pilot's yoke` - the number of
references depends on the type), `CMND=` / `DREF=` prefixes (matched without
them, kept as written), decimal and negative numbers, tabs and lines without
indent.

## Rule files

`manip_types.ini` and `manip_actions.ini` are the only place where the
manipulator types, ACTIONs and settings are defined - the plugin has no built-in
copy. They belong to the plugin: they ship next to it (`libs/` in the source; the
build copies them to `<build>/vrconfig_helper/`; install and deploy put them in
`<X-Plane>/Resources/plugins/vrconfig_helper/` and replace them every time), and
each starts with a **do not edit** warning.

* **Your own rules:** copy the file to `custom_manip_types.ini` /
  `custom_manip_actions.ini` in the same folder and edit the copy. When a custom
  file exists the plugin reads it **instead of** the shipped one (the whole
  file, nothing is merged); the pages show "Using custom_... (your own ...)".
  Install and deploy never touch the custom files. Delete the custom file to go
  back to the shipped rules.
* **Versions:** each file starts with `[file] version = N`; the plugin expects
  a version (`kTypesFileVersion`, `kActionsFileVersion`). A custom file with
  another version shows an orange note: it was made from an older shipped file -
  compare it with the new one and update your copy. A shipped file with another
  version says to reinstall the plugin. When the shipped file's content
  changes, its version and the constant go up together.
* **Missing file:** a red "... is missing from the plugin folder - reinstall the
  plugin". The Extract page then has no types; the Updater page has no ACTIONs
  or settings (Before / After and free-text editing still work); the vrconfig
  reader counts every word of a `BEGIN_MANIP` line as a reference.
* Both files are read again when a page reads (parsed only when the file used or
  its content changed).

## Code conventions

* Namespace `hpvr` (`hpvr::ui`, `hpvr::color`, `hpvr::vrconfig`, `hpvr::hotspot`, `hpvr::manip`, `hpvr::xp`, `hpvr::log`).
* Functions that draw UI start with `ui_`; popups with `ui_popup_`
  (reusable: `ui::ui_popup_yes_no(id, YesNoText)`, `ui::ui_popup_message`).
* Colours by name only: `hpvr::color::color_vec4_<name>` (standard web names and
  the project's `color_vec4_hpvr_*`); which colour is used for what is in `ui/Theme.h`.
* Lambdas are named `lmbda_*`; logging inside them uses the `LOG_*_AS` macros.
* No one-letter names, except `i` as a `for` loop counter and the `x` / `y` / `z`
  axis fields of `Vec3` (the vrconfig axis names).

## Logging

All messages go to `vrconfig_helper.log` in the plugin folder
(`Resources/plugins/vrconfig_helper/`). At every start, and when the file reaches
5 MB, it is rotated: `vrconfig_helper.log` -> `.log.1` -> `.log.2`; older copies are
deleted. Both limits are in `src/SharedInfo.h` (`info::kLogMaxFileSize`,
`info::kLogKeptCopies`).

Messages are also copied to X-Plane's `Log.txt` when the mirror flag is on:
default `info::kLogToXPlaneDefault` (true) in `src/SharedInfo.h`, at runtime
`vrconfig_helper::log::SetMirrorToXPlane(bool)`.

Debug builds log `LOG_DEBUG` messages, Release builds start at `LOG_INFO`
(`vrconfig_helper::log::SetLevel(...)` changes it).

### Conventions

* Use the macros from `Log.h`; they format with C++20 `std::format` and add the
  function name: `LOG_INFO("loaded {} hotspots", n)` -> `[LoadHotspots] loaded 3 hotspots`.
* `LOG_DEBUG` also adds the line: `[LoadHotspots:42] ...`.
* `LOG_ERROR`, `LOG_WARN`, `LOG_INFO`, `LOG_DEBUG`.
* Lambdas are named with the `lmbda_` prefix and log through the `*_AS` macros
  (inside a lambda `__func__` is only `operator()`):

  ```cpp
  auto lmbda_onSave = [&]() { LOG_INFO_AS("lmbda_onSave", "saved {}", path); };
  ```

## Fonts

`fonts::regular` is DejaVu Sans from X-Plane (ImGui's default font if missing);
it is not shipped with the plugin. `fonts::fontawsome` holds the Font Awesome 5
Solid icons named in `IconsFontAwesome5.h`, embedded from `libs/fonts/fa-solid-900`
(see Credits):

```cpp
#include "Fonts.h"
ImGui::PushFont(vrconfig_helper::fonts::fontawsome, 0.0f);   // 0 = keep current size
ImGui::Button(ICON_FA_CROSSHAIRS);
ImGui::PopFont();
```

## Credits

* **Icons:** [Font Awesome Free 5](https://fontawesome.com) by Fonticons, Inc. The
  plugin embeds its solid icon font, `fa-solid-900` (`libs/fonts/fa-solid-900.ttf`,
  embedded as `fa-solid-900.inc`; names in `IconsFontAwesome5.h`, generated by
  [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders)).
  Licences: the icons under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/),
  the font file under the [SIL Open Font License 1.1](https://scripts.sil.org/OFL),
  the code under MIT (see [fontawesome.com/license/free](https://fontawesome.com/license/free)).
  The About box shows the same credit.
* **Libraries:** [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (MIT
  licence, `libs/imgui/LICENSE.txt`) and ImgWindow by Christopher Collins, extended by
  Steven L. Goldberg ([slgoldberg](https://github.com/slgoldberg)) (BSD 3-clause licence,
  `libs/ImgWindow/COPYING.md`); logging by loguru (public domain). The About
  box credits Dear ImGui and ImgWindow.
* **Inspired by** the "VR Tools" plugin, by jrKok.
