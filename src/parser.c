// #define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>

#include "parser.h"
#include "defs.h"

#define MAX_ARG_LEN 4096

static const struct {
	const char *name;
	void (*fn)(void);
} call_table[] = {{"close_window", close_focused},
                  {"decrease_gaps", dec_gaps},
                  {"focus_next", focus_next},
                  {"focus_prev", focus_prev},
                  {"increase_gaps", inc_gaps},
                  {"master_next", move_master_next},
                  {"master_previous", move_master_prev},
                  {"quit", quit},
                  {"reload_config", reload_config},
                  {"master_increase", resize_master_add},
                  {"master_decrease", resize_master_sub},
                  {"toggle_floating", toggle_floating},
                  {"global_floating", toggle_floating_global},
                  {"fullscreen", toggle_fullscreen},
                  {NULL, NULL}};

static void remap_and_dedupe_binds(Config *cfg)
{
	for (int i = 0; i < cfg->bindsn; i++) {
		Binding *b = &cfg->binds[i];
		if (b->mods & (Mod1Mask | Mod4Mask)) {
			unsigned others = b->mods & ~(Mod1Mask | Mod4Mask);
			b->mods = others | cfg->modkey;
		}
	}

	for (int i = 0; i < cfg->bindsn; i++) {
		for (int j = i + 1; j < cfg->bindsn; j++) {
			if (cfg->binds[i].mods == cfg->binds[j].mods && cfg->binds[i].keysym == cfg->binds[j].keysym) {
				memmove(&cfg->binds[j], &cfg->binds[j + 1], sizeof(Binding) * (cfg->bindsn - j - 1));
				cfg->bindsn--;
				j--;
			}
		}
	}
}

static char *strip(char *s)
{
	while (*s && isspace((unsigned char)*s)) {
		s++;
	}
	char *e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e)) {
		*e-- = '\0';
	}
	return s;
}

static char *strip_quotes(char *s)
{
	size_t L = strlen(s);
	if (L > 0 && s[0] == '"') {
		s++;
		L--;
	}
	if (L > 0 && s[L - 1] == '"') {
		s[L - 1] = '\0';
	}
	return s;
}

static Binding *alloc_bind(Config *cfg, unsigned mods, KeySym ks)
{
	for (int i = 0; i < cfg->bindsn; i++) {
		if (cfg->binds[i].mods == (int)mods && cfg->binds[i].keysym == ks) {
			return &cfg->binds[i];
		}
	}
	if (cfg->bindsn >= 256) {
		return NULL;
	}
	Binding *b = &cfg->binds[cfg->bindsn++];
	b->mods = mods;
	b->keysym = ks;
	return b;
}

static unsigned parse_combo(const char *combo, Config *cfg, KeySym *out_ks)
{
	unsigned m = 0;
	KeySym ks = NoSymbol;
	char buf[256];
	strncpy(buf, combo, sizeof buf - 1);
	for (char *p = buf; *p; p++) {
		if (*p == '+' || isspace((unsigned char)*p)) {
			*p = '+';
		}
	}
	buf[sizeof buf - 1] = '\0';
	for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
		for (char *q = tok; *q; q++) {
			*q = tolower((unsigned char)*q);
		}
		if (!strcmp(tok, "mod")) {
			m |= cfg->modkey;
		}
		else if (!strcmp(tok, "shift")) {
			m |= ShiftMask;
		}
		else if (!strcmp(tok, "ctrl")) {
			m |= ControlMask;
		}
		else if (!strcmp(tok, "alt")) {
			m |= Mod1Mask;
		}
		else if (!strcmp(tok, "super")) {
			m |= Mod4Mask;
		}
		else {
			ks = parse_keysym(tok);
		}
	}
	*out_ks = ks;
	return m;
}

