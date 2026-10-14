#include "login.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <unistd.h>

static int vt_events = -1;
static struct ui *vt_ui;

static int
terminal_events(void)
{
	struct signalfd_siginfo event;
	if (read(vt_events, &event, sizeof event) != sizeof event)
		return -1;
	if (event.ssi_signo == SIGUSR1) {
		vt_ui->active = false;
		return ioctl(vt_ui->tty, VT_RELDISP, 1);
	}
	if (ioctl(vt_ui->tty, VT_RELDISP, VT_ACKACQ) < 0)
		return -1;
	vt_ui->active = true;
	if (framebuffer_clear(vt_ui) < 0)
		return -1;
	return draw(vt_ui);
}

static int
terminal_unwatch(struct ui *ui)
{
	struct vt_mode mode = {.mode = VT_AUTO};
	if (ioctl(ui->tty, VT_SETMODE, &mode) < 0)
		return -1;
	if (vt_events >= 0) {
		close(vt_events);
		vt_events = -1;
	}
	vt_ui = NULL;
	return 0;
}

int
terminal_raw(struct ui *ui)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
		return -1;
	vt_events = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	if (vt_events < 0)
		return -1;
	vt_ui = ui;
	struct vt_mode mode = {.mode = VT_PROCESS, .relsig = SIGUSR1, .acqsig = SIGUSR2};
	if (ioctl(ui->tty, VT_SETMODE, &mode) < 0)
		return -1;
	while (ioctl(ui->tty, VT_WAITACTIVE, ui->vt) < 0) {
		if (errno != EINTR || stopping)
			return -1;
	}
	ui->active = true;
	struct termios raw = ui->original;
	cfmakeraw(&raw);
	if (ioctl(ui->tty, KDSKBMODE, K_UNICODE) < 0 || tcsetattr(ui->tty, TCSANOW, &raw) < 0)
		return -1;
	if (ioctl(ui->tty, KDSETMODE, KD_GRAPHICS) < 0)
		return -1;
	ui->graphics = true;
	return 0;
}

void
terminal_restore(struct ui *ui)
{
	if (ui->tty < 0)
		return;
	char path[32];
	snprintf(path, sizeof path, "/dev/tty%d", ui->vt);
	int fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd >= 0) {
		close(ui->tty);
		ui->tty = fd;
	}
	(void)terminal_unwatch(ui);
	if (ui->graphics) {
		if (ioctl(ui->tty, KDSETMODE, KD_TEXT) == 0)
			ui->graphics = false;
	}
	if (ui->keyboard_mode >= 0)
		(void)ioctl(ui->tty, KDSKBMODE, ui->keyboard_mode);
	(void)tcsetattr(ui->tty, TCSANOW, &ui->original);
}

int
terminal_open(struct ui *ui, const char *path)
{
	char *end;
	long vt = 0;
	if (strncmp(path, "/dev/tty", 8)) {
		errno = EINVAL;
		return -1;
	}
	vt = strtol(path + 8, &end, 10);
	if (end == path + 8 || *end || vt < 1 || vt > 63) {
		errno = EINVAL;
		return -1;
	}
	ui->tty = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (ui->tty < 0)
		return -1;
	if (tcgetattr(ui->tty, &ui->original) < 0 ||
	    ioctl(ui->tty, KDGKBMODE, &ui->keyboard_mode) < 0) {
		close(ui->tty);
		ui->tty = -1;
		return -1;
	}
	ui->vt = (int)vt;
	if (getsid(0) != getpid() && setsid() < 0)
		return -1;
	if (ioctl(ui->tty, TIOCSCTTY, 0) < 0)
		return -1;
	while (ioctl(ui->tty, VT_WAITACTIVE, ui->vt) < 0) {
		if (errno != EINTR || stopping)
			return -1;
	}
	return terminal_raw(ui);
}

int
terminal_detach(struct ui *ui)
{
	if (terminal_unwatch(ui) < 0 || ioctl(ui->tty, KDSKBMODE, ui->keyboard_mode) < 0)
		return -1;
	struct sigaction ignore = {.sa_handler = SIG_IGN}, previous;
	sigemptyset(&ignore.sa_mask);
	if (sigaction(SIGHUP, &ignore, &previous) < 0)
		return -1;
	int result = ioctl(ui->tty, TIOCNOTTY, 0), saved = errno;
	(void)sigaction(SIGHUP, &previous, NULL);
	errno = saved;
	return result;
}

