#include "login.h"
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct conversation {
	struct ui *ui;
	bool seeded;
};

static void
free_responses(struct pam_response *responses, int count)
{
	for (int i = 0; i < count; i++) {
		if (responses[i].resp) {
			explicit_bzero(responses[i].resp, strlen(responses[i].resp));
			free(responses[i].resp);
		}
	}
	free(responses);
}

static int
converse(int count, const struct pam_message **messages, struct pam_response **out, void *data)
{
	if (count < 1 || count > 32 || !messages || !out || !data)
		return PAM_CONV_ERR;
	struct conversation *conversation = data;
	struct ui *ui = conversation->ui;
	*out = NULL;
	struct pam_response *responses = calloc((size_t)count, sizeof *responses);
	if (!responses)
		return PAM_BUF_ERR;
	for (int i = 0; i < count; i++) {
		char answer_text[SECRET_SIZE] = {0};
		if (!messages[i])
			goto fail;
		const char *prompt = messages[i]->msg ? messages[i]->msg : "";
		switch (messages[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			if (conversation->seeded) {
				memcpy(answer_text, ui->password, sizeof answer_text);
				explicit_bzero(ui->password, sizeof ui->password);
				conversation->seeded = false;
			} else if (answer(ui, prompt, true, answer_text))
				goto fail;
			break;
		case PAM_PROMPT_ECHO_ON:
			if (answer(ui, prompt, false, answer_text))
				goto fail;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			notice(ui, prompt, messages[i]->msg_style == PAM_ERROR_MSG);
			continue;
		default:
			goto fail;
		}
		responses[i].resp = strdup(answer_text);
		explicit_bzero(answer_text, sizeof answer_text);
		if (!responses[i].resp)
			goto fail;
	}
	*out = responses;
	return PAM_SUCCESS;
fail:
	free_responses(responses, count);
	return PAM_CONV_ERR;
}

static int
wait_child(pid_t child, int *status)
{
	while (waitpid(child, status, 0) < 0)
		if (errno != EINTR)
			return -1;
	return 0;
}

static int
session_environment(pam_handle_t *pam, const struct passwd *user)
{
	char runtime[PATH_MAX];
	const char *directory = pam_getenv(pam, "XDG_RUNTIME_DIR");
	if (directory) {
		if (directory[0] != '/' ||
		    snprintf(runtime, sizeof runtime, "%s", directory) >= (int)sizeof runtime) {
			errno = EINVAL;
			return -1;
		}
	} else {
		snprintf(runtime, sizeof runtime, "/run/user/%lu", (unsigned long)user->pw_uid);
	}
	struct stat info;
	if (lstat(runtime, &info) < 0) {
		fprintf(stderr, "simple-gui-login: runtime directory %s: %s; check PAM session setup\n",
		        runtime, strerror(errno));
		return -1;
	}
	if (!S_ISDIR(info.st_mode) || info.st_uid != user->pw_uid ||
	    (info.st_mode & 0777) != 0700) {
		fprintf(stderr,
		        "simple-gui-login: runtime directory %s needs user ownership and mode 0700\n",
		        runtime);
		errno = EPERM;
		return -1;
	}
	char **environment = pam_getenvlist(pam);
	if (clearenv())
		return -1;
	if (environment) {
		for (char **p = environment; *p; p++) {
			char *equals = strchr(*p, '=');
			if (equals) {
				*equals = 0;
				if (setenv(*p, equals + 1, 1))
					_exit(126);
			}
			free(*p);
		}
		free(environment);
	}
	if (setenv("XDG_RUNTIME_DIR", runtime, 1) || setenv("HOME", user->pw_dir, 1) ||
	    setenv("USER", user->pw_name, 1) || setenv("LOGNAME", user->pw_name, 1) ||
	    setenv("SHELL", user->pw_shell, 1) ||
	    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin", 1) ||
	    setenv("TERM", "linux", 1))
		return -1;
	return 0;
}

static int
worker(pam_handle_t *pam, struct ui *ui, struct passwd *user, gid_t *groups, int group_count)
{
	int status = PAM_SYSTEM_ERR, child_status = 0;
	const char *step = "session TTY";
	bool credentials = false, opened = false;
	if (setsid() < 0 || terminal_reclaim(ui) < 0) {
		fprintf(stderr, "simple-gui-login: session TTY: %s\n", strerror(errno));
		goto done;
	}
	char vt[32], type[64];
	step = "PAM session environment";
	if (snprintf(type, sizeof type, "XDG_SESSION_TYPE=%s", config.session_type) >=
	    (int)sizeof type)
		goto done;
	snprintf(vt, sizeof vt, "XDG_VTNR=%d", ui->vt);
	if ((status = pam_putenv(pam, type)) != PAM_SUCCESS ||
	    (status = pam_putenv(pam, "XDG_SESSION_CLASS=user")) != PAM_SUCCESS ||
	    (status = pam_putenv(pam, "XDG_SEAT=seat0")) != PAM_SUCCESS ||
	    (status = pam_putenv(pam, vt)) != PAM_SUCCESS)
		goto done;
	step = "pam_setcred";
	if ((status = pam_setcred(pam, PAM_ESTABLISH_CRED)) != PAM_SUCCESS)
		goto done;
	credentials = true;
	step = "pam_open_session";
	if ((status = pam_open_session(pam, 0)) != PAM_SUCCESS)
		goto done;
	opened = true;
	fprintf(stderr, "simple-gui-login: PAM session opened for uid %lu, launching %s\n",
	        (unsigned long)user->pw_uid, config.session[0]);
	status = PAM_SYSTEM_ERR;
	step = "session fork";
	pid_t child = fork();
	if (child < 0) {
		fprintf(stderr, "simple-gui-login: session fork: %s\n", strerror(errno));
		goto done;
	}
	if (!child) {
		if (setpgid(0, 0) || session_environment(pam, user) ||
		    setgroups((size_t)group_count, groups) ||
		    setresgid(user->pw_gid, user->pw_gid, user->pw_gid) ||
		    setresuid(user->pw_uid, user->pw_uid, user->pw_uid) || chdir(user->pw_dir)) {
			fprintf(stderr, "simple-gui-login: session environment or identity: %s\n",
			        strerror(errno));
			_exit(126);
		}
		for (int fd = 0; fd < 2; fd++)
			if (dup2(ui->tty, fd) < 0)
				_exit(126);
		struct sigaction action = {.sa_handler = SIG_DFL};
		sigemptyset(&action.sa_mask);
		if (sigprocmask(SIG_SETMASK, &action.sa_mask, NULL))
			_exit(126);
		int signals[] = {SIGTERM, SIGINT, SIGHUP, SIGTTOU, SIGTTIN};
		for (unsigned i = 0; i < sizeof signals / sizeof *signals; i++)
			if (sigaction(signals[i], &action, NULL))
				_exit(126);
		if (close_range(3, ~0u, 0))
			_exit(126);
		execv(config.session[0], config.session);
		fprintf(stderr, "simple-gui-login: exec %s: %s\n", config.session[0], strerror(errno));
		_exit(127);
	}
	session_group = child;
	step = "desktop process";
	(void)tcsetpgrp(ui->tty, child);
	(void)kill(-child, SIGCONT);
	int waited = wait_child(child, &child_status);
	if (waited < 0)
		fprintf(stderr, "simple-gui-login: wait desktop: %s\n", strerror(errno));
	else if (WIFEXITED(child_status))
		fprintf(stderr, "simple-gui-login: desktop exited with status %d\n", WEXITSTATUS(child_status));
	else if (WIFSIGNALED(child_status))
		fprintf(stderr, "simple-gui-login: desktop killed by signal %d\n", WTERMSIG(child_status));
	if (!waited && WIFEXITED(child_status) &&
	    !WEXITSTATUS(child_status))
		status = PAM_SUCCESS;
	session_group = 0;
done:
	if (status != PAM_SUCCESS && !stopping)
		fprintf(stderr, "simple-gui-login: %s failed: %s (PAM status %d)\n",
		        step, pam_strerror(pam, status), status);
	if (opened)
		(void)pam_close_session(pam, 0);
	if (credentials)
		(void)pam_setcred(pam, PAM_DELETE_CRED);
	(void)pam_end(pam, status);
	return status == PAM_SUCCESS ? 0 : 1;
}

int
login_session(struct ui *ui)
{
	struct conversation conversation = {.ui = ui, .seeded = true};
	struct pam_conv conv = {.conv = converse, .appdata_ptr = &conversation};
	pam_handle_t *pam = NULL;
	int status = pam_start(config.pam_service, ui->user, &conv, &pam);
	if (status != PAM_SUCCESS) {
		fprintf(stderr, "simple-gui-login: pam_start %s: %s (PAM status %d)\n",
		        config.pam_service, pam_strerror(pam, status), status);
		errno = EIO;
		return -1;
	}
	char tty[32];
	snprintf(tty, sizeof tty, "/dev/tty%d", ui->vt);
	status = pam_set_item(pam, PAM_TTY, tty);
	if (status == PAM_SUCCESS)
		status = pam_authenticate(pam, PAM_DISALLOW_NULL_AUTHTOK);
	if (status == PAM_SUCCESS) {
		status = pam_acct_mgmt(pam, 0);
		if (status == PAM_NEW_AUTHTOK_REQD)
			status = pam_chauthtok(pam, PAM_CHANGE_EXPIRED_AUTHTOK);
	}
	explicit_bzero(ui->password, sizeof ui->password);
	if (status != PAM_SUCCESS || stopping) {
		if (status != PAM_SUCCESS && !stopping)
			fprintf(stderr, "simple-gui-login: authentication: %s\n",
			        pam_strerror(pam, status));
		(void)pam_end(pam, status);
		if (status != PAM_SUCCESS)
			errno = EACCES;
		return -1;
	}
	const void *pam_user = NULL;
	const char *step = "account lookup";
	struct passwd user, *found;
	char account[16384];
	int group_count = 0;
	gid_t *groups = NULL;
	if (pam_get_item(pam, PAM_USER, &pam_user) != PAM_SUCCESS || !pam_user ||
	    getpwnam_r(pam_user, &user, account, sizeof account, &found) || !found)
		goto fail;
	fprintf(stderr, "simple-gui-login: authenticated uid %lu\n", (unsigned long)user.pw_uid);
	step = "group lookup";
	(void)getgrouplist(user.pw_name, user.pw_gid, NULL, &group_count);
	if (group_count < 1 || group_count > 4096)
		goto fail;
	groups = calloc((size_t)group_count, sizeof *groups);
	if (!groups || getgrouplist(user.pw_name, user.pw_gid, groups, &group_count) < 0)
		goto fail;
	step = "TTY handoff";
	if (tcsetattr(ui->tty, TCSANOW, &ui->original) < 0 || terminal_detach(ui) < 0)
		goto fail;
	ui->interactive = false;
	if (ui->fb >= 0) {
		close(ui->fb);
		ui->fb = -1;
	}
	step = "PAM worker fork";
	pid_t child = fork();
	if (child < 0)
		goto fail;
	if (!child)
		_exit(worker(pam, ui, &user, groups, group_count));
	free(groups);
	(void)pam_end(pam, PAM_SUCCESS);
	session_group = child;
	int child_status = 0;
	int waited = wait_child(child, &child_status);
	session_group = 0;
	if (!waited && WIFEXITED(child_status) && !WEXITSTATUS(child_status))
		return 0;
	errno = EIO;
	return 1;
fail:
	fprintf(stderr, "simple-gui-login: %s failed: %s\n", step, strerror(errno));
	free(groups);
	(void)pam_end(pam, PAM_SYSTEM_ERR);
	errno = EACCES;
	return 1;
}
