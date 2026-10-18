/*
 * wimp.c against riscos-crossdev's Fake RISC OS, with the Wimp SWIs it
 * uses faked here and host.c replaced by a stub: start-up, the window
 * opened where it should be, the sprite and how it's plotted, dragging a
 * file in, the keys (F11, Escape, F8), the menus, Quit.
 *
 * Copyright (C) 2026 Andrew Youll. GPL-2.0-or-later, as PCSX ReARMed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fake.h"
#include "swis.h"
#include "rofe.h"

int wimp_main(int argc, char **argv);

/* ---- a stub host ---- */
static host_fb_func fbf;
static int loaded, runs, resets, saves, loads, paused_audio = -1;
static char loaded_path[512];
static struct host_video vid;
static int vid_new;

int host_init(const char *s, const char *sv, const char *st, host_fb_func fb)
{
	CHECK(strcmp(s, "/<Choices$Write>/PCSX/bios") == 0, "system dir");
	(void)sv; (void)st;
	fbf = fb;
	return 1;
}
void host_shutdown(void) {}
static int nloads;
static char loadlist[4][512];
int host_load_game(const char *p) { snprintf(loaded_path, sizeof loaded_path, "%s", p); if (nloads < 4) snprintf(loadlist[nloads++], 512, "%s", p); loaded = 1; return 1; }
const char *host_disc_serial(void) { return ""; }
void host_unload_game(void) { loaded = 0; }
int host_game_loaded(void) { return loaded; }
const char *host_game_name(void) { return "Game"; }
void host_reset(void) { resets++; }
void host_run_frame(int focus)
{
	int pitch;
	void *p;
	(void)focus;
	runs++;
	p = fbf(320, 240, &pitch);
	CHECK(p != NULL && pitch == 320 * 4, "a 320x240 buffer");
	if (p) {
		memset(p, 0x40, 320 * 4);
		vid.buf = p; vid.width = 320; vid.height = 240; vid.pitch = pitch;
		vid_new = 1;
	}
}
double host_fps(void) { return 50.0; }
int host_audio_rate(void) { return 0; }   /* no sound: paced by the clock */
const struct host_video *host_frame(int *n) { if (n) { *n = vid_new; vid_new = 0; } return vid.buf ? &vid : NULL; }
int host_save_state(int s) { (void)s; saves++; return 1; }
int host_load_state(int s) { (void)s; loads++; return 1; }
void host_reload_choices(void) {}
void host_flush_saves(void) {}
const char *host_message(unsigned n) { (void)n; return NULL; }
unsigned host_pad_bits(int p) { (void)p; return 0; }
int host_pad_analog(int p, int s, int a) { (void)p; (void)s; (void)a; return 0; }
int host_setting_int(const char *k, int d) { return choices_get_int(k, d); }
void plat_audio_pause(int p) { paused_audio = p; }
int plat_audio_queued(void) { return -1; }
int plat_key_down(int k) { (void)k; return 0; }
unsigned plat_time_cs(void) { unsigned t; _swix(OS_ReadMonotonicTime, _OUT(0), &t); return t; }
unsigned plat_time_us(void) { return plat_time_cs() * 10000u; }
void plat_log(const char *fmt, ...) { (void)fmt; }
char *__unixify_std(const char *n, char *b, size_t l, int t)
{
	/* enough of UnixLib's for the test: ADFS::4.$.Games.Tri/cue */
	char *o = b;
	(void)t;
	CHECK(strncmp(n, "ADFS::4.$.", 10) == 0, "a RISC OS path");
	o += snprintf(o, l, "/tmp/pcsxtest/ADFS::4/$/");
	for (n += 10; *n; n++)
		*o++ = *n == '.' ? '/' : *n == '/' ? '.' : *n;
	*o = 0;
	return b;
}

