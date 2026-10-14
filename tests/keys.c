#include "login.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

volatile sig_atomic_t stopping, session_group;

int
main(void)
{
	const struct {
		const char *input;
		int key;
		const char *text;
	} cases[] = {
	    {"a", 1, "a"},
	    {"é", 2, "é"},
	    {"•", 3, "•"},
	    {"😀", 4, "😀"},
	    {"\n", KEY_ENTER, ""},
	    {"\r", KEY_ENTER, ""},
	    {"\t", KEY_TAB, ""},
	    {"\177", KEY_BACKSPACE, ""},
	    {"\025", KEY_CLEAR, ""},
	    {"\033", KEY_ESCAPE, ""},
	    {"\033[Z", KEY_TAB, ""},
	    {"\033[A", KEY_IGNORE, ""},
	    {"\xc0\x80", KEY_IGNORE, ""},
	    {"\xed\xa0\x80", KEY_IGNORE, ""},
	    {"\xf4\x90\x80\x80", KEY_IGNORE, ""},
	    {"", -1, ""},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		int fds[2];
		char text[5];
		size_t length = strlen(cases[i].input);
		if (pipe(fds) || write(fds[1], cases[i].input, length) != (ssize_t)length)
			return 1;
		close(fds[1]);
		int key = read_key(fds[0], text);
		close(fds[0]);
		if (key != cases[i].key || (key > 0 && strcmp(text, cases[i].text))) {
			fprintf(stderr, "key case %zu failed\n", i);
			return 1;
		}
	}
	return 0;
}
