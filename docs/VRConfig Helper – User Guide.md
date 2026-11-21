# VRConfig Helper – User Guide

Nov 18, 2026 · @Saar

## Getting started

VRConfig Helper edits an aircraft's `<aircraft>_vrconfig.txt` from inside X-Plane: the teleport hotspots you jump to in VR, and the VR behaviour of the cockpit's knobs, switches and levers. Nothing is written to the file until you press **Save**.

**Install.** Copy the `vrconfig_helper` folder into `X-Plane/Resources/plugins/`. It holds the plugin (`lin_x64/vrconfig_helper.xpl`) and its two rule files, `manip_types.ini` and `manip_actions.ini`. Keep them together.

**Open the window.** Use the menu **Plugins → VRConfig Helper → Toggle Window**, or bind the command `vrconfig_helper/toggle_main_window` to a key or a controller button. The window has a fixed size; questions and forms open as popups on top of it.

**The Home page** offers three screens:

| Button | What it does | When to use it |
| --- | --- | --- |
| Hotspot | Move, turn, rename, add or delete the teleport hotspots of the loaded aircraft. | The VR seats or standing spots are missing or in the wrong place. |
| Extract manipulators to vrconfig | Finds the knobs, switches and levers in the aircraft's OBJ files and adds them to the vrconfig file with default VR settings. | The aircraft has no vrconfig, or its controls are hard to use in VR. |
| VRConfig Manipulator Updater | Tunes the blocks already in a vrconfig file, or converts a control to another type. | After Extract, or to fix an existing aircraft. Works for any aircraft. |

**About** (top right of the Home page) shows the plugin's version, who made it, its licence, and that it was inspired by the "VR Tools" plugin by jrKok.

**Publishing an aircraft?** If you used VRConfig Helper to build or improve its vrconfig file, please credit it in your release notes or documentation, for example "VR configuration made with VRConfig Helper". The same request is shown at the bottom of the Home page.

&#91;image: The Home page with the credit note at the bottom, and the About box\]

**Rules that apply everywhere:**

- **Orange** means an unsaved change, **green** means saved in this X-Plane run. Both always come with a symbol (● or ✓).
- While one screen has unsaved changes, Home disables the other screens and says why. Save or cancel first.
- Loading or reloading an aircraft discards unsaved changes for that aircraft.
- The first Save of a file in each X-Plane run makes a backup of it first, so **Restore backup** can undo the session.
- Every **(?)** mark explains the item next to it: hover over it.

## Hotspot screen

This screen places the teleport hotspots of the **loaded** aircraft: the seats and standing spots you jump to in VR. Sit in VR while you use it, so you see each change from the hotspot itself.

**Typical use:** pick a hotspot, sit where it should be, press **Use my current head position**, fine-tune, then **Save** and reload the aircraft.

&#91;image: The Hotspot screen\]

1. **Pick a hotspot** in the **Hotspot** list. Its type (SITTING or STANDING) shows next to it. **Rename**, **Add** and **Delete** work on the list; **Refresh** reads the file again.
2. **Move it.**
   - **Use my current head position** copies where your head is now into the hotspot.
   - **Forward / Back / Left / Right / Up / Down** nudge it; you can also type values in the **New** column.
   - The **Saved** column shows the values in the file, for comparison.
3. **Turn it.** The heading buttons turn it by 1°, or face it **N / E / S / W**. **Adv** opens pitch and roll, and **Copy a value to other hotspots** (for example the same eye height for every seat).
4. **Check it.** **Go to hotspot** (purple) teleports you there. **Live view (WYSIWYG)** shows your edits in the sim as you make them. **Revert this hotspot** undoes its changes.
5. **Save.** Nothing is written before **Save**. **Cancel changes** undoes all unsaved changes; **Restore backup** puts back the file as it was before your first save in this X-Plane run. Reload the aircraft to try the result.

**Problems in the file:**

- **Commas between numbers** (`AABB 0.39, 0.64, 0.58`) make a hotspot read-only. Pick it and press **Remove the commas**; they're removed when you save.
- An orange **⚠ *N*** button next to **Explain** lists other remarks: misspelled keywords (for example `EGIN_TELEPORT_HOTSPOT`), lines outside a block, the same hotspot name used twice.

**Backup:** `<aircraft>_vrconfig.txt.bak`.

## Extract manipulators to vrconfig screen

This screen finds the controls in the **loaded** aircraft's 3D cockpit files and adds a `BEGIN_MANIP` block for each one to its vrconfig file, with sensible VR defaults. Use it first on an aircraft with no vrconfig, or with controls that are hard to use in VR. You can then fine-tune the blocks in the Manipulator Updater.

&#91;image: Left: choosing the files in step 1. Right: the result of Extract in step 2\]

**Step 1: Read the aircraft files.** The plugin reads the list of objects in the aircraft's `.acf` (plus its `_cockpit.obj`) in the background; **Abort** stops it. A popup lists each file with how many controls it has. Tick the files to use (usually the cockpit ones), then press **Use selected files**. Files that are missing or have no controls are greyed out. **Change files** brings the popup back.

**Step 2: Pick the manipulators to extract.**

1. **Tick the types** you want, grouped as knobs, switches, levers and yoke. Click a group's name to tick or clear its whole row. Each **(?)** explains a type.
2. **Pick single controls (optional).** The number next to a type is a button: it lists that type's controls with a tick each. Untick the ones you don't want; the button then shows, for example, "15/17".
3. **Press Extract to vrconfig.** The result list shows each control under its type:
   - **added** – new, will be written;
   - **exists** – already in the file, left alone. Tick **Override existing** first to replace those (**overridden**);
   - untick a row to leave it out (**skipped**).
