/*
 * sequencer.h
 *
 * Copyright (C)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * About
 * -----
 *
 * Author: AiO <aio at aio dot nu>
 *
 * This is a simplified API for allocating resources and initialise MIDI
 * ports with an ALSA sequencer.
 *
 */

#ifndef _SEQUENCER_H_
#define _SEQUENCER_H_

#include <alsa/asoundlib.h>

/*
 * Allocate and initialize the MIDI interfaces.
 */
snd_seq_t *sequencer_new(int *in_port_ptr, int *out_port_ptr, char *port_name);


/*
 * Cleanup MIDI interfaces and locked resources.
 */
void sequencer_delete(snd_seq_t *seq_handle);


/*
 * Allocate and initiate a new MIDI event poller.
 */
struct pollfd *sequencer_poller_new(snd_seq_t *seq_handle, int *npfd_ptr);


/*
 * Cleanup a MIDI event poller.
 */
void sequencer_poller_delete(struct pollfd *pfd);


#endif /* _SEQUENCER_H_ */