/* ---- the fake Wimp ---- */
#define MAXW 4
static struct { int block[23]; int open, vis[4], scroll[2], ext[4]; } win[MAXW];
static int nwin, caret_w = -1, iconbar_icons, task_started, menus_made, processed;
static int closed_down, textops, others;
static FILE *drawlog;
static unsigned gcol;
static int px, py;
static const _kernel_oserror *other(int swi, int r[10])
{
	others++;
	if (!drawlog)
		drawlog = fopen("/tmp/pcsx-draw.log", "w");
	if (swi == 0x40743)
		gcol = (unsigned)r[0];
	else if (swi == 0x45) {
		if (r[0] == 4) { px = r[1]; py = r[2]; }
		else if (r[0] == 101 && drawlog)
			fprintf(drawlog, "fill %d %d %d %d %08x\n", px, py, r[1], r[2], gcol);
	}
	return NULL;
}
static char started[256];
static int sprite_ok, plots, plot_x, plot_y, plot_scale[4], last_scale[4], last_x;
static const int *menu_last;

/* scripted events for Wimp_Poll */
struct evt { int reason; int b[64]; };
static struct evt script[64];
static int nscript, iscript;

static void add_event(int reason, const int *b, int n)
{
	script[nscript].reason = reason;
	memset(script[nscript].b, 0, sizeof script[nscript].b);
	memcpy(script[nscript].b, b, n * sizeof(int));
	nscript++;
}

static const _kernel_oserror *wimp(int swi, int r[10])
{
	int *b = (int *)r[1];
	int i;
	switch (swi) {
	case 0x400C0:   /* Wimp_Initialise */
		CHECK(r[0] == 380 && r[1] == 0x4B534154, "Wimp_Initialise 380, TASK");
		CHECK(r[3] != 0 && ((int *)r[3])[0] == 3, "messages: DataLoad first");
		r[0] = 380; r[1] = 0x77;
		break;
	case 0x400C1:   /* Wimp_CreateWindow */
		memcpy(win[nwin].block, b, sizeof win[nwin].block);
		r[0] = 0x1000 + nwin++;
		break;
	case 0x400C2:   /* Wimp_CreateIcon */
		CHECK(r[0] == 0 && b[0] == -1, "icon bar icon, right");
		CHECK(strcmp((char *)&b[6], "!pcsx") == 0, "its sprite");
		iconbar_icons++;
		r[0] = 1;
		break;
	case 0x400C5:   /* Wimp_OpenWindow */
		i = b[0] - 0x1000;
		CHECK(i >= 0 && i < nwin, "open: our window");
		win[i].open = 1;
		memcpy(win[i].vis, &b[1], 16);
		memcpy(win[i].scroll, &b[5], 8);
		break;
	case 0x400C6:   /* Wimp_CloseWindow */
		win[b[0] - 0x1000].open = 0;
		break;
	case 0x400C7:   /* Wimp_Poll */
	case 0x400E1:   /* Wimp_PollIdle */
		if (iscript == 8) {
			/* the dropped file turns up a little late (as from SparkFS) */
			FILE *f = fopen("/tmp/pcsxtest/ADFS::4/$/Games/Tri.cue", "w");
			if (f) fclose(f);
			CHECK(nloads == 1, "the late file isn't loaded before it opens");
		}
		if (iscript < nscript) {
			memcpy((int *)r[1], script[iscript].b, sizeof script[iscript].b);
			/* y = -12345: 100 OS units below the library window's top */
			if (script[iscript].reason == 6 && ((int *)r[1])[1] == -12345)
				((int *)r[1])[1] = win[2].vis[3] - 100;
			r[0] = script[iscript].reason;
			iscript++;
		} else {
			/* end of the script: ask to quit */
			int q[5] = { 20, 0, 0, 0, 0 };
			memcpy((int *)r[1], q, sizeof q);
			r[0] = 17;
		}
		fake_advance(2);
		break;
	case 0x400C8:   /* Wimp_RedrawWindow */
	case 0x400C9:   /* Wimp_UpdateWindow */
		i = b[0] - 0x1000;
		/* one rectangle: the visible area */
		memcpy(&b[1], win[i].vis, 16);
		memcpy(&b[5], win[i].scroll, 8);
		memcpy(&b[7], win[i].vis, 16);         /* graphics window = all of it */
		r[0] = win[i].open;
		break;
	case 0x400CA:   /* Wimp_GetRectangle */
		r[0] = 0;
		break;
	case 0x400CB:   /* Wimp_GetWindowState */
		i = b[0] - 0x1000;
		memcpy(&b[1], win[i].vis, 16);
		memcpy(&b[5], win[i].scroll, 8);
		b[7] = -1;
		break;
	case 0x400D2:   /* Wimp_SetCaretPosition */
		caret_w = r[0];
		CHECK(r[1] == -1 && (r[4] & (1 << 25)), "invisible caret");
		break;
	case 0x400D3:   /* Wimp_GetCaretPosition */
		b[0] = caret_w;
		break;
	case 0x400D4:   /* Wimp_CreateMenu */
		menus_made++;
		menu_last = (const int *)r[1];
		break;
	case 0x400D7:   /* Wimp_SetExtent */
		memcpy(win[r[0] - 0x1000].ext, b, 16);
		break;
	case 0x400D1:   /* Wimp_ForceRedraw */
	case 0x400DC:   /* Wimp_ProcessKey */
		if (swi == 0x400DC) processed++;
		break;
	case 0x400CF:   /* Wimp_GetPointerInfo */
		b[0] = 0; b[1] = 0; b[2] = 4;   /* Select (not Adjust) */
		break;
	case 0x400DD:   /* Wimp_CloseDown */
		closed_down = 1;
		break;
	case 0x400DE:   /* Wimp_StartTask */
		snprintf(started, sizeof started, "%s", (char *)r[0]);
		task_started++;
		break;
	case 0x400F9:   /* Wimp_TextOp */
		if (r[0] == 2) {
			textops++;
			if (!drawlog)
				drawlog = fopen("/tmp/pcsx-draw.log", "w");
			if (drawlog)
				fprintf(drawlog, "text %d %d %s\n", r[4], r[5], (char *)r[1]);
		} else if (r[0] == 0 && drawlog)
			fprintf(drawlog, "colour %08x\n", (unsigned)r[1]);
		break;
	case 0x400DF:   /* Wimp_ReportError */
		fprintf(stderr, "ReportError: %s\n", ((_kernel_oserror *)r[0])->errmess);
		break;
	case 0x400E7:   /* Wimp_SendMessage */
		CHECK(r[0] == 17 && b[4] == 4 && b[3] == 0x55, "DataLoadAck to the sender's my_ref");
		break;
	}
	return NULL;
}

