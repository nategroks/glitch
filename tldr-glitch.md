# glitch

> Animated vaporwave system information.
> Configurable shapes and colors via `~/.config/glitch/` (use `color.config` or `COLOR_CONFIG`).
> More information: `man glitch`.

- Run with default animation:

`glitch`

- Run once (single frame, then exit):

`glitch --once`

- Set animation speed in milliseconds:

`glitch --speed 80`

- Use environment variable for speed:

`GLITCH_SPEED=200 glitch`

- Apply a manual palette (hex) in `~/.config/glitch/color.config`:

`BG1=#0f0f0f ... FG_PIPE=#c0b090`

- Build variants:

`make lean` (smaller/faster), `make minimal` (text-only, no curl/kitty)

- Rebuild with new shape or colors after editing config:

`./install.sh`
