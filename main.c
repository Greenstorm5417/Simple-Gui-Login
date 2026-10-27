#include "login.h"
#include "config.h"

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

volatile sig_atomic_t session_group, stopping;

static void stop(int signum);

static void append(char* buffer, size_t size, const char* text) {
    size_t length = strlen(buffer), more = strlen(text);
    if (length + more < size) {
        memcpy(buffer + length, text, more + 1);
    }
}

static void backspace(char* value) {
    size_t n = strlen(value);
    if (!n) {
        return;
    }

    do {
        n--;
    } while (n && ((unsigned char) value[n] & 0xc0) == 0x80);

    explicit_bzero(value + n, strlen(value + n));
}

static int init_signals(void) {
    struct sigaction sa = { .sa_handler = stop, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        return -1;
    }

    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        return -1;
    }

    sa.sa_handler = SIG_IGN;

    if (sigaction(SIGTTOU, &sa, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int log_open(void) {
    int fd = open(config.log_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        return -1;
    }

    if (!S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_nlink != 1) {
        errno = EPERM;
        return -1;
    }

    if (fchmod(fd, 0600) < 0) {
        return -1;
    }
    if (dup2(fd, STDERR_FILENO) < 0) {
        return -1;
    }
    if (fcntl(STDERR_FILENO, F_SETFD, 0) < 0) {
        return -1;
    }

    if (fd != STDERR_FILENO) {
        close(fd);
    }

    return 0;
}

static void stop(int signum) {
    (void) signum;

    stopping = 1;
    if (session_group > 0) {
        kill(-session_group, SIGTERM);
    }
}

int answer(struct ui* ui, const char* prompt, bool secret, char out[SECRET_SIZE]) {
    if (!ui->interactive) {
        return -1;
    }

    explicit_bzero(out, SECRET_SIZE);
    explicit_bzero(ui->password, sizeof ui->password);

    notice(ui, prompt, false);

    while (!stopping) {
        char text[5];

        int key = read_key(ui->tty, text);
        if (key == -1) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }

        if (key == KEY_ENTER) {
            explicit_bzero(ui->password, sizeof ui->password);
            return 0;
        }

        if (key == KEY_ESCAPE) {
            break;
        }

        if (key == KEY_CLEAR) {
            explicit_bzero(out, SECRET_SIZE);
        } else if (key == KEY_BACKSPACE) {
            backspace(out);
        } else if (key > 0) {
            append(out, SECRET_SIZE, text);
        }

        if (secret) {
            snprintf(ui->password, sizeof ui->password, "%s", out);
        } else {
            snprintf(ui->status, sizeof ui->status, "%.160s", out);
        }

        explicit_bzero(text, sizeof text);

        if (draw(ui) < 0) {
            break;
        }
    }

    explicit_bzero(out, SECRET_SIZE);
    explicit_bzero(ui->password, sizeof ui->password);
    return -1;
}

void notice(struct ui* ui, const char* message, bool error) {
    snprintf(ui->status, sizeof ui->status, "%s", message);

    for (char *p = ui->status; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }

    ui->error = error;

    draw(ui);
}

int main(int argc, char* argv[]) {
    struct ui ui = {
        .tty = -1,
        .fb = -1,
        .keyboard_mode = -1,
        .interactive = true,
        .focus = 1,
    };

    const char* tty = config.tty;
    const char* image = NULL;

    snprintf(ui.user, sizeof ui.user, "%s", config.user);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tty") && i + 1 < argc) {
            tty = argv[++i];
        } else if (!strcmp(argv[i], "--preview") && i + 1 < argc) {
            image = argv[++i];
        } else if (!strcmp(argv[i], "--help")) {
            puts("simple-gui-login [--tty /dev/tty1] [--preview FILE.ppm]");
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "simple-gui-login: unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (image) {
        return preview(&ui, image) ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (geteuid() != 0) {
        errx(EXIT_FAILURE, "run as root");
    }

    if (log_open() < 0) {
        err(EXIT_FAILURE, "failed to open log %s", config.log_path);
    }

    fprintf(stderr, "simple-gui-login: starting pid %d, tty %s, PAM %s, session %s\n",
            getpid(), tty, config.pam_service, config.session[0]);

    if (init_signals() < 0) {
        err(EXIT_FAILURE, "failed to initialize signals");
    }

    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
        err(EXIT_FAILURE, "setrlimit(RLIMIT_CORE)");
    }

    int ret = EXIT_SUCCESS;

    if (terminal_open(&ui, tty) < 0) {
        warn("failed to open terminal %s", tty);
        ret = EXIT_FAILURE; 
        goto end;
    }

    if (framebuffer_open(&ui) < 0 || framebuffer_clear(&ui) < 0 || draw(&ui) < 0) {
        warn("failed to initialize framebuffer");
        ret = EXIT_FAILURE; 
        goto end;
    }

    while (!stopping) {
        char text[5];

        int key = read_key(ui.tty, text);
        if (key == -1) {
            if (errno == EINTR) {
                continue;
            }

            ret = EXIT_FAILURE;
            break;
        }

        char* value = ui.focus ? ui.password : ui.user;
        size_t size = ui.focus ? sizeof ui.password : sizeof ui.user;

        if (key == KEY_TAB) {
            ui.focus ^= 1;
        } else if (key == KEY_CLEAR || key == KEY_ESCAPE) {
            explicit_bzero(value, size);
        } else if (key == KEY_BACKSPACE) {
            backspace(value);
        } else if (key > 0 && (ui.focus || (unsigned char)text[0] > 32)) {
            append(value, size, text);
        } else if (key == KEY_ENTER) {
            notice(&ui, "", false);

            int status = login_session(&ui);

            explicit_bzero(ui.password, sizeof ui.password);

            if (stopping) {
                break;
            }

            if (!ui.interactive) {
                if (terminal_reclaim(&ui) < 0 || terminal_raw(&ui) < 0 || framebuffer_open(&ui) < 0 || framebuffer_clear(&ui) < 0) {
                    ret = EXIT_FAILURE;
                    break;
                }

                ui.interactive = true;
            }

            notice(&ui, status > 0  ? "Session failed to start. Check the service log."
                    : status < 0 ? "Authentication failed. Please try again."
                    : "",
                    status != 0);
        }

        explicit_bzero(text, sizeof text);

        if (draw(&ui) < 0) {
            ret = EXIT_FAILURE;
            break;
        }
    }

    if (ret && !stopping) {
        fprintf(stderr, "simple-gui-login: %s\n", strerror(errno));
    }

end:
    explicit_bzero(ui.password, sizeof ui.password);

    terminal_restore(&ui);

    if (ui.fb >= 0) {
        close(ui.fb);
    }
    if (ui.tty >= 0) {
        close(ui.tty);
    }

    return stopping ? EXIT_SUCCESS : ret;
}
