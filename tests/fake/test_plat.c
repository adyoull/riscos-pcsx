/*
 * plat_riscos.c against riscos-crossdev's Fake RISC OS: the sound stream
 * (SharedSoundBuffer/StreamManager), the joysticks (USBJoystick's API and
 * Joystick_Read) and the keyboard scan. Built for 32-bit ARM Linux and run
 * under QEMU (the fake needs 32-bit pointers).
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

/* ---- a fake SharedSoundBuffer + StreamManager ---- */
static int s_open, s_paused = 1, s_rate, s_volume, s_bufsize, s_block_units;
static unsigned s_added, s_played, s_blocks_added;
static unsigned s_refuse_after = ~0u;
static int s_last_block;

static unsigned s_playing_cs;      /* centiseconds spent playing */
static void s_play(void)
{
	/* plays in whole blocks of 4096 bytes, at rate * 4 bytes a second */
	static unsigned last, base;
	unsigned now = 0, due;
	_swix(OS_ReadMonotonicTime, _OUT(0), &now);
	if (!s_paused)
		s_playing_cs += now - last;
	last = now;
	due = (unsigned)((unsigned long long)s_playing_cs * s_rate * 4 / 100);
	if (due / 4096 * 4096 > base) {
		s_played += due / 4096 * 4096 - base;
		base = due / 4096 * 4096;
	}
	if (s_played > s_added) {
		/* ran dry: what wasn't there isn't owed later */
		s_played = s_added;
	}
}

static const _kernel_oserror *ssb(int swi, int r[10])
{
	static const _kernel_oserror full = { 1, "Buffer full" };
	s_play();
	switch (swi) {
	case 0x55FC0: s_open = 1; s_last_block = r[2]; r[0] = 0x1234; break;
	case 0x55FC1: s_open = 0; break;
	case 0x55FCE: r[0] = 0x5678; break;
	case 0x55FC4: s_volume = r[1]; break;
	case 0x55FC5: s_rate = r[1] / 1024; break;
	case 0x55FC9: s_paused = !(r[1] & 1); break;
	case 0x57287: s_bufsize = r[1]; break;
	case 0x57282:
		CHECK(r[0] == 0x5678, "AddBlock: the stream handle");
		CHECK(r[2] == s_last_block, "AddBlock: whole blocks");
		if (s_blocks_added >= s_refuse_after || s_added - s_played + r[2] > (unsigned)s_bufsize)
			return &full;
		s_added += r[2];
		s_blocks_added++;
		break;
	case 0x57288:
		r[0] = s_block_units ? s_added / 4096 : s_added;
		r[1] = s_block_units ? s_played / 4096 : s_played;
		break;
	}
	return NULL;
}

/* ---- a fake USBJoystick ---- */
struct uj_device_info {
	uint32_t size;
	const char *manufacturer, *product, *serial;
	uint32_t vendor_id, product_id, num_axes, num_buttons, num_hats;
};
struct uj_axis_info {             /* with room after: records are 48 bytes */
	uint32_t hid_usage_page, hid_usage, type;
	const char *name;
	int32_t min, mid, max;
	int32_t is_digital;
	uint32_t extra[4];
};
static uint32_t j_mask = 0x5, j_gen = 1;
static struct uj_device_info j_dev = { sizeof j_dev, "Acme", "Test Pad", "", 1, 2, 5, 12, 1 };
static struct uj_axis_info j_axes[5] = {
	{ 1, 0x30, 5, "X", 0, 128, 255, 0 },
	{ 1, 0x31, 6, "Y", 0, 128, 255, 0 },
	{ 1, 0x32, 7, "Z", -1000, 0, 1000, 0 },
	{ 1, 0x35, 10, "RZ", -1000, 0, 1000, 0 },
	{ 1, 0x39, 1, "HatX", -1, 0, 1, 1 },
};
static struct { int32_t v; uint32_t pad; } j_vals[6];      /* 8-byte values */
static uint32_t j_buttons;
static int j_unplugged, j_enum_calls, j_values_fail, j_axisinfo_small;
static const _kernel_oserror j_err = { 0x43F00, "Draft call changed" };

static const _kernel_oserror *joy(int swi, int r[10])
{
	switch (swi) {
	case 0x43F44: r[0] = (int)j_mask; r[1] = (int)j_gen; j_enum_calls++; break;
	case 0x43F45: r[2] = (int)&j_dev; break;
	case 0x43F46: r[1] = j_axisinfo_small ? 12 : (int)sizeof j_axes[0]; r[2] = 5; r[3] = (int)j_axes; break;
	case 0x43F47:
		if (j_values_fail) return &j_err;
		if (j_unplugged) { r[3] = 0; break; }
		r[1] = sizeof j_vals[0]; r[2] = 5; r[3] = (int)j_vals; r[4] = (int)j_buttons;
		break;
	}
	return NULL;
}

