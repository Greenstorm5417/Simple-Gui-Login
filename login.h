#ifndef SIMPLE_GUI_LOGIN_H
#define SIMPLE_GUI_LOGIN_H

#include <linux/fb.h>

#include <signal.h>
#include <stdint.h>
#include <termios.h>

#define WIDTH   480
#define HEIGHT  380

#define SECRET_SIZE 513

enum key {
    KEY_ENTER = -2,
    KEY_TAB = -3,
    KEY_BACKSPACE = -4,
    KEY_CLEAR = -5,
    KEY_ESCAPE = -6,
    KEY_IGNORE = -7
};

struct config {
    const char* user;
    const char* tty;
    const char* title;
    const char* pam_service;
    const char* session_type;
    const char* log_path;
    char* session[16];
    uint32_t background;
    uint32_t text;
    uint32_t muted;
    uint32_t accent;
    uint32_t card;
    uint32_t border;
    uint32_t field;
    uint32_t field_border;
    uint32_t error;
};

extern const struct config config;

struct ui {
    int tty;
    int fb;
    int vt;
    int keyboard_mode;
    bool graphics, interactive, error, active;
    struct termios original;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    char user[65];
    char password[SECRET_SIZE];
    char status[161];
    unsigned focus;
};

extern volatile sig_atomic_t stopping, session_group;

int framebuffer_open(struct ui* ui);
int framebuffer_clear(struct ui* ui);

int terminal_open(struct ui* ui, const char* path);
void terminal_restore(struct ui* ui);
int terminal_raw(struct ui* ui);
int terminal_detach(struct ui* ui);
int terminal_reclaim(struct ui* ui);

int answer(struct ui* ui, const char* prompt, bool secret, char out[SECRET_SIZE]);
int draw(struct ui* ui);
int login_session(struct ui* ui);
void notice(struct ui* ui, const char* message, bool error);
int preview(struct ui* ui, const char* path);
int read_key(int fd, char text[5]);

#endif
