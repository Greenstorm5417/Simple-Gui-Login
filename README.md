# Simple-Gui-Login

A Linux framebuffer login screen written in C. It authenticates through PAM and
starts the session command compiled into `config.h`.

## Build

Install a C11 compiler, Make, PAM development headers, and
Python 3 with Pillow. Python and Pillow are needed only during the build.
The screen requires `/dev/fb0` with a true-color display of at least 480 × 500.

```sh
make
make check
```

## Configuration

Edit `config.h`, then run `make` again. Settings are compiled
into the executable; changing the file does not change an already running process.

| Setting | What to put there |
| --- | --- |
| `user` | The local account name prefilled in the username field. You can change it on the login screen. |
| `tty` | A numbered Linux VT, such as `/dev/tty1`. `--tty` overrides this for a single launch. Valid numbers are 1–63. |
| `title` | The text above the username field. Use `""` to hide it. |
| `pam_service` | The PAM policy name. `"login"` uses `/etc/pam.d/login`. |
| `session_type` | The session type passed to PAM as `XDG_SESSION_TYPE`, normally `"wayland"`. |
| `session` | The executable's absolute path followed by its arguments and a final `NULL`. There are 16 slots, including the final `NULL`. |

The supplied session command points to `/home/green/.local/bin/green-session`.
Replace it with your own executable or wrapper script.

For Mango:

```c
.session = { "/usr/bin/dbus-run-session", "/usr/bin/mango", NULL },
```

For dwl:

```c
.session = { "/usr/bin/dbus-run-session", "/usr/bin/dwl", NULL },
```

For a script that starts your desktop and its services:

```c
.session = { "/usr/bin/dbus-run-session", "/home/yourname/.local/bin/session", NULL },
```

Each argument occupies its own slot. The program calls `execv` directly, so shell
variables, `~`, pipes, and command substitutions are not expanded. Put shell
commands in an executable script. The session starts as the authenticated user,
with their home directory as the working directory.

### Colors

Use `0xRRGGBB` values in `config.h`.

| Setting | Screen element |
| --- | --- |
| `background` | Full-screen background. |
| `text` | Title and field contents. |
| `muted` | Field labels and normal status messages. |
| `accent` | Focused field outline and empty-field cursor. |
| `card` | Login panel background. |
| `border` | Login panel outline. |
| `field` | Input background. |
| `field_border` | Unfocused input outline. |
| `error` | Error message text. |

### Image

Choose a PNG, JPEG, or another image format supported by Pillow:

```sh
make IMAGE=/absolute/path/to/image.png
```

Relative image paths start at the source directory. The default is
`assets/void-logo.png`. The build embeds the image as RGBA pixels, fits it within
128 × 80 while preserving its aspect ratio, and keeps smaller images at their
original size. The login screen does not read the image file at runtime.

Preview the compiled theme without opening a tty or authenticating:

```sh
build/green-login --preview preview.ppm
```

## Install and run

```sh
sudo make install
```

The default install path is `/usr/local/bin/green-login`. Override `PREFIX` to
change it, or use `DESTDIR` for a staged installation. Run `make clean` to remove
build output.

Configure your service
supervisor to run that executable as root on the selected tty. Stop that tty's
getty first so the two programs do not compete for it. Keep a second tty available
while testing. The greeter waits until its tty is active before drawing.

Tab changes fields, Enter submits, Escape or Ctrl-U clears the selected field,
and Backspace removes a character. After login, a waiting parent and PAM session
worker remain until the desktop exits; the login screen then returns.

The C code is MIT licensed. The bundled font has its own license in
`assets/FONT-LICENSE.txt`.
