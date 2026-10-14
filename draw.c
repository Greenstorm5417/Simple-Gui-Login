#include "assets/font.h"
#include "login.h"
#include "logo.h"
#include <errno.h>
#include <linux/kd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct canvas {
	uint32_t pixels[WIDTH];
	int y;
};

static int
rounded(float value)
{
	return (int)(value < 0 ? value - 0.5f : value + 0.5f);
}

static void
pixel(struct canvas *c, int x, int y, uint32_t color, unsigned alpha)
{
	if (x < 0 || x >= WIDTH || y != c->y)
		return;
	uint32_t old = c->pixels[x], mixed = 0;
	for (unsigned shift = 0; shift <= 16; shift += 8)
		mixed |=
		    ((((color >> shift) & 255) * alpha + ((old >> shift) & 255) * (255 - alpha)) /
		     255)
		    << shift;
	c->pixels[x] = mixed;
}

static void
rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t color)
{
	int py = c->y;
	if (py < y || py >= y + h)
		return;
	for (int px = x; px < x + w; px++) {
		int dx = px < x + r ? x + r - px : px >= x + w - r ? px - (x + w - r - 1) : 0;
		int dy = py < y + r ? y + r - py : py >= y + h - r ? py - (y + h - r - 1) : 0;
		if (dx * dx + dy * dy <= r * r)
			pixel(c, px, py, color, 255);
	}
}

static unsigned
next_char(const char **text)
{
	const unsigned char *p = (const unsigned char *)*text;
	unsigned ch = *p++, count = 0;
	if (ch >= 0xc2 && ch <= 0xf4) {
		count = ch < 0xe0 ? 1 : ch < 0xf0 ? 2 : 3;
		ch &= (1u << (6 - count)) - 1;
		for (unsigned i = 0; i < count; i++) {
			if ((*p & 0xc0) != 0x80) {
				*text = (const char *)p;
				return '?';
			}
			ch = (ch << 6) | (*p++ & 63);
		}
	}
	*text = (const char *)p;
	return ch;
}

static const struct glyph *
glyph(unsigned ch, unsigned size)
{
	unsigned index = ch >= 32 && ch <= 126 ? ch - 32
	                 : ch == 0xb7          ? 95
	                 : ch == 0x2022        ? 96
	                                       : '?' - 32;
	return &glyphs[(size == 12 ? 0 : size == 17 ? 1 : 2) * 97 + index];
}

static void
text(struct canvas *c, const char *s, int x, int baseline, unsigned size, uint32_t color, int limit)
{
	float cursor = (float)x;
	while (*s) {
		const struct glyph *g = glyph(next_char(&s), size);
		if (cursor + g->advance > (float)(x + limit))
			break;
		int gx = rounded(cursor) + g->xmin;
		int gy = baseline - g->ymin - (int)g->height;
		int row = c->y - gy;
		if (row >= 0 && (unsigned)row < g->height)
			for (unsigned col = 0; col < g->width; col++)
				pixel(c, gx + (int)col, c->y, color,
				      glyph_pixels[g->offset + (unsigned)row * g->width + col]);
		cursor += g->advance;
	}
}

static void
centered(struct canvas *c, const char *s, int baseline, unsigned size, uint32_t color)
{
	float width = 0;
	for (const char *p = s; *p;)
		width += glyph(next_char(&p), size)->advance;
	text(c, s, rounded((WIDTH - width) / 2), baseline, size, color, WIDTH - 32);
}

static void
field(struct canvas *c, int y, const char *label, const char *value, bool focus)
{
	text(c, label, 58, y, 12, config.muted, 366);
	rect(c, 56, y + 10, 368, 43, 7, focus ? config.accent : config.field_border);
	rect(c, 57, y + 11, 366, 41, 6, config.field);
	text(c, value, 70, y + 39, 17, config.text, 338);
	if (focus && !*value)
		rect(c, 70, y + 20, 1, 20, 0, config.accent);
}

