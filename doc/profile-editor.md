# Application profiles in spnavcfg

Build and install the matching jl1990 forks of spacenavd, libspnav, and spnavcfg.
The profile editor is an experimental extension of the local socket protocol.
Older daemons continue to use spnavcfg's original Axes and Buttons interface.

## Everyday use

1. Open **Profiles**, choose **New**, and give the profile a unique name.
2. Use **Choose application** to select a desktop application. Alternatively,
   choose **Detect application**, then keep the target window focused for five
   seconds. GNOME Wayland requires the installed SpaceNav focus extension/helper;
   X11 uses the focused window's class.
3. Select **Buttons**. **Identify a button (10 seconds)** temporarily suppresses
   device mappings while physical button presses select and highlight their rows.
   Identification expires automatically; disconnecting the editor also ends it.
4. Uncheck **Use Default** for a button or axis to customize it. Checking it again
   restores inheritance, including future edits to Default. Sensitivity and
   Swap Y–Z have independent inheritance controls.
5. For a keyboard assignment, click the recorder and press a shortcut. The **…**
   button accepts key names such as `Control_L+s` or the modifier-only `Shift_L`.
   Numpad keys are distinct from the number row. Shortcuts can contain up to eight
   keys. They are held until the device button is released, including across
   application/profile changes. Shared modifiers stay down until all bindings
   using them are released.
6. Enter a short screen label, such as `Frame selected`. The Enterprise display
   shows labels for its first twelve buttons. Its current bitmap font supports
   ASCII; keep labels short and use basic Latin characters for predictable display.
7. **Apply** changes the running session. **Save** applies and persists the changes.
   **Revert draft** discards unapplied edits and reloads the running configuration.
   A failed save leaves session changes active and reports the failure.

The **Editing** profile is independent of **Currently active**. Focus changes do
not switch the editor or overwrite a draft. The selected application profile can
be duplicated, renamed or deleted; Default cannot be renamed or deleted. There
is a limit of sixteen application profiles plus Default. Global screen and LED
settings remain in the existing controls.

Application matching is case-insensitive: `=blender.desktop` means an exact ID,
while `blender` matches a substring. The first matching profile wins. Applications
without a match use Default.

## Persistence and compatibility

Saving atomically replaces the configuration file. Global comments/options are
retained; profile blocks are regenerated from the edited settings. Canonical
`editor-controls`, `editor-axisN`, and `editor-buttonN` records persist explicit
inheritance, numeric keysyms, and hex-encoded label bytes. These records require
this daemon version. Keep the installation backup if reverting to an older build.
The normal global save operation uses Default mappings even while an application
profile is active. Existing legacy profile names/mappings load into the editor.

A snapshot carries a revision. The daemon validates every profile and allocates
replacement state before accepting any changes. Stale revisions return `-2` and
leave the draft/live state intact; unsupported Linux keys return `-3`. Reload or
reset and legacy mapping edits invalidate outstanding snapshots. Transfers are
bounded and private to each client; incomplete or out-of-order writes cannot be
applied.

Keyboard delivery on Linux uses uinput, including native Wayland applications.
Key translation currently uses the daemon's Linux key-position table, not a
compositor-provided keyboard layout. Non-US layouts, punctuation and
application-specific shortcut behavior should be tested in the target app.
Unsupported keysyms are rejected rather than silently accepted.

## Validation

`sh tests/run.sh` includes configuration round trips, snapshot validation,
inheritance, stale revisions, transfer bounds/client isolation, held-key release
and overlapping modifiers. Set `TEST_CFLAGS='-fsanitize=address,undefined'` for
sanitizer coverage. The sibling libspnav suite exercises real socket framing,
interleaved input events and conflict propagation. spnavcfg's offscreen tests
exercise draft isolation, physical-button row selection and shortcut conversion.
