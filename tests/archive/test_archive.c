/*
 * archive.c on Linux, against zip files made by Python and 7z files made
 * by 7-Zip (run.sh): every way of packing unpacks to the same files, in
 * steps of about a megabyte; .cue files are changed to match the names
 * written; damage and unsupported packing are reported; serials are read
 * from inside archives.
 *
 * Copyright (C) 2026 Andrew Youll. GPL-2.0-or-later, as PCSX ReARMed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include "archive.h"
#include "library.h"
#include "rofe.h"

static int checks, fails;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, what, cur); } } while (0)
static const char *cur = "";

void plat_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }

static const char *T;
#define G "Game One & Two (Europe)"
#define GD "Game One _ Two (Europe)"      /* as written: & isn't allowed */

static unsigned char *slurp(const char *p, long *n)
{
	FILE *f = fopen(p, "rb");
	unsigned char *b;
	if (f == NULL) { *n = -1; return NULL; }
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(*n + 1);
	if (fread(b, 1, *n, f) != (size_t)*n) *n = -1;
	b[*n] = 0;
	fclose(f);
	return b;
}

static int same_file(const char *a, const char *b)
{
	long na, nb;
	unsigned char *x = slurp(a, &na), *y = slurp(b, &nb);
	int ok = na >= 0 && na == nb && memcmp(x, y, na) == 0;
	free(x); free(y);
	return ok;
}

static int count_entries(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;
	if (!d) return -1;
	while ((e = readdir(d)) != NULL)
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			n++;
	closedir(d);
	return n;
}

/* Unpacks NAME into T/out; returns the step result (0 done) */
static int unpack(const char *name, char *err, int *steps, struct arc **ap)
{
	char p[600], o[600];
	struct arc *a;
	int r;
	snprintf(p, sizeof p, "%s/arc/%s", T, name);
	snprintf(o, sizeof o, "%s/out", T);
	arc_clear_dir(o);
	*steps = 0;
	*ap = a = arc_open(p, err, 300);
	if (a == NULL)
		return -2;
	if (!arc_unpack_start(a, o, err, 300))
		return -3;
	while ((r = arc_unpack_step(a, err, 300)) > 0)
		(*steps)++;
	return r;
}

static void full_game(const char *name)
{
	char err[300] = "", a[600], b[600];
	int steps, r;
	struct arc *ar;
	long n;
	unsigned char *cue;
	cur = name;
	r = unpack(name, err, &steps, &ar);
	CHECK(r == 0, err);
	CHECK(ar && arc_game(ar) && !strcmp(arc_game(ar), G ".cue"), "the .cue is the game");
	snprintf(a, sizeof a, "%s/out/" GD ".cue", T);
	CHECK(ar && !strcmp(arc_game_path(ar), a), "the game's unpacked path");
	CHECK(ar && arc_percent(ar) == 100, "100%");
	CHECK(steps >= 9, "in steps of about a megabyte (the desktop keeps going)");
	snprintf(a, sizeof a, "%s/out/" GD ".bin", T);
	snprintf(b, sizeof b, "%s/src/" G ".bin", T);
	CHECK(same_file(a, b), "the .bin unpacked whole");
	snprintf(a, sizeof a, "%s/out/" GD ".cue", T);
	cue = slurp(a, &n);
	CHECK(cue && strstr((char *)cue, "FILE \"" GD ".bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n") == (char *)cue,
	      "the .cue names the file as written, the rest as it was");
	free(cue);
	snprintf(a, sizeof a, "%s/out/extra/a.txt", T);
	snprintf(b, sizeof b, "%s/src/extra/a.txt", T);
	CHECK(same_file(a, b), "a file in a folder");
	snprintf(a, sizeof a, "%s/out/empty.dat", T);
	{ struct stat st; CHECK(stat(a, &st) == 0 && st.st_size == 0, "an empty file"); }
	arc_close(ar);
	/* the serial, from inside it */
	{
		char s[16];
		snprintf(a, sizeof a, "%s/arc/%s", T, name);
		CHECK(library_read_serial(a, s, sizeof s) && !strcmp(s, "SLES12345"), "the serial from inside");
	}
}

