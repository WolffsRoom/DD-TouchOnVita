# TouchOnVita v0.61 — missing UI assets and absent logs

**Date:** 2026-11-20
**Status:** experimental diagnostic build, not stable. A physical PS Vita
running an unmodified 1.17 executable is still needed for confirmation.

## Report

A tester with digital Darkest Dungeon PCSE00919 1.17 reports that the
Start+L panel now appears, but it says UI FILES MISSING, and no logs could
be found under the path written as ux:data/DDTouchUI.

The path in that report omits the numeral ZERO: the correct mount is
ux0:data/DDTouchUI/. That might be shorthand, not necessarily an actual
installation mistake.

## What the source code and package tell us

- UI FILES MISSING indicates at least one missing or unreadable resource.
  Expected sizes: UI.rgba 882360, Selector.rgba 41832, On.rgba 14204,
  Off.rgba 14204, Mod_on.rgba 145500, Mod_off.rgba 145500 bytes.
- The v0.60 ZIP included all six files at expected sizes, but had a
  flat layout that could confuse installation to two separate partitions.
- Original logging ignored open/write errors. The absence of log files
  could also indicate a failed write, an incorrect path or an unmounted
  storage folder. This is NOT proven to be the cause on the reporter's Vita.
- Start+L opening implies at least the display and controller hooks load;
  RePatch is not required, and old saves do not affect plugin loading.

## v0.61 diagnostic improvements

- Shows the FIRST missing image on-screen using the fallback text UI.
- Shows where logs were written: LOGS UX0 DATA, LOGS UR0 DATA,
  or NO LOG ACCESS if neither was usable.
- Checks short writes and errors and retries the ux0 logging folder.
  If writes still fail, attempts logging under ur0:data/DDTouchUI/.
- Keeps report filenames startup_v060.txt and assets_status_v060.txt
  so collectors can compare with the previous diagnostic.
- The new ZIP mirrors Vita paths:
  ur0/tai/DDTouchUI.suprx; ux0/data/DDTouchUI/mode.ini and six assets.
- Does not alter native click behavior or include game data/audio/saves.

## Tests

1. VitaSDK compilation passed.
2. Vita3K 1.16 with UI.rgba temporarily removed: no crash, log correctly
   showed actual=-1, expected=882360, error io=80010002; all other
   five RGBA resources had correct sizes.
3. Vita3K 1.16 with ux0:data/DDTouchUI replaced with a regular file:
   no crash. Logs hooks.txt, startup_v060.txt and assets_status_v060.txt
   appeared in ur0:data/DDTouchUI as expected.
4. Isolated emulator original resources and SUPRX were restored.
5. v0.61 has NOT been tested on a real stock 1.17 Vita, and its on-screen
   text has not been visually verified on hardware.

## How to isolate the reporter's problem

- Confirm files are exactly in ux0:data/DDTouchUI, not one extra nested
  directory and not a mistaken ux:data mount.
- Open Start+L and send a photo showing the first missing resource.
- Look for logs under BOTH ux0:data/DDTouchUI/ and ur0:data/DDTouchUI/.
- Send startup_v060.txt, assets_status_v060.txt, engine_hook_init.txt,
  engine_compat_probe_v047.txt and hooks.txt if available.
- Confirm whether the installation was manual or via Wolff's Store.
- Do not request the game executable, PSARC, or savedata.

Keep v0.59 as the public release until physical compatibility is confirmed.
