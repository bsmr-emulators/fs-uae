/*
* UAE - The Un*x Amiga Emulator
*
* Event stuff
*
* Copyright 1995-2002 Bernd Schmidt
* Copyright 1995 Alessandro Bissacco
* Copyright 2000-2012 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "memory.h"
#include "newcpu.h"
#include "uae/ppc.h"

static const int pissoff_nojit_value = 256 * CYCLE_UNIT;

unsigned long int event_cycles, nextevent, currcycle;
int is_syncline, is_syncline_end;
long cycles_to_next_event;
long max_cycles_to_next_event;
long cycles_to_hsync_event;
unsigned long start_cycles;
bool event_wait;

frame_time_t vsyncmintime, vsyncmaxtime, vsyncwaittime;
int vsynctimebase;
int event2_count;

void events_schedule (void)
{
	int i;

	unsigned long int mintime = ~0L;
	for (i = 0; i < ev_max; i++) {
		if (eventtab[i].active) {
			unsigned long int eventtime = eventtab[i].evtime - currcycle;
			if (eventtime < mintime)
				mintime = eventtime;
		}
	}
	nextevent = currcycle + mintime;
}

void do_cycles_slow (unsigned long cycles_to_add)
{
	if ((pissoff -= cycles_to_add) >= 0)
		return;

	cycles_to_add = -pissoff;
	pissoff = 0;

	while ((nextevent - currcycle) <= cycles_to_add) {
		int i;

		/* Keep only CPU emulation running while waiting for sync point. */
		if (is_syncline) {
			if (!vblank_found_chipset) {
				if (is_syncline > 0) {
					int rpt = read_processor_time ();
					int v = rpt - vsyncmintime;
					int v2 = rpt - is_syncline_end;
					if (v > vsynctimebase || v < -vsynctimebase) {
						v = 0;
					}
					if (v < 0 && v2 < 0 && event_wait) {

#ifdef WITH_PPC
						if (ppc_state) {
							if (is_syncline == 1) {
								uae_ppc_execute_check();
							} else {
								uae_ppc_execute_quick();
							}
						}
#endif
						if (currprefs.cachesize)
							pissoff = pissoff_value;
						else
							pissoff = pissoff_nojit_value;
						return;
					}
				} else if (is_syncline < 0) {
					int rpt = read_processor_time ();
					int v = rpt - is_syncline_end;
					if (v < 0 && event_wait) {

#ifdef WITH_PPC
						if (ppc_state) {
							uae_ppc_execute_check();
						}
#endif
						if (currprefs.cachesize)
							pissoff = pissoff_value;
						else
							pissoff = pissoff_nojit_value;
						return;
					}
				}
			}
			is_syncline = 0;
		}

		cycles_to_add -= nextevent - currcycle;
		currcycle = nextevent;

		for (i = 0; i < ev_max; i++) {
			if (eventtab[i].active && eventtab[i].evtime == currcycle) {
				if (eventtab[i].handler == NULL) {
					gui_message(_T("eventtab[%d].handler is null!\n"), i);
					eventtab[i].active = 0;
				} else {
					(*eventtab[i].handler)();
				}
			}
		}
		events_schedule ();


	}
	currcycle += cycles_to_add;
}

void MISC_handler (void)
{
	static bool dorecheck;
	bool recheck;
	int i;
	evt mintime;
	evt ct = get_cycles ();
	static int recursive;

	if (recursive) {
		dorecheck = true;
		return;
	}
	recursive++;
	eventtab[ev_misc].active = 0;
	recheck = true;
	while (recheck) {
		recheck = false;
		mintime = ~0L;
		for (i = 0; i < ev2_max; i++) {
			if (eventtab2[i].active) {
				if (eventtab2[i].evtime == ct) {
					eventtab2[i].active = false;
					event2_count--;
					eventtab2[i].handler (eventtab2[i].data);
					if (dorecheck || eventtab2[i].active) {
						recheck = true;
						dorecheck = false;
					}
				} else {
					evt eventtime = eventtab2[i].evtime - ct;
					if (eventtime < mintime)
						mintime = eventtime;
				}
			}
		}
	}
	if (mintime != ~0UL) {
		eventtab[ev_misc].active = true;
		eventtab[ev_misc].oldcycles = ct;
		eventtab[ev_misc].evtime = ct + mintime;
		events_schedule ();
	}
	recursive--;
}


void event2_newevent_xx (int no, evt t, uae_u32 data, evfunc2 func)
{
	evt et;
	static int next = ev2_misc;

	et = t + get_cycles ();
	if (no < 0) {
		no = next;
		for (;;) {
			if (!eventtab2[no].active) {
				event2_count++;
				break;
			}
			if (eventtab2[no].evtime == et && eventtab2[no].handler == func && eventtab2[no].data == data)
				break;
			no++;
			if (no == ev2_max)
				no = ev2_misc;
			if (no == next) {
				write_log (_T("out of event2's!\n"));
				return;
			}
		}
		next = no;
	}
	eventtab2[no].active = true;
	eventtab2[no].evtime = et;
	eventtab2[no].handler = func;
	eventtab2[no].data = data;
	MISC_handler ();
}

int current_hpos (void)
{
	int hp = current_hpos_safe ();
	if (hp < 0 || hp > 256) {
		gui_message(_T("hpos = %d!?\n"), hp);
		hp = 0;
	}
	return hp;
}

