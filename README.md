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

## Build

```shell
gcc gamepad-picker.c -o gamepad-picker $(pkg-config --cflags --libs sdl2)
```

Only runtime dependency is `libSDL2`.

## License

MIT, see [LICENSE](LICENSE). `font8x8_basic.h` is a separate third-party file, Public Domain.