static void
scanline(struct canvas *c, const struct ui *ui, bool caps)
{
	for (unsigned x = 0; x < WIDTH; x++)
		c->pixels[x] = config.background;
	if (c->y >= 10 && c->y < 10 + LOGO_HEIGHT) {
		unsigned y = (unsigned)c->y - 10;
		for (unsigned x = 0; x < LOGO_WIDTH; x++) {
			const unsigned char *p = logo_pixels + (y * LOGO_WIDTH + x) * 4;
			pixel(c, (int)x + (WIDTH - LOGO_WIDTH) / 2, c->y,
			      ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2], p[3]);
		}
	}
	centered(c, config.title, 109, 23, config.text);
	rect(c, 31, 140, 418, 184, 14, config.border);
	rect(c, 32, 141, 416, 182, 13, config.card);
	field(c, 166, "Username", ui->user, ui->focus == 0);
	char masked[28 * 3 + 1] = {0};
	unsigned count = 0;
	for (const char *p = ui->password; *p && count < 28; count++) {
		(void)next_char(&p);
		memcpy(masked + count * 3, "\xe2\x80\xa2", 3);
	}
	field(c, 247, caps ? "Password · Caps Lock" : "Password", masked, ui->focus == 1);
	centered(c, ui->status, 356, 12, ui->error ? config.error : config.muted);
}

static uint32_t
component(struct fb_bitfield f, unsigned value)
{
	return ((value * ((1u << f.length) - 1) + 127) / 255) << f.offset;
}

static void
packed(const struct ui *ui, uint32_t color, unsigned char *out)
{
	uint32_t p = component(ui->var.red, (color >> 16) & 255) |
	             component(ui->var.green, (color >> 8) & 255) |
	             component(ui->var.blue, color & 255) | component(ui->var.transp, 255);
	for (unsigned i = 0; i < ui->var.bits_per_pixel / 8; i++)
		out[i] = (unsigned char)(p >> (i * 8));
}

static int
write_row(struct ui *ui, const void *data, size_t length, unsigned x, unsigned y)
{
	const unsigned char *p = data;
	off_t offset = (off_t)(((uint64_t)y + ui->var.yoffset) * ui->fix.line_length +
	                       ((uint64_t)x + ui->var.xoffset) * (ui->var.bits_per_pixel / 8));
	while (length) {
		ssize_t n = pwrite(ui->fb, p, length, offset);
		if (n < 0 && errno == EINTR && !stopping)
			continue;
		if (n <= 0) {
			if (!n)
				errno = EIO;
			return -1;
		}
		p += n;
		length -= (size_t)n;
		offset += n;
	}
	return 0;
}

int
framebuffer_clear(struct ui *ui)
{
	if (!ui->active)
		return 0;
	unsigned bytes = ui->var.bits_per_pixel / 8;
	unsigned char row[8192 * 4];
	for (unsigned x = 0; x < ui->var.xres; x++)
		packed(ui, config.background, row + x * bytes);
	for (unsigned y = 0; y < ui->var.yres; y++)
		if (write_row(ui, row, ui->var.xres * bytes, 0, y) < 0)
			return -1;
	return 0;
}

int
draw(struct ui *ui)
{
	if (ui->fb < 0 || !ui->active)
		return 0;
	struct canvas c;
	unsigned char row[WIDTH * 4], leds = 0;
	(void)ioctl(ui->tty, KDGKBLED, &leds);
	unsigned ox = (ui->var.xres - WIDTH) / 2, oy = (ui->var.yres - HEIGHT) / 2;
	unsigned bytes = ui->var.bits_per_pixel / 8;
	for (c.y = 0; c.y < HEIGHT; c.y++) {
		scanline(&c, ui, (leds & LED_CAP) != 0);
		for (unsigned x = 0; x < WIDTH; x++)
			packed(ui, c.pixels[x], row + x * bytes);
		if (write_row(ui, row, WIDTH * bytes, ox, oy + (unsigned)c.y) < 0)
			return -1;
	}
	return 0;
}

int
preview(struct ui *ui, const char *path)
{
	FILE *out = fopen(path, "wb");
	if (!out)
		return -1;
	bool failed = fprintf(out, "P6\n1280 800\n255\n") < 0;
	struct canvas c;
	unsigned char row[1280 * 3];
	for (int y = 0; y < 800 && !failed; y++) {
		c.y = y - (800 - HEIGHT) / 2;
		if (c.y >= 0 && c.y < HEIGHT)
			scanline(&c, ui, false);
		for (int x = 0; x < 1280; x++) {
			uint32_t color = x >= (1280 - WIDTH) / 2 && x < (1280 + WIDTH) / 2 &&
			                         c.y >= 0 && c.y < HEIGHT
			                     ? c.pixels[x - (1280 - WIDTH) / 2]
			                     : config.background;
			row[x * 3] = (unsigned char)(color >> 16);
			row[x * 3 + 1] = (unsigned char)(color >> 8);
			row[x * 3 + 2] = (unsigned char)color;
		}
		failed = fwrite(row, 1, sizeof row, out) != sizeof row;
	}
	if (fclose(out))
		failed = true;
	return failed ? -1 : 0;
}