4. **Save.** New blocks go to the end of the file, sorted by group: the path of their first command or dataref, such as `sim/autopilot`. Each group gets a `# --- sim/autopilot ---` comment, each block a comment with its name and OBJ file.

**Defaults written:** knobs get `DEG_PER_ACTION` (how far to turn the wrist per click): 30° for selectors, 10° for altitude knobs, 5° for the rest. Other types get the template from `manip_types.ini`.

**Backup:** `<aircraft>_vrconfig.txt.manip.bak`. Loading an aircraft starts step 1 again.

## VRConfig Manipulator Updater screen

This screen tunes the `BEGIN_MANIP` blocks already in a vrconfig file, or converts a control to another type. It works on **any** aircraft, not only the loaded one; X-Plane uses the changes the next time that aircraft loads.

&#91;image: Left: the blocks of a vrconfig file. Right: converting a knob with ACTION, values filled in from the OBJ\]

**Step 1: Choose the aircraft.** The list shows the loaded aircraft's folder first, in **blue**, with the loaded aircraft at the top. Tick **List all aircraft** to add every aircraft under `X-Plane/Aircraft`; those in other folders are **red**, with a pulsing warning when you pick one. Picking another variant from the same folder shows a grey note instead.

**Step 2: Read vrconfig.** The list of blocks appears at once; the aircraft's OBJ files are checked in the background. Narrow the list with:

- the **type ticks** (each has a **(?)** that explains the type and its values);
- **Find**: part of a name, command or dataref;
- **Group**: the path of the first command or dataref, such as `sim/autopilot`.

The tag next to a block tells you about it:

| Tag | Meaning |
| --- | --- |
| ⚠ not in OBJ | No such control in the aircraft's OBJ files. X-Plane may show an error for it when the aircraft loads. |
| ⚠ duplicate | Same type, references and tooltip as an earlier block; X-Plane can't tell them apart. |
| hold/spring | Has `HOLD_MANIP`: the command keeps running while you hold the control. |
| default | No settings: X-Plane's default behaviour. |
| → axis | An `ACTION` makes X-Plane treat it as another type in VR. |

**Change a block:** press its name. The popup shows **Before** (saved) and **After** (what Save will write).

1. **ACTION** converts the control: ✓ means Laminar's own aircraft use that conversion, orange ⚠ means it isn't confirmed. Values are filled in from the OBJ file when they can be; **Reverse** flips a knob's direction.
2. **Settings** are ticks with values, for example **Degrees per action**, **Hold / spring-loaded**, **Wraps around**, or a **Realistic yoke** mode for yokes and sticks.
3. Press **Apply** to keep the change, **Remove** to delete the block on save, or **Revert** to undo.

**Need something the screen doesn't offer?** Tick **Edit the After text myself (advanced)** and type the block. It's checked as you type: red errors must be fixed, orange warnings allow **Apply anyway**.

**Save** writes all changes. Changes for an aircraft that isn't loaded stay until you save or cancel.

**Backup:** `<aircraft>_vrconfig.txt.manip.upd.bak`.

## Rule files: your own types and actions

Two text files in the plugin folder decide what the manipulator screens offer. Don't edit them: every plugin update replaces them.

| File | What it defines |
| --- | --- |
| `manip_types.ini` | The control types the plugin knows, their help texts and the block each one gets when extracted. |
| `manip_actions.ini` | The ACTION conversions and settings offered by the Updater, with their help texts. |

**To change them:**

1. Copy the file in the same folder, naming the copy with `custom_` in front: `custom_manip_types.ini` or `custom_manip_actions.ini`.
2. Edit the copy. Each file starts with notes that explain its keys.
3. Read the files again: open the screen and press **Read again**.

When a custom file exists, the plugin uses it **instead of** the shipped one; nothing is merged. The screens then show "Using custom\_…". Delete the copy to go back to the shipped rules.

**After a plugin update** the screen may say that your custom copy was made from an older version. Compare it with the new shipped file, bring over what changed, and raise the `version` at the top of your copy to match.

**Mistakes** in a file (an unknown key, a section line that lost its `;`) are listed in red on the screen, and that entry is skipped. If a shipped file is missing, the screen says so: reinstall the plugin.

## Files and troubleshooting

| File | Where | Written by |
| --- | --- | --- |
| `<aircraft>_vrconfig.txt` | Aircraft folder | Save, on any screen |
| `…_vrconfig.txt.bak` | Aircraft folder | Hotspot screen, first save per X-Plane run |
| `…_vrconfig.txt.manip.bak` | Aircraft folder | Extract screen, first save per X-Plane run |
| `…_vrconfig.txt.manip.upd.bak` | Aircraft folder | Updater screen, first save per X-Plane run |
| `custom_manip_types.ini`, `custom_manip_actions.ini` | Plugin folder | You (optional) |
| `vrconfig_helper.log` | Plugin folder | The plugin: what it read, wrote and found |

**I don't see my changes in VR.** X-Plane reads the vrconfig file only when the aircraft loads. Save, then reload the aircraft.

**A screen's button on Home is greyed out.** Another screen has unsaved changes; the note under the button says which. Save or cancel there first.

**The aircraft shows an error about the vrconfig when it loads.** Open the Updater and look for **⚠ not in OBJ** or **⚠ duplicate** blocks; fix or remove them.

**The file was changed by another program while I edited.** On Save the plugin asks first. **Save anyway** applies your changes to the file as it is now.

**I want my old file back.** Press **Restore backup** on the screen you saved from. Each screen restores only its own backup.

**Something looks wrong.** Check `vrconfig_helper.log` in the plugin folder; it lists every file read and written, and every problem found.