/* Joystick_Read, the stable API: sticks 0 and 1 */
static unsigned jr_pos[2], jr_buttons[2], jr_calls, jr_sticks = 2, jr_no16;
static int jr_8bit[2];
static const _kernel_oserror jr_bad = { 0x43F01, "Bad joystick number" };
static const _kernel_oserror *joy_read(int swi, int r[10])
{
	int stick = r[0] & 0xff, reason = (r[0] >> 8) & 0xff;
	(void)swi;
	jr_calls++;
	CHECK((r[0] & ~0x1ff) == 0, "Joystick_Read: reason 0 or 1, no other bits");
	if (stick >= (int)jr_sticks || (reason == 1 && jr_no16))
		return &jr_bad;
	if (reason == 1) {
		r[0] = (int)jr_pos[stick];
		r[1] = (int)jr_buttons[stick];
	} else
		r[0] = jr_8bit[stick];
	return NULL;
}

static const _kernel_oserror *joy_enum_fail(int swi, int r[10])
{
	(void)swi; (void)r;
	return &j_err;
}

/* ---- keys ---- */
static int keys_down[128];
static const _kernel_oserror *osbyte(int swi, int r[10])
{
	(void)swi;
	CHECK(r[0] == 121, "OS_Byte 121");
	r[1] = keys_down[(r[1] ^ 0x80) & 0x7f] ? 0xff : 0;
	return NULL;
}

static void time_passes(unsigned cs)
{
	fake_advance(cs);
	s_play();
}

