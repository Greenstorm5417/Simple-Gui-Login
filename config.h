/* Edit these settings, then rebuild with Meson. */
const struct config config = {
    /* Login defaults and PAM policy. */
    .user = "green",
    .tty = "/dev/tty1",
    .title = "VOID LINUX",
    .pam_service = "login",
    .session_type = "wayland",
    /* Executable and arguments; terminate the list with NULL. */
    .session = {"/usr/bin/dbus-run-session", "/home/green/.local/bin/green-session", NULL},

    /* RGB colors. Set the image with Meson's -Dimage option. */
    .background = 0x263238,
    .text = 0xe1e9e2,
    .muted = 0x98a8a5,
    .accent = 0xa6bfa0,
    .card = 0x2b383e,
    .border = 0x354249,
    .field = 0x223036,
    .field_border = 0x425058,
    .error = 0xe6aaa0,
};