int main(int argc, char **argv)
{
	char err[300], p[600], o[600];
	int steps, r, have7z = argc > 2 && argv[2][0];
	struct arc *a;
	T = argv[1];
	snprintf(o, sizeof o, "%s/out", T);

	/* names as written */
	{
		char n[100];
		arc_disc_name("a/../b\\c d&e:f.bin", n, sizeof n);
		cur = "names";
		CHECK(!strcmp(n, "a/b/c d_e_f.bin"), "no .., \\ is a folder, & and : go");
		arc_disc_name("Pok\xC3\xA9mon.cue", n, sizeof n);
		CHECK(!strcmp(n, "Pok\xE9mon.cue"), "UTF-8 to Latin-1");
		arc_disc_name("Caf\xE9.cue", n, sizeof n);
		CHECK(!strcmp(n, "Caf\xE9.cue"), "Latin-1 kept");
	}
	snprintf(p, sizeof p, "%s/src/readme.txt", T);
	cur = "type";
	CHECK(arc_type(p) == ARC_NONE, "a text file isn't an archive");
	snprintf(p, sizeof p, "%s/arc/deflate.zip", T);
	CHECK(arc_type(p) == ARC_ZIP && arc_ext(p) == ARC_ZIP, "zip by content and name");

	full_game("deflate.zip");
	full_game("stored.zip");
	full_game("lzma.zip");
	full_game("zip64.zip");

	cur = "nogame.zip";
	snprintf(p, sizeof p, "%s/arc/nogame.zip", T);
	a = arc_open(p, err, sizeof err);
	CHECK(a && arc_game(a) == NULL, "no game in it");
	arc_close(a);

	cur = "clonecd.zip";
	r = unpack("clonecd.zip", err, &steps, &a);
	CHECK(r == 0 && arc_game(a) && !strcmp(arc_game(a), "Tomba! [U] [SCUS-94236].img"),
	      "a CloneCD rip: the core is given the .img (it finds the .ccd and .sub)");
	snprintf(p, sizeof p, "%s/out/Tomba! [U] [SCUS-94236].ccd", T);
	{ FILE *f = fopen(p, "r"); CHECK(f != NULL, "the .ccd beside it"); if (f) fclose(f); }
	arc_close(a);
	{
		char s[16];
		snprintf(p, sizeof p, "%s/arc/clonecd.zip", T);
		CHECK(library_read_serial(p, s, sizeof s) && !strcmp(s, "SLES12345"), "its serial, from the .img");
	}

	cur = "multi.zip";
	r = unpack("multi.zip", err, &steps, &a);
	CHECK(r == 0 && arc_game(a) && !strcmp(arc_game(a), "Game.m3u"), "the .m3u is the game");
	{
		long n;
		unsigned char *m;
		snprintf(p, sizeof p, "%s/out/Game.m3u", T);
		m = slurp(p, &n);
		CHECK(m && !strcmp((char *)m, "Disc 1.cue\nDisc 2.cue\n"), "m3u as it was (names fine on Linux)");
		free(m);
	}
	arc_close(a);

	cur = "damaged.zip";
	r = unpack("damaged.zip", err, &steps, &a);
	CHECK(r < 0 && strstr(err, "CRC"), "damage found by the CRC");
	arc_close(a);

	if (have7z) {
		full_game("lzma2.7z");
		full_game("lzma.7z");
		full_game("copy.7z");
		full_game("nonsolid.7z");
		full_game("ultra.7z");
		cur = "bcj-small.7z";
		r = unpack("bcj-small.7z", err, &steps, &a);
		CHECK(r == 0, err);
		snprintf(p, sizeof p, "%s/out/readme.txt", T);
		snprintf(o, sizeof o, "%s/src/readme.txt", T);
		CHECK(same_file(p, o), "a small filtered block, in memory");
		arc_close(a);
		cur = "bcj-big.7z";
		r = unpack("bcj-big.7z", err, &steps, &a);
		CHECK(r < 0 && strstr(err, "LZMA2"), "a big filtered block: says how to pack it");
		arc_close(a);
		cur = "truncated.7z";
		snprintf(p, sizeof p, "%s/arc/truncated.7z", T);
		a = arc_open(p, err, sizeof err);
		CHECK(a == NULL && err[0], "a cut-off 7z (its directory's at the end)");
		arc_close(a);
	} else
		fprintf(stderr, "test_archive: no 7zr, 7z not tested\n");

	snprintf(o, sizeof o, "%s/out", T);
	arc_clear_dir(o);
	cur = "clear";
	CHECK(count_entries(o) == 0, "cleared");

	/* the library lists archives, and reads their serials while idle */
	{
		char f[600], c[600], s[600], st[600];
		struct game *g = NULL;
		int i, k = 0;
		cur = "library";
		snprintf(f, sizeof f, "%s/Library", T);
		snprintf(c, sizeof c, "%s/covers", T);
		snprintf(s, sizeof s, "%s/games", T);
		snprintf(st, sizeof st, "%s/states", T);
		library_init(f, c, s, st);
		snprintf(p, sizeof p, "%s/arc", T);
		library_add_folder(p);
		for (i = 0; i < library_count(); i++)
			if (strstr(library_game(i)->path, "nogame"))
				k = 1;
		CHECK(!k, "an archive with no game isn't listed");
		for (i = 0; i < library_count(); i++)
			if (strstr(library_game(i)->path, "/deflate.zip"))
				g = library_game(i);
		CHECK(g && !strcmp(g->name, "deflate") && !g->serial[0], "listed; serial not read yet");
		CHECK(library_peek_pending(), "serials to read");
		for (i = 0; i < 100 && library_peek_serial() >= 0; i++)
			;
		CHECK(!library_peek_pending(), "all looked at");
		CHECK(g && !strcmp(g->serial, "SLES12345"), "read while idle");
	}

	fprintf(stderr, "test_archive: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
