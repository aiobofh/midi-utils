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
 * This is a simplified implementation for allocation resources and initalise
 * MIDI ports witn an ALSA sequencer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>

#include "error.h"
#include "sequencer.h"

/*
 * Allocate and initialize the MIDI interfaces.
 */
snd_seq_t *sequencer_new(int *in_port_ptr,
                         int *out_port_ptr,
                         char *port_name) {

  snd_seq_t *seq_handle;

  char input_name[255];
  char output_name[255];
  int mode = 0;

  sprintf(input_name, "%s - In", port_name);
  sprintf(output_name, "%s - Out", port_name);

  if (NULL != in_port_ptr) {
    mode = mode | SND_SEQ_OPEN_INPUT;
  }
  if (NULL != out_port_ptr) {
    mode = mode | SND_SEQ_OPEN_OUTPUT;
  }


  /*
   * Open an ALSA MIDI input and output ports.
   */
  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    error("Error opening ALSA sequencer for '%s'.", port_name);
  }
  snd_seq_set_client_name(seq_handle, port_name);

  if (in_port_ptr) {
    *in_port_ptr = snd_seq_create_simple_port(seq_handle, input_name,
                                              SND_SEQ_PORT_CAP_WRITE |
                                              SND_SEQ_PORT_CAP_SUBS_WRITE,
                                              SND_SEQ_PORT_TYPE_APPLICATION);
    if (*in_port_ptr < 0) {
      error("Error creating sequencer input port for '%s'.", port_name);
    }
  }

  if (out_port_ptr) {
    *out_port_ptr = snd_seq_create_simple_port(seq_handle, output_name,
                                               SND_SEQ_PORT_CAP_READ |
                                               SND_SEQ_PORT_CAP_SUBS_READ,
                                               SND_SEQ_PORT_TYPE_APPLICATION);
    if (*out_port_ptr < 0) {
      error("Error creating sequencer output port for '%s'.", port_name);
    }
  }

  return seq_handle;
}


/*
 * Cleanup MIDI interfaces and locked resources.
 */
void sequencer_delete(snd_seq_t *seq_handle) {
  snd_seq_close(seq_handle);
}


/*
 * Allocate and initiate a new MIDI event poller.
 */
struct pollfd *sequencer_poller_new(snd_seq_t *seq_handle,
					   int *npfd_ptr) {
  struct pollfd *pfd;
  /*
   * Prepeare event polling on the MIDI input.
   */
  *npfd_ptr = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
  pfd = (struct pollfd *)malloc(*npfd_ptr * sizeof(struct pollfd));
  snd_seq_poll_descriptors(seq_handle, pfd, *npfd_ptr, POLLIN);

  return  pfd;
}


/*
 * Cleanup a MIDI event poller.
 */
void sequencer_poller_delete(struct pollfd *pfd) {
  free(pfd);
}