int main(void)
{
	static int16_t buf[1024 * 2 * 3];
	struct joy_state js[2];
	int q, n, i;

	fake_reset();
	fake_swi(OS_Byte, "OS_Byte", osbyte);

	/* --- keys --- */
	keys_down[97] = 1;
	CHECK(plat_key_down(97), "Z down");
	CHECK(!plat_key_down(66), "X up");
	CHECK(!plat_key_down(200), "out of range");

	/* --- sound: missing modules --- */
	CHECK(plat_audio_open(44100, 2940) == 0, "no SSB: no sound");

	/* --- sound --- */
	fake_swi(0x55FC0, "SharedSoundBuffer_OpenStream", ssb);
	fake_swi(0x55FC1, "SharedSoundBuffer_CloseStream", ssb);
	fake_swi(0x55FC4, "SharedSoundBuffer_Volume", ssb);
	fake_swi(0x55FC5, "SharedSoundBuffer_SampleRate", ssb);
	fake_swi(0x55FC9, "SharedSoundBuffer_Pause", ssb);
	fake_swi(0x55FCE, "SharedSoundBuffer_ReturnStreamHandle", ssb);
	fake_swi(0x57282, "StreamManager_AddBlock", ssb);
	fake_swi(0x57287, "StreamManager_SetBuffer", ssb);
	fake_swi(0x57288, "StreamManager_BufferStats", ssb);
	CHECK(plat_audio_open(44100, 2940) == 1, "opens");
	CHECK(s_rate == 44100, "rate");
	CHECK(s_volume == -1, "full volume");
	CHECK(s_paused, "paused until there's sound");
	CHECK(plat_audio_queued() == 0, "nothing queued");
	/* 735 frames (one NTSC frame) = 2940 bytes: less than a block */
	plat_audio_write(buf, 2940);
	CHECK(s_added == 0, "less than a block: held back");
	CHECK(plat_audio_queued() == 2940, "held back counts as queued");
	plat_audio_write(buf, 2940);
	CHECK(s_added == 4096, "one block given");
	CHECK(s_paused, "still paused at one block");
	plat_audio_write(buf, 2940);
	CHECK(s_added == 8192 && !s_paused, "two blocks: playing");
	CHECK(plat_audio_queued() == 8820, "queued = given + held back");
	time_passes(5);   /* 5 cs = 8820 bytes: two whole blocks played */
	q = plat_audio_queued();
	CHECK(q == 8820 - 8192, "played blocks leave the queue");

	/* the pacing loop: write a frame whenever under 60 ms; over 2 s of
	   time the amount written must match what's played */
	{
		unsigned written = 8820, t;
		for (t = 0; t < 200; t++) {
			int k;
			for (k = 0; k < 4 && plat_audio_queued() < 44100 * 4 * 60 / 1000; k++) {
				plat_audio_write(buf, 2940);
				written += 2940;
			}
			time_passes(1);
		}
		q = plat_audio_queued();
		CHECK(q > 0 && q < 44100 * 4 * 90 / 1000, "pacing keeps 0-90 ms queued");
		if (!(written > 44100 * 4 * 195 / 100 && written < 44100 * 4 * 210 / 100))
			fprintf(stderr, "written %u\n", written);
		CHECK(written > 44100 * 4 * 195 / 100 && written < 44100 * 4 * 210 / 100,
		      "pacing writes what plays (2.05 s) and no more");
	}

	/* pause stops play; resume plays at once */
	plat_audio_pause(1);
	CHECK(s_paused, "pause");
	q = plat_audio_queued();
	time_passes(50);
	CHECK(plat_audio_queued() == q, "nothing plays while paused");
	plat_audio_pause(0);
	plat_audio_write(buf, 2940);
	CHECK(!s_paused, "resumes on the next write");

	/* BufferStats in blocks */
	{
		int qb = plat_audio_queued();
		s_block_units = 1;
		q = plat_audio_queued();
		if (q != qb) fprintf(stderr, "bytes %d blocks %d (added %u played %u, given %u)\n", qb, q, s_added, s_played, s_blocks_added);
		CHECK(q == qb, "block units detected: the same bytes queued");
	}
	s_block_units = 0;

	/* refused: kept for later, nothing lost below the limit */
	s_refuse_after = s_blocks_added;
	{
		unsigned before = s_added;
		plat_audio_write(buf, 4096 * 2);
		CHECK(s_added == before, "refused: not added");
		s_refuse_after = ~0u;
		plat_audio_write(buf, 4);
		CHECK(s_added >= before + 8192, "given when there's room again");
	}
	/* far too much: the oldest is dropped, never overflowing */
	for (i = 0; i < 100; i++)
		plat_audio_write(buf, 4096 * 3);
	CHECK(plat_audio_queued() >= 0, "after a flood");
	plat_audio_close();
	CHECK(!s_open, "closed");

	/* --- joysticks: none --- */
	n = plat_joy_poll(js, 2);
	CHECK(n == 0, "no Joystick module: no pads");

	/* --- USBJoystick --- */
	fake_reset();
	fake_swi(0x43F44, "Joystick_Enumerate", joy);
	fake_swi(0x43F45, "Joystick_DeviceInfo", joy);
	fake_swi(0x43F46, "Joystick_AxisInfo", joy);
	fake_swi(0x43F47, "Joystick_AxisValues", joy);
	fake_swi(0x43F40, "Joystick_Read", joy_read);
	/* plat_riscos.c only decides which API once; it can't be reset, so the
	   Joystick_Read case is a separate run (test_plat_jr) */
	j_vals[0].v = 128; j_vals[1].v = 128; j_vals[2].v = 0; j_vals[3].v = 0;
	j_vals[4].v = 0;
	fake_advance(1000);   /* (the clock went back to 0) */
	n = plat_joy_poll(js, 2);
	CHECK(n == 2, "two pads (slots 0 and 2)");
	CHECK(js[0].present && js[1].present, "present");
	CHECK(strcmp(js[0].name, "Test Pad") == 0, "product name");
	CHECK(js[0].x == 0 && js[0].y == 0, "centred");
	CHECK(js[0].has_rstick, "Z/RZ as the right stick");
	CHECK(js[0].has_hat == 0, "only a hat X: no hat");
	j_vals[0].v = 255; j_vals[1].v = 0; j_vals[2].v = 1000; j_vals[3].v = -500;
	j_buttons = 0x201;
	n = plat_joy_poll(js, 2);
	CHECK(js[0].x == 32767 && js[0].y == -32767, "full right, full up");
	CHECK(js[0].rx == 32767 && js[0].ry == -16383, "right stick scaled about mid");
	CHECK(js[0].buttons == 0x201, "buttons");
	n = plat_joy_poll(js, 1);
	CHECK(n == 1, "max respected");
	/* unplugged: released */
	j_unplugged = 1;
	n = plat_joy_poll(js, 2);
	CHECK(!js[0].present && js[0].buttons == 0, "unplugged: nothing pressed");
	j_unplugged = 0;
	/* hot plug: rescans once a second when the generation changes */
	{
		int before = j_enum_calls;
		j_mask = 0x1;
		j_gen = 2;
		plat_joy_poll(js, 2);
		CHECK(j_enum_calls == before, "no rescan within a second");
		fake_advance(101);
		n = plat_joy_poll(js, 2);
		CHECK(j_enum_calls == before + 1 && n == 1, "rescanned: one pad");
	}
	/* a hat */
	j_axes[3].type = 2;   /* RZ becomes HatY: hat = axes 4 (x) and 3 (y) */
	j_gen = 3;
	fake_advance(101);
	j_vals[4].v = -1; j_vals[3].v = 1;
	n = plat_joy_poll(js, 2);
	CHECK(js[0].has_hat && js[0].hat_x == -1 && js[0].hat_y == 1, "hat left-down");
	CHECK(!js[0].has_rstick, "no right stick without RZ");

	/* --- joy.api: stable: Joystick_Read even with the newer calls there --- */
	choices_set("joy.api", "stable");
	plat_joy_reset();
	j_enum_calls = 0;
	jr_pos[0] = 0;                      /* the PRM: the first read is X=0 Y=0 */
	jr_pos[1] = 0xFFFF8000u;            /* stick 1: full right, centred Y */
	jr_buttons[1] = 0x80000001u;        /* 32 buttons */
	n = plat_joy_poll(js, 2);
	CHECK(n == 2 && j_enum_calls == 0, "stable: two sticks, newer calls not used");
	CHECK(js[0].x == 0 && js[0].y == 0, "the first read's 0,0 is centred, not a corner");
	CHECK(js[1].x == 32767 && js[1].y == 0 && js[1].buttons == 0x80000001u, "stick 1, 32 buttons");
	CHECK(!js[0].has_hat && !js[0].has_rstick, "no hat or right stick from the stable API");
	jr_pos[0] = 0x8000FFFFu;            /* up */
	n = plat_joy_poll(js, 2);
	CHECK(js[0].y == -32767 && js[0].x == 0, "Y 65535 is up");
	n = plat_joy_poll(js, 1);
	CHECK(n == 1, "stable: max respected");

	/* --- auto, but the newer calls misbehave: Joystick_Read from then on --- */
	choices_set("joy.api", "auto");
	plat_joy_reset();
	j_values_fail = 1;
	j_enum_calls = 0;
	n = plat_joy_poll(js, 2);
	CHECK(n == 2 && !strcmp(js[0].name, "Joystick 0"), "AxisValues error: stable, same poll");
	CHECK(js[0].y == -32767, "and read through Joystick_Read");
	j_values_fail = 0;
	fake_advance(101);
	j_gen++;
	{
		unsigned e = j_enum_calls;
		n = plat_joy_poll(js, 2);
		CHECK(j_enum_calls == e && !strcmp(js[0].name, "Joystick 0"), "the newer calls not tried again");
	}
	/* AxisInfo records smaller than expected */
	plat_joy_reset();
	j_axisinfo_small = 1;
	n = plat_joy_poll(js, 2);
	CHECK(n == 2 && !strcmp(js[0].name, "Joystick 0"), "short AxisInfo records: stable");
	j_axisinfo_small = 0;
	/* Enumerate itself failing */
	plat_joy_reset();
	fake_swi(0x43F44, "Joystick_Enumerate", joy_enum_fail);
	n = plat_joy_poll(js, 2);
	CHECK(n == 2 && !strcmp(js[0].name, "Joystick 0"), "Enumerate error: stable");

	/* --- a module with only Joystick_Read, one stick, no 16-bit read --- */
	fake_reset();
	fake_swi(0x43F40, "Joystick_Read", joy_read);
	fake_advance(1000);
	choices_set("joy.api", "usbjoystick");    /* asked for, not there: stable anyway */
	plat_joy_reset();
	jr_sticks = 1;
	jr_no16 = 1;
	jr_8bit[0] = (0x05 << 16) | (0x81 << 8) | 0x7F;   /* buttons 0,2; X -127; Y +127 (up) */
	n = plat_joy_poll(js, 2);
	CHECK(n == 1, "one stick (stick 1 is an error)");
	CHECK(js[0].x == -32767 && js[0].y == -32767 && js[0].buttons == 5, "8-bit read: left, up, buttons");
	jr_8bit[0] = (64 << 8) | (unsigned char)-64;          /* a digital stick: +-64 */
	n = plat_joy_poll(js, 2);
	CHECK(js[0].x == 64 * 32767 / 127 && js[0].y == 64 * 32767 / 127, "digital +-64: past the dead zone");

	return fake_done("test_plat");
}
