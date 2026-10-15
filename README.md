# Simple-Gui-Login

A Linux framebuffer login screen written in C. It authenticates through PAM and
starts the session command compiled into `config.h`.

## Build

Install a C11 compiler, Make, PAM development headers, and
Python 3 with Pillow. Python and Pillow are needed only during the build.
The screen requires `/dev/fb0` with a true-color display of at least 480 × 380.

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

## Arch Linux setup

Install the build dependencies and clone the repository:

```sh
sudo pacman -Syu --needed base-devel git pam python-pillow
git clone https://github.com/Greenstorm5417/Simple-Gui-Login.git
cd Simple-Gui-Login
```

Edit `config.h`: set `.user` to your account name and replace `.session` with
an installed compositor or your session script, using an absolute path. Keep
`.tty = "/dev/tty1"` and `.pam_service = "login"` for the setup below. The
configuration examples above show session commands. Then build and install:

```sh
make
make check
sudo make install
```

Arch includes PAM headers in `pam`; no separate development package is needed.
The existing `/etc/pam.d/login` policy handles authentication and session setup.
Check that `/dev/fb0` exists before enabling the service.

Create `/etc/systemd/system/green-login.service`:

```ini
[Unit]
Description=Simple GUI Login
After=systemd-user-sessions.service getty@tty1.service plymouth-quit.service
Conflicts=getty@tty1.service

[Service]
Type=exec
ExecStart=/usr/local/bin/green-login --tty /dev/tty1
Restart=on-failure
RestartSec=1

[Install]
WantedBy=multi-user.target
```

If another display manager is enabled, disable its service first. Switch to tty2
with Ctrl+Alt+F2 and log in there before running:

```sh
sudo systemctl daemon-reload
sudo systemctl mask --now getty@tty1.service
sudo systemctl enable --now green-login.service
```

Switch to tty1 with Ctrl+Alt+F1 to use the greeter. Keep tty2 available while
testing. To restore the regular tty1 login, run these commands from tty2:

```sh
sudo systemctl disable --now green-login.service
sudo systemctl unmask getty@tty1.service
sudo systemctl start getty@tty1.service
```

View service errors with `journalctl -u green-login.service -b`.
For package details, see Arch's [PAM](https://archlinux.org/packages/core/x86_64/pam/)
and [Pillow](https://archlinux.org/packages/extra/x86_64/python-pillow/) packages,
and the [systemd service manual](https://man.archlinux.org/man/systemd.service.5.en).

## Artix Linux / OpenRC setup

These instructions assume Artix is already running OpenRC. Install dependencies:

```sh
sudo pacman -Syu --needed base-devel git pam python-pillow dbus-openrc elogind-openrc
```

Clone the repository and edit `config.h` as described above, then run `make`,
`make check`, and `sudo make install`. Keep the `login` PAM policy and make sure
it includes `pam_elogind.so` through Artix's existing session configuration;
the greeter requires `/run/user/<uid>` to be created for the logged-in user.
Check that `/dev/fb0` exists.

Create `/etc/init.d/green-login`:

```sh
#!/sbin/openrc-run

command="/usr/local/bin/green-login"
command_args="--tty /dev/tty1"
command_background=true
pidfile="/run/${RC_SVCNAME}.pid"

depend() {
    need dbus elogind
    after bootmisc
}
```

Disable any existing display manager first. Switch to tty2 with Ctrl+Alt+F2
and log in there. The commands below assume the normal tty1 login is the
`agetty.tty1` service in the `default` runlevel; check `rc-update show` and use
its actual runlevel if different.

```sh
sudo chmod 755 /etc/init.d/green-login
sudo rc-update add dbus default
sudo rc-update add elogind default
sudo rc-update del agetty.tty1 default
sudo rc-service agetty.tty1 stop
sudo rc-update add green-login default
sudo rc-service green-login start
```

Switch to tty1 with Ctrl+Alt+F1. To restore the normal login, run from tty2:

```sh
sudo rc-service green-login stop
sudo rc-update del green-login default
sudo rc-update add agetty.tty1 default
sudo rc-service agetty.tty1 start
```

If your system starts tty1 through `/etc/inittab`, disable that tty1 getty entry
instead of using the `agetty.tty1` commands, and restore the entry when reverting.
Keep tty2 enabled. Other OpenRC distributions can use the same service script,
with their own dependency packages and PAM session configuration.

See OpenRC's [service script guide](https://github.com/OpenRC/openrc/blob/master/service-script-guide.md)
and [agetty guide](https://github.com/OpenRC/openrc/blob/master/agetty-guide.md).

The C code is MIT licensed. The bundled font has its own license in
`assets/FONT-LICENSE.txt`.
