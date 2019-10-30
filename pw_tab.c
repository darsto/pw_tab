/*-
 * The MIT License
 *
 * Copyright 2019 Darek Stojaczyk
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>

/* we want to use pointers in our game structs for convenience,
 * so make sure this lib is compiled as 32bit, just like the original
 * PW client
 */

/* real game structures
 * fields named "unk" are simply unknown and are used only
 * as a padding.
 */

struct player {
	char unk1[60];
	float pos_x;
	float pos_z;
	float pos_y;
	char unk2[1072];
	uint32_t hp;
	char unk3[1436];
	uint32_t target_id;
};

struct mob {
	char unk1[60];
	float pos_x;
	float pos_z;
	float pos_y;
	char unk2[108];
	uint32_t type; /* 6=mob, 7=npc */
	char unk3[100];
	uint32_t id;
	char unk4[12];
	uint32_t hp;
	char unk5[236];
	uint32_t disappear_count; /* number of ticks after mob death */
	uint32_t disappear_maxval; /* ticks before the mob disappears */
	char unk6[128];
	uint32_t state; /* walking, chasing, returning, or just-died */
};

struct mob_list {
	char unk1[20];
	uint32_t count;
	char unk2[56];
	struct mob_array {
		struct mob *mob[0];
	} *mobs;
};

struct world_objects {
	char unk1[32];
	char unk2[4]; /* player list? */
	struct mob_list *moblist;
};

struct game_data {
	char unk1[8];
	struct world_objects *wobj;
	char unk2[20];
	struct player *player;
	char unk3[108];
	uint32_t logged_in; /* 2 = logged in */
};

struct app_data {
	struct game_data *game;
};

static HWND g_window;
static WNDPROC g_orig_event_handler;

static uint32_t g_base_addr;
static struct app_data *g_app;

static void
find_pwi_game_data(void)
{
	DWORD_PTR module_base_addr = 0;
	MODULEENTRY32 module_entry;
	HANDLE snapshot;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) {
		goto err;
	}

	module_entry.dwSize = sizeof(MODULEENTRY32);

	if (Module32First(snapshot, &module_entry)) {
		do {
			if (_tcsicmp(module_entry.szModule, _T("game.exe")) == 0) {
				module_base_addr = (DWORD_PTR)module_entry.modBaseAddr;
				break;
			}
		} while (Module32Next(snapshot, &module_entry));
	}
	CloseHandle(snapshot);

	if (module_base_addr == 0) {
		goto err;
	}

	g_base_addr = module_base_addr;
	g_app = (struct app_data *)((DWORD)module_base_addr + 0x52764C);
	return;

err:
	MessageBox(NULL, "Failed to retrieve PWI base address", "Status", MB_OK);
}

static BOOL CALLBACK
window_enum_cb(HWND window, LPARAM _unused)
{
	int length = GetWindowTextLength(window);
	char buf[64];

	GetWindowText(window, buf, sizeof(buf));
	if (strcmp(buf, "Element Client") == 0) {
		g_window = window;
		return FALSE;
	}

	return TRUE;
}

static void
hook_pwi_window_event_handler(LONG event_handler)
{
	EnumWindows(window_enum_cb, 0);
	if (g_window == 0) {
		MessageBox(NULL, "Failed to find the PWI game window", "Status", MB_OK);
		return;
	}

	g_orig_event_handler = (WNDPROC)SetWindowLong(g_window, GWL_WNDPROC, event_handler);
}

static void
select_mob(uint32_t id)
{
	uint32_t _sel_mob_call_addr = g_base_addr + 0x1A8080;

	/* we could technically just set player->target_id and
	 * it will select proper mob in the game, but the target hp bar
	 * will be all black as if the mob had 0 HP (that's what the game
	 * thinks actually). To get the mob HP, we need to send a request
	 * to the server, so here we just call the actual in-game function
	 * that's called whenever the user selects some monster with mouse
	 */
	asm (
		"push %1\n\t"
		"call *%0"
		:: "r" (_sel_mob_call_addr), "r" (id)
		: "sp"
	);
}


static void
select_closest_mob(void)
{
	struct game_data *game = g_app->game;
	struct player *player = game->player;
	uint32_t mobcount = game->wobj->moblist->count;
	float min_dist = 999;
	uint32_t new_target_id = player->target_id;
	struct mob *mob;

	for (int i = 0; i < mobcount; i++) {
		float dist, dist_tmp;

		mob = game->wobj->moblist->mobs->mob[i];
		if (mob->type != 6) {
			/* not a monster */
			continue;
		}

		if (mob->disappear_count > 0) {
			/* mob is dead and already disappearing */
			continue;
		}

		dist_tmp = mob->pos_x - player->pos_x;
		dist_tmp *= dist_tmp;
		dist = dist_tmp;

		dist_tmp = mob->pos_z - player->pos_z;
		dist_tmp *= dist_tmp;
		dist += dist_tmp;

		dist_tmp = mob->pos_y - player->pos_y;
		dist_tmp *= dist_tmp;
		dist += dist_tmp;

		dist = sqrt(dist);
		if (dist < min_dist) {
			min_dist = dist;
			new_target_id = mob->id;
		}
	}

	select_mob(new_target_id);
}


static LRESULT CALLBACK
event_handler(HWND window, UINT event, WPARAM data, LPARAM _unused)
{
	switch(event) {
	case WM_KEYDOWN:
		if (g_app && g_app->game && g_app->game->logged_in == 2) {
			if (data== VK_TAB) {
				select_closest_mob();
				return TRUE;
			}
		}
		break;
	default:
		break;
	}

	/* let the game handle this key */
	return CallWindowProc(g_orig_event_handler, window, event, data, _unused);
}

static DWORD WINAPI
ThreadMain(LPVOID _unused)
{
	/* let the game start */
	Sleep(8 * 1000);

	/* find and init some game data */
	find_pwi_game_data();

	/* init the input handling */
	hook_pwi_window_event_handler((LONG)event_handler);

	return 0;
}

BOOL APIENTRY
DllMain(HMODULE mod, DWORD reason, LPVOID _reserved)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DWORD tid;

		DisableThreadLibraryCalls(mod);
		CreateThread(NULL, 0, ThreadMain, NULL, 0, &tid);
		return TRUE;
	}
	case DLL_PROCESS_DETACH:
		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}
