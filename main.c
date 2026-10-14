/* SPDX-License-Identifier: MIT */
#include "login.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "config.h"

volatile sig_atomic_t stopping, session_group;

static void stop(int signal)
{
    (void)signal;
    stopping = 1;
    if (session_group > 0) (void)kill(-session_group, SIGTERM);
}

static int signals(void)
{
    struct sigaction action = { .sa_handler = stop };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGHUP, &action, NULL)) return -1;
    action.sa_handler = SIG_IGN;
    return sigaction(SIGTTOU, &action, NULL);
}

static void backspace(char *value)
{
    size_t n = strlen(value);
    if (!n) return;
    do { n--; } while (n && ((unsigned char)value[n] & 0xc0) == 0x80);
    explicit_bzero(value + n, strlen(value + n));
}

static void append(char *buffer, size_t size, const char *text)
{
    size_t length = strlen(buffer), more = strlen(text);
    if (length + more < size) memcpy(buffer + length, text, more + 1);
}

void notice(struct ui *ui, const char *message, bool error)
{
    snprintf(ui->status, sizeof ui->status, "%s", message);
    for (char *p = ui->status; *p; p++) if (*p == '\r' || *p == '\n') *p = ' ';
    ui->error = error;
    (void)draw(ui);
}

int answer(struct ui *ui, const char *prompt, bool secret, char out[SECRET_SIZE])
{
    if (!ui->interactive) return -1;
    explicit_bzero(out, SECRET_SIZE);
    explicit_bzero(ui->password, sizeof ui->password);
    notice(ui, prompt, false);
    while (!stopping) {
        char text[5];
        int key = read_key(ui->tty, text);
        if (key == -1) { if (errno == EINTR) continue; break; }
        if (key == KEY_ENTER) { explicit_bzero(ui->password, sizeof ui->password); return 0; }
        if (key == KEY_ESCAPE) break;
        if (key == KEY_CLEAR) explicit_bzero(out, SECRET_SIZE);
        else if (key == KEY_BACKSPACE) backspace(out);
        else if (key > 0) append(out, SECRET_SIZE, text);
        if (secret) snprintf(ui->password, sizeof ui->password, "%s", out);
        else snprintf(ui->status, sizeof ui->status, "%.160s", out);
        explicit_bzero(text, sizeof text);
        if (draw(ui) < 0) break;
    }
    explicit_bzero(out, SECRET_SIZE);
    explicit_bzero(ui->password, sizeof ui->password);
    return -1;
}

static int report_session(void)
{
    printf("uid=%lu euid=%lu gid=%lu runtime=%s home=%s\n",
           (unsigned long)getuid(), (unsigned long)geteuid(), (unsigned long)getgid(),
           getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "", getenv("HOME") ? getenv("HOME") : "");
    return getuid() == 0 || geteuid() == 0;
}

int main(int argc, char **argv)
{
    struct ui ui = { .tty = -1, .fb = -1, .interactive = true, .focus = 1 };
    const char *tty = config.tty, *image = NULL;
    bool auth = false, probe = false;
    int result = EXIT_FAILURE;
    snprintf(ui.user, sizeof ui.user, "%s", config.user);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tty") && i + 1 < argc) tty = argv[++i];
        else if (!strcmp(argv[i], "--preview") && i + 1 < argc) image = argv[++i];
        else if (!strcmp(argv[i], "--auth-test")) auth = true;
        else if (!strcmp(argv[i], "--session-test")) auth = probe = true;
        else if (!strcmp(argv[i], "--report-session")) return report_session();
        else if (!strcmp(argv[i], "--help")) {
            puts("green-login [--tty /dev/tty1]\n  --preview FILE.ppm\n  --auth-test\n  --session-test");
            return EXIT_SUCCESS;
        } else { fprintf(stderr, "green-login: unknown or incomplete option: %s\n", argv[i]); return EXIT_FAILURE; }
    }
    if (image) return preview(&ui, image) ? EXIT_FAILURE : EXIT_SUCCESS;
    if (geteuid()) { fputs("green-login: run as root, without setuid bits\n", stderr); return EXIT_FAILURE; }
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) < 0 || signals() < 0) goto done;
    if (terminal_open(&ui, auth ? "/dev/tty" : tty, !auth) < 0) goto done;
    if (!auth && (framebuffer_open(&ui) < 0 || framebuffer_clear(&ui) < 0 || draw(&ui) < 0)) goto done;
    if (auth) {
        char password[SECRET_SIZE];
        fputs("Password (PAM test):\n", stderr);
        if (answer(&ui, "Password:", true, password)) goto done;
        memcpy(ui.password, password, sizeof password);
        explicit_bzero(password, sizeof password);
        result = login_session(&ui, !probe, probe) ? EXIT_FAILURE : EXIT_SUCCESS;
        if (!result) puts("PAM authentication and cleanup passed");
        goto done;
    }
    result = EXIT_SUCCESS;
    while (!stopping) {
        char text[5];
        int key = read_key(ui.tty, text);
        if (key == -1) { if (errno == EINTR) continue; result = EXIT_FAILURE; break; }
        char *value = ui.focus ? ui.password : ui.user;
        size_t size = ui.focus ? sizeof ui.password : sizeof ui.user;
        if (key == KEY_TAB) ui.focus ^= 1;
        else if (key == KEY_CLEAR || key == KEY_ESCAPE) explicit_bzero(value, size);
        else if (key == KEY_BACKSPACE) backspace(value);
        else if (key > 0 && (ui.focus || (unsigned char)text[0] > 32)) append(value, size, text);
        else if (key == KEY_ENTER) {
            notice(&ui, "", false);
            int status = login_session(&ui, false, false);
            explicit_bzero(ui.password, sizeof ui.password);
            if (stopping) break;
            if (!ui.interactive) {
                if (terminal_reclaim(&ui) < 0 || terminal_raw(&ui) < 0 ||
                    framebuffer_open(&ui) < 0 || framebuffer_clear(&ui) < 0) { result = EXIT_FAILURE; break; }
                ui.interactive = true;
            }
            notice(&ui, status ? "Could not sign in. Please try again." : "", status != 0);
        }
        explicit_bzero(text, sizeof text);
        if (draw(&ui) < 0) { result = EXIT_FAILURE; break; }
    }
done:
    if (result && !stopping) fprintf(stderr, "green-login: %s\n", strerror(errno));
    explicit_bzero(ui.password, sizeof ui.password);
    terminal_restore(&ui);
    if (ui.fb >= 0) close(ui.fb);
    if (ui.tty >= 0) close(ui.tty);
    return stopping ? EXIT_SUCCESS : result;
}
