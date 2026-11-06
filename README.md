# gamepad-picker

A tiny, dependency-free SDL2 tool for showing a gamepad-navigable choice screen before launching something else. Built for [PortMaster](https://portmaster.games/) ports (pick a language, a difficulty, a game variant) but not tied to it; it just prints a chosen command line to stdout.

## Usage

```
gamepad-picker "Label 1|command 1" "Label 2|command 2" ...
```

Each argument is a display label and a full command line to run if chosen, split on the first `|`. An argument with no `|` is used as both its own label and command. On confirm, the chosen command line is printed to stdout and the tool exits 0. On cancel (B button) or quit, it exits 1 with no output, so a caller can fall back to a default.

```
$ ./gamepad-picker "Deutsch|./game" "English|./game --language en"
./game --language en
```

Controls: D-pad up/down or left-stick Y axis to navigate, A to confirm, B to cancel. Keyboard up/down/enter/space/escape work too, useful for testing without a controller attached.

If `GAMEPAD_PICKER_OUTPUT` is set to a file path, the chosen command is also written there (truncated to empty at startup). Scripts should read that file rather than capture stdout via `$(...)`, since unrelated output can end up mixed into stdout in some launch environments.

## Example: PortMaster launch script

```bash
export GAMEPAD_PICKER_OUTPUT="$GAMEDIR/.picker_result"
./gamepad-picker.${DEVICE_ARCH} "Deutsch|./$BINARY" "English|./$BINARY --language en" > /dev/null
LAUNCH_CMD=$(cat "$GAMEPAD_PICKER_OUTPUT")
LAUNCH_CMD="${LAUNCH_CMD:-./$BINARY}"
```

## Look

The picker uses the same card as [portmsg](https://github.com/PortsMaster/portmsg), PortMaster's message tool. It has the same background, card, fonts and key chips, and it scales from 480x320 to 1080p. Long labels wrap onto more lines instead of being cut off. A long list scrolls with the selection, showing fades and a scrollbar. The footer shows the D-pad, A and B hints.

## Build

```shell
apt-get install -y libsdl2-dev xxd
make                # -> gamepad-picker.aarch64 (ARCH=armhf / x86_64 to name other native builds)
```

The aarch64 build uses portmsg's own `_start` (`start_aarch64.S`), so the binary only needs GLIBC_2.17. The fonts are compiled in. The only runtime dependency is `libSDL2`.

Dev preview, no display needed: `gamepad-picker.aarch64 --snapshot out.bmp --size 640x480 [--snapshot-selected N] "Label|cmd" ...`

## License

MIT, see [LICENSE](LICENSE). Bundled from portmsg:
- stb_truetype, public domain or MIT.
- Portmsg Sans, a renamed subset of Lato under the SIL OFL 1.1. See [fonts/OFL.txt](fonts/OFL.txt).

A port that ships the picker needs `LICENSE` and `fonts/OFL.txt` in its `licenses/`.