int parser(Config *cfg)
{
	char path[PATH_MAX];
	const char *home = getenv("HOME");
	if (!home) {
		fputs("sxwmrc: HOME not set\n", stderr);
		return -1;
	}
	snprintf(path, sizeof path, "%s/.config/sxwmrc", home);
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "sxwmrc: cannot open %s\n", path);
		return -1;
	}

	char line[512];
	int lineno = 0;
	while (fgets(line, sizeof line, f)) {
		lineno++;
		char *s = strip(line);
		if (!*s || *s == '#') {
			continue;
		}

		char *sep = strchr(s, ':');
		if (!sep) {
			fprintf(stderr, "sxwmrc:%d: missing ':'\n", lineno);
			continue;
		}
		*sep = '\0';
		char *key = strip(s);
		char *rest = strip(sep + 1);

		if (!strcmp(key, "mod_key")) {
			unsigned m = parse_mods(rest, cfg);
			if (m & (Mod1Mask | Mod4Mask)) {
				cfg->modkey = m;
			}
			else {
				fprintf(stderr, "sxwmrc:%d: unknown mod_key '%s'\n", lineno, rest);
			}
		}
		else if (!strcmp(key, "gaps")) {
			cfg->gaps = atoi(rest);
		}
		else if (!strcmp(key, "border_width")) {
			cfg->border_width = atoi(rest);
		}
		else if (!strcmp(key, "focused_border_colour")) {
			cfg->border_foc_col = parse_col(rest);
		}
		else if (!strcmp(key, "unfocused_border_colour")) {
			cfg->border_ufoc_col = parse_col(rest);
		}
		else if (!strcmp(key, "swap_border_colour")) {
			cfg->border_swap_col = parse_col(rest);
		}
		else if (!strcmp(key, "master_width")) {
			cfg->master_width = atoi(rest) / 100.0f;
		}
		else if (!strcmp(key, "motion_throttle")) {
			cfg->motion_throttle = atoi(rest);
		}
		else if (!strcmp(key, "resize_master_amount")) {
			cfg->resize_master_amt = atoi(rest);
		}
		else if (!strcmp(key, "snap_distance")) {
			cfg->snap_distance = atoi(rest);
		}
		else if (!strcmp(key, "call") || !strcmp(key, "bind")) {
			char *mid = strchr(rest, ':');
			if (!mid) {
				fprintf(stderr, "sxwmrc:%d: '%s' missing action\n", lineno, key);
				continue;
			}
			*mid = '\0';
			char *combo = strip(rest);
			char *act = strip(mid + 1);

			KeySym ks;
			unsigned mods = parse_combo(combo, cfg, &ks);
			if (ks == NoSymbol) {
				fprintf(stderr, "sxwmrc:%d: bad key in '%s'\n", lineno, combo);
				continue;
			}
			Binding *b = alloc_bind(cfg, mods, ks);
			if (!b) {
				fputs("sxwm: too many binds\n", stderr);
				break;
			}

			if (*act == '"' && !strcmp(key, "bind")) {
				b->type = TYPE_CMD;
				b->action.cmd = build_argv(strip_quotes(act));
			}
			else {
				b->type = TYPE_FUNC;
				Bool found = False;
				for (int i = 0; call_table[i].name; i++) {
					if (!strcmp(act, call_table[i].name)) {
						b->action.fn = call_table[i].fn;
						found = True;
						break;
					}
				}
				if (!found) {
					fprintf(stderr, "sxwmrc:%d: unknown function '%s'\n", lineno, act);
				}
			}
		}
		else if (!strcmp(key, "workspace")) {
			char *mid = strchr(rest, ':');
			if (!mid) {
				fprintf(stderr, "sxwmrc:%d: workspace missing action\n", lineno);
				continue;
			}
			*mid = '\0';
			char *combo = strip(rest);
			char *act = strip(mid + 1);

			KeySym ks;
			unsigned mods = parse_combo(combo, cfg, &ks);
			if (ks == NoSymbol) {
				fprintf(stderr, "sxwmrc:%d: bad key in '%s'\n", lineno, combo);
				continue;
			}
			Binding *b = alloc_bind(cfg, mods, ks);
			if (!b) {
				fputs("sxwm: too many binds\n", stderr);
				break;
			}

			int n;
			if (sscanf(act, "move %d", &n) == 1 && n >= 1 && n <= NUM_WORKSPACES) {
				b->type = TYPE_CWKSP;
				b->action.ws = n - 1;
			}
			else if (sscanf(act, "swap %d", &n) == 1 && n >= 1 && n <= NUM_WORKSPACES) {
				b->type = TYPE_MWKSP;
				b->action.ws = n - 1;
			}
			else {
				fprintf(stderr, "sxwmrc:%d: invalid workspace action '%s'\n", lineno, act);
			}
		}
		else {
			fprintf(stderr, "sxwmrc:%d: unknown option '%s'\n", lineno, key);
		}
	}

	fclose(f);
	remap_and_dedupe_binds(cfg);
	return 0;
}

int parse_mods(const char *mods, Config *cfg)
{
	KeySym dummy;
	return parse_combo(mods, cfg, &dummy);
}

KeySym parse_keysym(const char *key)
{
	KeySym ks = XStringToKeysym(key);
	if (ks != NoSymbol) {
		return ks;
	}

	char buf[64];
	size_t n = strlen(key);
	if (n >= sizeof buf) {
		n = sizeof buf - 1;
	}

	buf[0] = toupper((unsigned char)key[0]);
	for (size_t i = 1; i < n; i++) {
		buf[i] = tolower((unsigned char)key[i]);
	}
	buf[n] = '\0';
	ks = XStringToKeysym(buf);
	if (ks != NoSymbol) {
		return ks;
	}

	for (size_t i = 0; i < n; i++) {
		buf[i] = toupper((unsigned char)key[i]);
	}
	buf[n] = '\0';
	ks = XStringToKeysym(buf);
	if (ks != NoSymbol) {
		return ks;
	}

	fprintf(stderr, "sxwmrc: unknown keysym '%s'\n", key);
	return NoSymbol;
}

const char **build_argv(const char *cmd)
{
	char *dup = strdup(cmd);
	char *saveptr = NULL;
	const char **argv = malloc(MAX_ARGS * sizeof(*argv));
	int i = 0;

	char *tok = strtok_r(dup, " \t", &saveptr);
	while (tok && i < MAX_ARGS - 1) {
		if (*tok == '"') {
			char *end = tok + strlen(tok) - 1;
			if (*end == '"') {
				*end = '\0';
				char *arg = malloc(MAX_ARG_LEN);
				strlcpy(arg, tok + 1, MAX_ARG_LEN);
				argv[i++] = arg;
			}
			else {
				char *quoted = malloc(MAX_ARG_LEN);
				quoted[0] = '\0';
				strlcpy(quoted, tok + 1, MAX_ARG_LEN);
				size_t len = strlen(quoted);

				while ((tok = strtok_r(NULL, " \t", &saveptr)) && *tok != '"' && len < MAX_ARG_LEN - 1) {
					strlcat(quoted, " ", MAX_ARG_LEN);
					strlcat(quoted, tok, MAX_ARG_LEN);
					len = strlen(quoted);
				}
				if (tok && *tok == '"') {
					strlcat(quoted, tok, MAX_ARG_LEN);
				}
				argv[i++] = quoted;
			}
		}
		else {
			char *arg = malloc(MAX_ARG_LEN);
			strlcpy(arg, tok, MAX_ARG_LEN);
			argv[i++] = arg;
		}
		tok = strtok_r(NULL, " \t", &saveptr);
	}
	argv[i] = NULL;
	free(dup);
	return argv;
}