static const _kernel_oserror *readmodevar(int swi, int r[10])
{
	(void)swi;
	switch (r[1]) {
	case 4: case 5: r[2] = 1; break;             /* eig 1: 1920 x 1080 */
	case 11: r[2] = 1919; break;
	case 12: r[2] = 1079; break;
	}
	return NULL;
}

static const _kernel_oserror *spriteop(int swi, int r[10])
{
	const int *area = (const int *)r[1], *s = (const int *)r[2];
	const int *sc = (const int *)r[6];
	(void)swi;
	CHECK(r[0] == 512 + 52, "SpriteOp 52, by pointer");
	CHECK(s == area + 4, "the sprite after the area header");
	sprite_ok = s[4] == 319 && s[10] == (int)0x301680B5 && s[8] == 44 &&
	            (s[4] + 1) * 4 * (s[5] + 1) >= (1 << 20) && area[3] == 16 + s[0];
	if (plots++ == 0) {          /* the first: in the desktop window */
		plot_x = r[3];
		plot_y = r[4];
		memcpy(plot_scale, sc, 16);
	} else
		memcpy(last_scale, sc, 16);
	last_x = r[3];
	CHECK(r[5] == 0 && r[7] == 0, "plain plot, no table");
	return NULL;
}

static const _kernel_oserror *osbyte(int swi, int r[10]) { (void)swi; r[1] = 0; return NULL; }

