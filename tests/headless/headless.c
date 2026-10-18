/*
 * Headless test host for !PCSX: host.c and the core, with the plat_*
 * calls replaced by a script. Built for arm-linux-gnueabihf and run under
 * qemu-arm-aligntrap (alignment traps on, like RISC OS), so the core,
 * the dynarec's output and host.c run as they will on the Pi, minus the
 * Wimp.
 *
 *   headless GAME FRAMES OUT.ppm [key=value ...]
 *
 * key=value pairs go into Choices (core options or frontend settings).
 * Special keys:
 *   press=FRAME:KEY:LEN   hold KEY (a Choices key name) from FRAME for LEN
 *                         frames (repeatable)
 *   joy=FRAME:BUTTON:LEN  hold joystick button BUTTON
 *   hat=FRAME:X,Y:LEN     joystick hat
 *   stick=FRAME:X,Y:LEN   joystick left stick (-32767..32767)
 *   shot=FRAME:FILE.ppm   also write the picture after FRAME frames
 *   savestate=FRAME:SLOT, loadstate=FRAME:SLOT
 * Prints: frames, picture size and checksum, sound statistics, and each
 * frame's pad bits when padlog=1.
 *
 * Copyright (C) 2026 Andrew Youll. GPL-2.0-or-later, as PCSX ReARMed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include "rofe.h"

static int frame_no, padlog;
static unsigned fake_cs;

#define MAX_EV 64
struct ev { int kind, from, len, code, x, y; char file[256]; };
static struct ev evs[MAX_EV];
static int nev;
enum { EV_KEY, EV_JOY, EV_HAT, EV_STICK, EV_SHOT, EV_SAVE, EV_LOAD };

int plat_key_down(int inkey)
{
	int i;
	for (i = 0; i < nev; i++)
		if (evs[i].kind == EV_KEY && evs[i].code == inkey &&
		    frame_no >= evs[i].from && frame_no < evs[i].from + evs[i].len)
			return 1;
	return 0;
}

unsigned plat_time_cs(void)
{
	return fake_cs;
}

void plat_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static long long audio_bytes, audio_nonzero;
static double audio_sumsq_l, audio_sumsq_r;
static int audio_is_open;

int plat_audio_open(int rate, int block)
{
	fprintf(stderr, "test: audio open %d Hz, block %d\n", rate, block);
	audio_is_open = 1;
	return 1;
}
void plat_audio_close(void) { audio_is_open = 0; }
int plat_audio_write(const void *data, int bytes)
{
	const short *s = data;
	int i;
	for (i = 0; i + 1 < bytes / 2; i += 2) {
		if (s[i] || s[i + 1])
			audio_nonzero++;
		audio_sumsq_l += (double)s[i] * s[i];
		audio_sumsq_r += (double)s[i + 1] * s[i + 1];
	}
	audio_bytes += bytes;
	return bytes;
}
int plat_audio_queued(void) { return 0; }
void plat_audio_pause(int p) { (void)p; }

int plat_joy_poll(struct joy_state *js, int max)
{
	int i, any = 0;
	if (max < 1)
		return 0;
	memset(js, 0, sizeof *js);
	for (i = 0; i < nev; i++) {
		struct ev *e = &evs[i];
		int on = frame_no >= e->from && frame_no < e->from + e->len;
		if (e->kind == EV_JOY || e->kind == EV_HAT || e->kind == EV_STICK)
			any = 1;
		if (!on)
			continue;
		if (e->kind == EV_JOY)
			js->buttons |= 1u << e->code;
		else if (e->kind == EV_HAT) {
			js->has_hat = 1;
			js->hat_x = e->x;
			js->hat_y = e->y;
		} else if (e->kind == EV_STICK) {
			js->x = e->x;
			js->y = e->y;
		}
	}
	for (i = 0; i < nev; i++)
		if (evs[i].kind == EV_HAT)
			js->has_hat = 1;
	js->present = any;
	return any;
}

/* the "sprite" */
static unsigned char fbuf[1024 * 512 * 4];
static void *fb(int w, int h, int *pitch)
{
	if (w > 1024 || h > 512)
		return NULL;
	*pitch = w * 4;
	return fbuf;
}

static unsigned checksum(const struct host_video *v)
{
	unsigned c = 2166136261u;
	int x, y;
	for (y = 0; y < v->height; y++) {
		const unsigned char *row = (const unsigned char *)v->buf + y * v->pitch;
		for (x = 0; x < v->width * 4; x++)
			c = (c ^ row[x]) * 16777619u;
	}
	return c;
}

