# PSP Clarity (PRO CFW, PSP-2000/3000)

Sharpen filter + pixel-overdrive (ghosting compensation) with volume-button control.

## Build
1. Install PSPSDK (pspdev toolchain).
2. Copy `libpspsystemctrl_kernel.a` from the PRO / ARK-4 SDK into `./libs`.
3. Run `make` -> produces `psp_clarity.prx`.

## Install (PRO CFW)
1. Copy `psp_clarity.prx` to `ms0:/seplugins/`.
2. Add this line to `ms0:/seplugins/game.txt` (create the file if missing):
   `ms0:/seplugins/psp_clarity.prx 1`
3. Restart the PSP (or Recovery menu -> Plugins to verify it is enabled).

## Controls
| Input                         | Action                  |
|-------------------------------|-------------------------|
| VOL+ and VOL- together        | Plugin ON / OFF         |
| SELECT (held) + VOL+ / VOL-   | Sharpness +1 / -1 (0-8) |
| START  (held) + VOL+ / VOL-   | Overdrive +1 / -1 (0-8) |

The on-screen status box appears for ~3 s after every change.
Settings persist in `ms0:/seplugins/psp_clarity.ini`.

## Tuning tips
- Start with Sharp 2-3, Overdrive 2-4. Too much overdrive causes bright/dark halos (inverse ghosting).
- If a game slows down, switch the plugin off for that game or lower the effect.
