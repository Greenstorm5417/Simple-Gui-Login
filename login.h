#ifndef SIMPLE_GUI_LOGIN_H
#define SIMPLE_GUI_LOGIN_H

#include <linux/fb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h>

#define WIDTH 480
#define HEIGHT 380
#define SECRET_SIZE 513

struct config {
	const char *user, *tty, *title, *pam_service, *session_type, *log_path;
	char *session[16];
	uint32_t background, text, muted, accent, card, border, field, field_border, error;
};
extern const struct config config;

struct ui {
	int tty, fb, vt, keyboard_mode;
	bool graphics, interactive, error, active;
	struct termios original;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	char user[65], password[SECRET_SIZE], status[161];
	unsigned focus;
};
extern volatile sig_atomic_t stopping, session_group;

int draw(struct ui *ui);
int preview(struct ui *ui, const char *path);
int framebuffer_open(struct ui *ui);
int framebuffer_clear(struct ui *ui);
int terminal_open(struct ui *ui, const char *path);
void terminal_restore(struct ui *ui);
int terminal_raw(struct ui *ui);
int terminal_detach(struct ui *ui);
int terminal_reclaim(struct ui *ui);
enum {
	KEY_ENTER = -2,
	KEY_TAB = -3,
	KEY_BACKSPACE = -4,
	KEY_CLEAR = -5,
	KEY_ESCAPE = -6,
	KEY_IGNORE = -7
};
int read_key(int fd, char text[5]);
int answer(struct ui *ui, const char *prompt, bool secret, char out[SECRET_SIZE]);
void notice(struct ui *ui, const char *message, bool error);
int login_session(struct ui *ui);

#endif
