/*
 * library.c on Linux, against a folder of made-up disc images built by
 * run.sh (real ISO 9660 with SYSTEM.CNF, as .iso and as raw .cue/.bin).
 *
 * Copyright (C) 2026 Andrew Youll. GPL-2.0-or-later, as PCSX ReARMed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "library.h"
#include "rofe.h"

static int checks, fails;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, what); } } while (0)

void plat_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }

static struct game *by_name(const char *n)
{
	int i;
	for (i = 0; i < library_count(); i++)
		if (!strcmp(library_game(i)->name, n))
			return library_game(i);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *T = argc > 1 ? argv[1] : "/tmp/libtest";
	char f[600], c[600], s[600], st[600], buf[64];
	struct game *g, *rec[8];
	int n;

	snprintf(f, sizeof f, "%s/Library", T);
	snprintf(c, sizeof c, "%s/covers", T);
	snprintf(s, sizeof s, "%s/games", T);
	snprintf(st, sizeof st, "%s/states", T);
	library_init(f, c, s, st);
	CHECK(library_load() == 0, "no library file yet");

	snprintf(buf, sizeof buf, "%s", "x");
	{
		char d[600];
		snprintf(d, sizeof d, "%s/discs", T);
		CHECK(library_add_folder(d) == 1, "folder added");
		CHECK(library_add_folder(d) == 0, "the same folder twice: no");
	}
	n = library_count();
	CHECK(n == 5, "5 games: Game One (cue), Other (iso), Multi (m3u), Homebrew (exe), Deep (subfolder chd)");
	if (n != 5) {
		int i;
		for (i = 0; i < n; i++)
			fprintf(stderr, "  %s | %s\n", library_game(i)->name, library_game(i)->path);
	}
	g = by_name("Game One");
	CHECK(g && !strcmp(g->serial, "SLES12345"), "serial from the raw .bin through the .cue");
	CHECK(g && !strcmp(library_region(g->serial), "PAL"), "SLES = PAL");
	CHECK(g && strstr(g->cover, "Game One.jpg"), "cover next to the disc");
	g = by_name("Other");
	CHECK(g && !strcmp(g->serial, "SLUS00001"), "serial from the .iso");
	CHECK(g && !strcmp(library_region(g->serial), "NTSC-U"), "SLUS = NTSC-U");
	CHECK(g && strstr(g->cover, "covers/SLUS00001.jpg"), "cover in our covers folder by serial");
	CHECK(by_name("Multi") != NULL, "the .m3u is listed");
	CHECK(by_name("disc1") == NULL && by_name("disc2") == NULL, "discs in the .m3u aren't");
	CHECK(by_name("loose") == NULL, "a .bin with no .cue isn't");
	CHECK(by_name("Deep") != NULL, "subfolders are scanned");
	CHECK(!strcmp(library_region("SLPS01234"), "NTSC-J") && !strcmp(library_region(""), ""), "regions");

	/* states counted from the states folder */
	g = by_name("Game One");
	CHECK(g && g->states == 2, "two save states");

	/* played: recent, and a game outside the folders */
	{
		char p[600];
		snprintf(p, sizeof p, "%s/elsewhere/Dropped.iso", T);
		g = library_note_played(p, "SCES00002");
		CHECK(g && g->plays == 1 && g->last && (g->source & LIB_PLAYED), "a dropped game is added");
		CHECK(library_count() == 6, "6 now");
	}
	g = by_name("Other");
	g = library_note_played(g->path, NULL);
	g->last += 10;          /* most recent */
	n = library_recent(rec, 8);
	CHECK(n == 2 && !strcmp(rec[0]->name, "Other") && !strcmp(rec[1]->name, "Dropped"), "recent order");

	/* sorting */
	library_sort(SORT_NAME);
	CHECK(!strcmp(library_game(0)->name, "Deep"), "sorted by name");
	library_sort(SORT_LAST);
	CHECK(!strcmp(library_game(0)->name, "Other"), "sorted by last played");

	/* per-game settings */
	g = by_name("Game One");
	CHECK(library_setting(g, "pcsx_rearmed_neon_enhancement_enable", buf, sizeof buf) == NULL, "none yet");
	CHECK(library_set_setting(g, "pcsx_rearmed_neon_enhancement_enable", "enabled"), "set");
	CHECK(library_set_setting(g, "pad1.type", "dualshock"), "set another");
	CHECK(library_set_setting(g, "pcsx_rearmed_neon_enhancement_enable", "disabled"), "change");
	CHECK(library_setting(g, "pcsx_rearmed_neon_enhancement_enable", buf, sizeof buf) &&
	      !strcmp(buf, "disabled"), "changed value read back");
	CHECK(library_setting(g, "pad1.type", buf, sizeof buf) && !strcmp(buf, "dualshock"), "other kept");
	library_set_setting(g, "pad1.type", NULL);
	CHECK(library_setting(g, "pad1.type", buf, sizeof buf) == NULL, "removed");
	{
		char p[700];
		library_settings_path(g, p, sizeof p);
		CHECK(strstr(p, "games/SLES12345") != NULL, "settings file named by serial");
	}

	/* save and load back */
	CHECK(library_save(), "saved");
	library_init(f, c, s, st);
	CHECK(library_count() == 0, "emptied");
	CHECK(library_load() == 1 && library_count() == 6 && library_folders() == 1, "loaded back");
	g = by_name("Dropped");
	CHECK(g && !strcmp(g->serial, "SCES00002") && g->plays == 1, "details kept");

	/* rescan keeps played games whose files went; drops scanned ones */
	{
		char p[600];
		snprintf(p, sizeof p, "%s/discs/Other.iso", T);
		rename(p, "/tmp/libtest-moved.iso");
		library_scan();
		g = by_name("Other");
		CHECK(g && g->source == LIB_PLAYED, "played game kept, no longer scanned");
		rename("/tmp/libtest-moved.iso", p);
	}
	library_remove_folder(0);
	CHECK(library_folders() == 0 && by_name("Deep") == NULL && by_name("Dropped") != NULL,
	      "folder removed with its unplayed games");

	fprintf(stderr, "test_library: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