int main(void)
{
	char *argv[2] = { "pcsx", NULL };
	int b[64];
	int i;

	fake_reset();
	{
		static const int w[] = { 0x400C0, 0x400C1, 0x400C2, 0x400C5, 0x400C6, 0x400C7,
			0x400C8, 0x400C9, 0x400CA, 0x400CB, 0x400CF, 0x400D1, 0x400D2, 0x400D3,
			0x400D4, 0x400D7, 0x400DC, 0x400DD, 0x400DE, 0x400DF, 0x400E0, 0x400E1,
			0x400E7, 0x400F9, 0 };
		for (i = 0; w[i]; i++)
			fake_swi(w[i], NULL, wimp);
		fake_swi(0x40743, "ColourTrans_SetGCOL", other);
		fake_swi(0x45, "OS_Plot", other);
		fake_swi(0x49980, "JPEG_Info", other);
		fake_swi(0x49982, "JPEG_PlotScaled", other);
		fake_swi(0x406C0, "Hourglass_On", other);
		fake_swi(0x406C1, "Hourglass_Off", other);
	}
	fake_swi(OS_ReadModeVariable, "OS_ReadModeVariable", readmodevar);
	fake_swi(OS_SpriteOp, "OS_SpriteOp", spriteop);
	fake_swi(OS_Byte, "OS_Byte", osbyte);
	/* wimp.c sends stderr to the log: the results end up there too */
	/* the dropped file: present (an archive that's slow to open is tested by
	   the delay below) */
	system("rm -rf '/tmp/pcsxtest' '/<Choices$Write>' && mkdir -p '/tmp/pcsxtest/ADFS::4/$/Games' "
	       "'/<Choices$Write>/PCSX/games' && touch '/tmp/pcsxtest/ADFS::4/$/Games/Lib.cue'");
	setenv("PCSX$Log", getenv("TEST_LOG") ? getenv("TEST_LOG") : "/tmp/test_wimp.log", 1);

	/* the library window is open at the start (no game given) */
	/* 1: drag a folder into the library: its game is listed */
	memset(b, 0, sizeof b);
	b[0] = 256; b[1] = 0x99; b[2] = 0x55; b[4] = 3; b[5] = 0x1002; b[10] = 0x1000;
	strcpy((char *)&b[11], "ADFS::4.$.Games");
	add_event(17, b, 30);
	memset(b, 0, sizeof b); b[0] = 0x1002; add_event(1, b, 1);       /* redraw */
	/* (after playing, redraw again: the game is in Recently played) */
	/* 2: double click its row: it plays */
	memset(b, 0, sizeof b); b[0] = 500; b[1] = -12345; b[2] = 4; b[3] = 0x1002;
	add_event(6, b, 5);
	/* 3: menu on the row: Game settings > High resolution */
	memset(b, 0, sizeof b); b[0] = 500; b[1] = -12345; b[2] = 2; b[3] = 0x1002;
	add_event(6, b, 5);
	memset(b, 0, sizeof b); b[0] = 1; b[1] = 0; b[2] = -1; add_event(9, b, 3);
	memset(b, 0, sizeof b); b[0] = 0x1002; add_event(1, b, 1);       /* redraw again */
	/* 4: Adjust on the icon bar: the game window */
	memset(b, 0, sizeof b); b[0] = 1500; b[2] = 1; b[3] = -2;
	add_event(6, b, 5);
	/* 5: drag a disc image onto the icon bar (it turns up a little late) */
	memset(b, 0, sizeof b);
	b[0] = 256; b[1] = 0x99; b[2] = 0x55; b[3] = 0; b[4] = 3; b[5] = -2;
	strcpy((char *)&b[11], "ADFS::4.$.Games.Tri/cue");
	add_event(17, b, 30);
	/* 3: null events: frames run and are shown */
	for (i = 0; i < 14; i++)
		add_event(0, b, 0);
	/* 4: F11: full screen */
	memset(b, 0, sizeof b); b[6] = 0x1CB;
	add_event(8, b, 7);
	add_event(0, b, 0);
	add_event(0, b, 0);
	/* 5: Escape: back to the window */
	memset(b, 0, sizeof b); b[6] = 0x1B;
	add_event(8, b, 7);
	/* 6: F8 pauses, F2 saves, a letter goes to Wimp_ProcessKey */
	memset(b, 0, sizeof b); b[6] = 0x188; add_event(8, b, 7);
	memset(b, 0, sizeof b); b[6] = 0x182; add_event(8, b, 7);
	memset(b, 0, sizeof b); b[6] = 'z'; add_event(8, b, 7);
	/* 7: menu over the window, choose Reset (item 1) */
	memset(b, 0, sizeof b); b[0] = 600; b[1] = 600; b[2] = 2; b[3] = 0x1000;
	add_event(6, b, 5);
	memset(b, 0, sizeof b); b[0] = 1; b[1] = -1; add_event(9, b, 2);
	/* 8: the close icon: closed, sound paused */
	memset(b, 0, sizeof b); b[0] = 0x1000; add_event(3, b, 1);

	CHECK(wimp_main(1, argv) == 0, "runs and quits");

	CHECK(nwin == 3, "three windows: desktop, full screen, library");
	CHECK(win[2].block[7] == (int)0xBF000002, "library window: scroll bar, size icon");
	CHECK(textops > 0, "library rows drawn");
	CHECK(nloads == 2 && strstr(loadlist[0], "/Games/Lib.cue") && strstr(loadlist[1], "/Games/Tri.cue"),
	      "the library's game played, then the dropped one");
	{
		FILE *f = fopen("/<Choices$Write>/PCSX/games/Lib", "r");
		char line[200] = "";
		int found = 0;
		while (f && fgets(line, sizeof line, f))
			if (strstr(line, "pcsx_rearmed_neon_enhancement_enable: enabled"))
				found = 1;
		if (f) fclose(f);
		CHECK(found, "per-game setting written");
		f = fopen("/<Choices$Write>/PCSX/Library", "r");
		found = 0;
		while (f && fgets(line, sizeof line, f))
			if (strstr(line, "folder: /tmp/pcsxtest/ADFS::4/$/Games") || strstr(line, "Tri.cue"))
				found++;
		if (f) fclose(f);
		CHECK(found == 2, "library file: the folder, and the dropped game as played");
	}
	CHECK(win[0].block[7] == (int)0x87000002, "desktop window: title, back, close, movable");
	CHECK((win[0].block[14] & 0x100) && win[0].block[18] != 0, "indirected title");
	CHECK(win[1].block[7] == (int)0x80000020, "full screen window: no furniture");
	CHECK(iconbar_icons == 1, "icon bar icon");
	CHECK(strcmp(loaded_path, "/tmp/pcsxtest/ADFS::4/$/Games/Tri.cue") == 0, "dropped file, Unix form");
	if (strcmp(loaded_path, "/tmp/pcsxtest/ADFS::4/$/Games/Tri.cue"))
		fprintf(stderr, "loaded %s\n", loaded_path);
	/* scale: 1920x1080 -> 3 (960x720 px leaves 160) -> 1920x1440 OS units */
	CHECK(win[0].ext[2] == 1920 && win[0].ext[1] == -1440, "extent: 320x240 x3 at eig 1");
	CHECK(win[0].vis[2] - win[0].vis[0] == 1920 && win[0].vis[3] - win[0].vis[1] == 1440,
	      "window size = picture");
	CHECK(win[0].vis[0] == (3840 - 1920) / 2 && win[0].vis[3] == (2160 + 1440) / 2,
	      "centred the first time");
	CHECK(runs >= 1, "frames run on null events");
	CHECK(plots >= 1 && sprite_ok, "sprite plotted: 320 wide, 32bpp 90 dpi, >= 1 MB");
	CHECK(plot_scale[0] == 1920 && plot_scale[2] == 640 &&
	      plot_scale[1] == 1440 && plot_scale[3] == 480, "scale 1920/640 x 1440/480");
	/* the first plot: the picture's top left at the window's top left */
	CHECK(plot_x == win[0].vis[0], "plotted at the window's left");
	{
		/* sprite rows (>= 1 MB: 820) scaled like the picture, bottom edge */
		int rows = ((1 << 20) + 1279) / 1280;
		int sh = rows * 1440 / 240;
		CHECK(plot_y == win[0].vis[3] - (sh >> 1 << 1), "picture's top at the window's top");
	}
	/* full screen: 1440 x 1080 pixels (4:3) in the middle of 1920 */
	CHECK(last_scale[0] == 2880 && last_scale[1] == 2160, "full screen scale: 4:3, full height");
	CHECK(last_x == (1920 - 1440) / 2 * 2, "full screen picture centred");
	CHECK(win[1].vis[2] == 3840 && win[1].vis[3] == 2160, "full screen covers the screen");
	CHECK(saves == 1, "F2 saved a state");
	CHECK(processed == 1, "other keys passed on");
	CHECK(resets == 1, "menu Reset");
	CHECK(menus_made >= 1, "menu opened");
	CHECK(!win[0].open && paused_audio == 1, "closed: sound paused");
	CHECK(closed_down, "Wimp_CloseDown");
	return fake_done("test_wimp");
}