int
terminal_reclaim(struct ui *ui)
{
	char path[32];
	snprintf(path, sizeof path, "/dev/tty%d", ui->vt);
	int fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ioctl(fd, TIOCSCTTY, 0) < 0) {
		close(fd);
		return -1;
	}
	close(ui->tty);
	ui->tty = fd;
	return 0;
}

static int
byte(int fd, int timeout)
{
	struct pollfd p[2] = {{.fd = fd, .events = POLLIN}, {.fd = vt_events, .events = POLLIN}};
	unsigned char ch;
	for (;;) {
		p[0].fd = vt_ui && !vt_ui->active ? -1 : fd;
		int n = poll(p, 2, timeout);
		if (!n)
			return KEY_IGNORE;
		if (n < 0)
			return -1;
		if (p[1].revents & POLLIN) {
			if (terminal_events() < 0)
				return -1;
			continue;
		}
		if (vt_ui && !vt_ui->active)
			continue;
		if (read(fd, &ch, 1) != 1) {
			errno = EIO;
			return -1;
		}
		break;
	}
	return ch;
}

int
read_key(int fd, char text[5])
{
	int first = byte(fd, -1);
	memset(text, 0, 5);
	if (first < 0)
		return first;
	switch (first) {
	case 10:
	case 13:
		return KEY_ENTER;
	case 9:
		return KEY_TAB;
	case 8:
	case 127:
		return KEY_BACKSPACE;
	case 21:
		return KEY_CLEAR;
	case 3:
		return KEY_ESCAPE;
	case 27: {
		char sequence[9] = {0};
		unsigned n = 0;
		int ch;
		while (n < 8 && (ch = byte(fd, 8)) >= 0)
			sequence[n++] = (char)ch;
		if (!strcmp(sequence, "[Z"))
			return KEY_TAB;
		return n ? KEY_IGNORE : KEY_ESCAPE;
	}
	default:
		break;
	}
	if (first >= 32 && first <= 126) {
		text[0] = (char)first;
		return 1;
	}
	if (first < 0xc2 || first > 0xf4)
		return KEY_IGNORE;
	unsigned len = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
	text[0] = (char)first;
	for (unsigned i = 1; i < len; i++) {
		int ch = byte(fd, 50);
		if (ch < 0x80 || ch > 0xbf)
			return KEY_IGNORE;
		text[i] = (char)ch;
	}
	unsigned second = (unsigned char)text[1];
	if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
	    (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
		return KEY_IGNORE;
	return (int)len;
}

static bool
valid_channel(struct fb_bitfield f, unsigned bpp, bool alpha)
{
	return f.length <= 8 && (alpha || f.length) && !f.msb_right && f.offset <= bpp &&
	       f.length <= bpp - f.offset;
}

int
framebuffer_open(struct ui *ui)
{
	ui->fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (ui->fb < 0)
		return -1;
	if (ioctl(ui->fb, FBIOGET_VSCREENINFO, &ui->var) < 0 ||
	    ioctl(ui->fb, FBIOGET_FSCREENINFO, &ui->fix) < 0)
		goto fail;
	struct fb_var_screeninfo *v = &ui->var;
	struct fb_fix_screeninfo *f = &ui->fix;
	if (v->xres < WIDTH || v->yres < HEIGHT || v->xres > 8192 || v->yres > 8192 ||
	    (v->bits_per_pixel != 16 && v->bits_per_pixel != 24 && v->bits_per_pixel != 32) ||
	    f->type != FB_TYPE_PACKED_PIXELS || f->visual != FB_VISUAL_TRUECOLOR || v->nonstd ||
	    v->rotate || !valid_channel(v->red, v->bits_per_pixel, false) ||
	    !valid_channel(v->green, v->bits_per_pixel, false) ||
	    !valid_channel(v->blue, v->bits_per_pixel, false) ||
	    !valid_channel(v->transp, v->bits_per_pixel, true) ||
	    ((uint64_t)v->yoffset + v->yres) * f->line_length > f->smem_len ||
	    ((uint64_t)v->xoffset + v->xres) * (v->bits_per_pixel / 8) > f->line_length) {
		errno = EINVAL;
		goto fail;
	}
	return 0;
fail:
	close(ui->fb);
	ui->fb = -1;
	return -1;
}