static void write_ppm(const char *path, const struct host_video *v)
{
	FILE *f = fopen(path, "wb");
	int x, y;
	if (!f) {
		perror(path);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", v->width, v->height);
	for (y = 0; y < v->height; y++) {
		const unsigned char *row = (const unsigned char *)v->buf + y * v->pitch;
		for (x = 0; x < v->width; x++)
			fwrite(row + x * 4, 1, 3, f);   /* TBGR in memory = R, G, B, 0 */
	}
	fclose(f);
}

static void add_event(const char *k, const char *v)
{
	struct ev *e = &evs[nev];
	char name[64];
	if (nev == MAX_EV)
		return;
	memset(e, 0, sizeof *e);
	if (strcmp(k, "press") == 0) {
		e->kind = EV_KEY;
		if (sscanf(v, "%d:%63[^:]:%d", &e->from, name, &e->len) != 3 ||
		    (e->code = keys_lookup(name)) < 0) {
			fprintf(stderr, "test: bad press=%s\n", v);
			exit(2);
		}
	} else if (strcmp(k, "joy") == 0) {
		e->kind = EV_JOY;
		sscanf(v, "%d:%d:%d", &e->from, &e->code, &e->len);
	} else if (strcmp(k, "hat") == 0) {
		e->kind = EV_HAT;
		sscanf(v, "%d:%d,%d:%d", &e->from, &e->x, &e->y, &e->len);
	} else if (strcmp(k, "stick") == 0) {
		e->kind = EV_STICK;
		sscanf(v, "%d:%d,%d:%d", &e->from, &e->x, &e->y, &e->len);
	} else if (strcmp(k, "shot") == 0) {
		e->kind = EV_SHOT;
		sscanf(v, "%d:%255s", &e->from, e->file);
	} else if (strcmp(k, "savestate") == 0 || strcmp(k, "loadstate") == 0) {
		e->kind = k[0] == 's' ? EV_SAVE : EV_LOAD;
		sscanf(v, "%d:%d", &e->from, &e->code);
	} else
		return;
	nev++;
}

/* Where an alignment fault (SIGBUS) or crash happened: PC, LR, address. */
static void fault(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = uc_;
	char buf[200];
	int n = snprintf(buf, sizeof buf, "test: signal %d at pc %08lx lr %08lx addr %p r0 %08lx r1 %08lx r2 %08lx r3 %08lx\n",
	                 sig, uc->uc_mcontext.arm_pc, uc->uc_mcontext.arm_lr, si->si_addr,
	                 uc->uc_mcontext.arm_r0, uc->uc_mcontext.arm_r1,
	                 uc->uc_mcontext.arm_r2, uc->uc_mcontext.arm_r3);
	write(2, buf, n);
	_exit(128 + sig);
}

int main(int argc, char **argv)
{
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		sa.sa_sigaction = fault;
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);
	}
	int frames, i;
	const struct host_video *v;
	const char *dir;
	char sys[512], save[512], state[512];

	if (argc < 4) {
		fprintf(stderr, "usage: headless GAME FRAMES OUT.ppm [key=value...]\n");
		return 2;
	}
	frames = atoi(argv[2]);
	padlog = 0;
	choices_clear();
	for (i = 4; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		if (eq == NULL)
			continue;
		*eq = 0;
		if (strcmp(argv[i], "choices") == 0)
			choices_load(eq + 1);
		else if (strcmp(argv[i], "padlog") == 0)
			padlog = atoi(eq + 1);
		else {
			choices_set(argv[i], eq + 1);
			add_event(argv[i], eq + 1);
		}
	}
	dir = getenv("TEST_DIR") ? getenv("TEST_DIR") : "/tmp/pcsx-test";
	snprintf(sys, sizeof sys, "%s/bios", dir);
	snprintf(save, sizeof save, "%s/saves", dir);
	snprintf(state, sizeof state, "%s/states", dir);
	host_init(sys, save, state, fb);
	if (!host_load_game(argv[1])) {
		fprintf(stderr, "test: load failed\n");
		return 1;
	}
	for (frame_no = 0; frame_no < frames; frame_no++) {
		int k;
		for (k = 0; k < nev; k++) {
			if (evs[k].from != frame_no)
				continue;
			if (evs[k].kind == EV_SAVE)
				printf("savestate %d: %s\n", evs[k].code,
				       host_save_state(evs[k].code) ? "ok" : "FAILED");
			if (evs[k].kind == EV_LOAD)
				printf("loadstate %d: %s\n", evs[k].code,
				       host_load_state(evs[k].code) ? "ok" : "FAILED");
		}
		host_run_frame(1);
		if (padlog) {
			static unsigned last = ~0u;
			unsigned b = host_pad_bits(0);
			int ax = host_pad_analog(0, 0, 0), ay = host_pad_analog(0, 0, 1);
			static int lax, lay;
			if (b != last || ax != lax || ay != lay)
				printf("pad %d: %04x %d,%d\n", frame_no, b, ax, ay);
			last = b; lax = ax; lay = ay;
		}
		fake_cs = (unsigned)((frame_no + 1) * 100.0 / host_fps());
		for (k = 0; k < nev; k++)
			if (evs[k].kind == EV_SHOT && evs[k].from == frame_no + 1 &&
			    (v = host_frame(NULL)) != NULL) {
				write_ppm(evs[k].file, v);
				printf("shot %d %dx%d %08x %s\n", frame_no + 1, v->width,
				       v->height, checksum(v), evs[k].file);
			}
	}
	v = host_frame(NULL);
	if (v) {
		write_ppm(argv[3], v);
		printf("frame %d: %dx%d checksum %08x\n", frames, v->width, v->height, checksum(v));
	} else
		printf("frame %d: no picture\n", frames);
	{
		long long n = audio_bytes / 4;
		printf("audio: %lld frames (%.2f s), %lld non-zero, rms L %.0f R %.0f\n", n,
		       n / 44100.0, audio_nonzero,
		       n ? sqrt(audio_sumsq_l / n) : 0.0, n ? sqrt(audio_sumsq_r / n) : 0.0);
	}
	host_flush_saves();
	host_shutdown();
	printf("done\n");
	return 0;
}
