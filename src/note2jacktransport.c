/*
 * note2jacktransport.c
 * ====================
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
 *
 * About
 * -----
 *
 * Author: AiO <aio at aio dot nu>
 *
 * This tool enables a _hard_coded_ way to make a generic controlsurface
 * translate into Jack Transport commands.
 *
 * The code in this tool is a real fast-hack to solve a specific problem,
 * hence the low quality of the code :(
 *
 * Requirements
 * ------------
 *
 * libasound2-dev
 * libjack-jackd2-dev
 *
 * How-to Compile
 * --------------
 *
 * Just run GCC :)
 *
 * gcc -o note2jacktransport note2jacktransport.c -lasound -ljack -Wall \
 * -pedantic -std=c99
 *
 * TODO: Make this hack a bit nicer since it's an old version and not really
 *       looked after some rewrite is required to integrate into midi-utils.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/transport.h>

#include "quit.h"

#define APPNAME "note2jacktransport"
#define VERSION "0.0.1"

enum Etransports {
  STOP,
  PLAY,
  REV,
  FWD,
  WHEEL,
  UNKNOWN
};

enum Equit {
  NONE,
  JACK_DIED,
  GOT_SIGNAL
};

static int quit = 0;

static void jack_shutdown(void *arg) {
  quit = JACK_DIED;
}

static void quit_callback(int sig) {
  signal(sig, quit_callback);
  quit = GOT_SIGNAL;
}

static void jack_reposition(jack_client_t *jack_client, double sec) {
  jack_nframes_t frame=rint(jack_get_sample_rate(jack_client) * sec);
  jack_transport_locate(jack_client, frame);
}

static double jack_get_position(jack_client_t *jack_client) {
  jack_position_t jack_position;
  double          jack_time;

  /*
   * Calculate frame.
   */
  jack_transport_query(jack_client, &jack_position);
  jack_time = jack_position.frame / (double) jack_position.frame_rate;

  return(jack_time);
}

static double jack_get_bpm(jack_client_t *jack_client) {
  jack_position_t tpos;
  jack_transport_query(jack_client, &tpos);
  
  if (tpos.valid & JackPositionBBT) {
    return tpos.beats_per_minute;
  }
  return 120.0;
}

static double jack_prev_beat(jack_client_t *jack_client) {
  double position = jack_get_position(jack_client);
  double bpm = jack_get_bpm(jack_client);
  int beats = ceil(bpm * ((position - 0.1) / 60.0)) - 1;
  double prev_beat_position = (double)beats / bpm * 60.0;
  if (prev_beat_position < 0) {
    return 0;
  }
  return prev_beat_position;
}

static double jack_next_beat(jack_client_t *jack_client) {
  double position = jack_get_position(jack_client);
  double bpm = jack_get_bpm(jack_client);
  int beats = floor(bpm * ((position + 0.1) / 60.0)) + 1;
  double next_beat_position = (double)beats / bpm * 60.0;
  return next_beat_position;
}

static double jack_move_partial_beat(jack_client_t *jack_client, int count) {
  double position = jack_get_position(jack_client);
  double bpm = jack_get_bpm(jack_client);
  double beats = bpm * (position / 60.0) + 0.0625 * count;
  double new_position = beats / bpm * 60.0;
  if (new_position < 0) {
    return 0;
  }
  return new_position;
}

int main(int argc, char *argv[]) {

  snd_seq_t *seq_handle;

  int in_port;

  int npfd;
  struct pollfd *pfd;
  snd_seq_event_t *ev;
  int i;
  int notes[UNKNOWN];

  char *app_name = APPNAME;

  jack_client_t *jack_client = NULL;

  int param;
  int value;

  /*
   * TODO: Read this from a n2j config file similar to n2n.
   */
  notes[STOP] = 93;
  notes[PLAY] = 94; 
  notes[REV] = 91;
  notes[FWD] = 92;
  notes[WHEEL] = 127;

  /*
   * Initiate as a jack client.
   *
   * TODO: Use jack2 jack_client_open() instead, since jack_client_new()
   *       is depricated.
   */
  if (!(jack_client = jack_client_new(app_name))) {
    fprintf(stderr, "Could not connect to the jack server.\n");
    exit(1);
  }
  jack_on_shutdown(jack_client, jack_shutdown, 0);
  jack_activate(jack_client);

  /*
   * Open an ALSA MIDI input.
   */
  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    fprintf(stderr, "Error opening ALSA sequencer.\n");
    exit(2);
  }
  snd_seq_set_client_name(seq_handle, app_name);
  in_port = snd_seq_create_simple_port(seq_handle, "In",
                                       SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
                                       SND_SEQ_PORT_TYPE_APPLICATION);
  if (in_port < 0) {
    fprintf(stderr, "Error creating sequencer port.\n");
    exit(3);
  }

  quit_init(quit_callback);

  /*
   * Prepeare event polling on the MIDI input.
   */
  npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
  pfd = (struct pollfd *)malloc(npfd * sizeof(struct pollfd));
  snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);

  /*
   * Main loop
   */
  while (!quit) {
    /*
     * Poll the ALSA midi interface for events.
     */
    if (poll(pfd, npfd, 100) > 0) {      
      do {
        /*
         * Get the event informatino.
         */
        snd_seq_event_input(seq_handle, &ev);
        snd_seq_ev_set_subs(ev);  
        snd_seq_ev_set_direct(ev);

        if (ev->type == SND_SEQ_EVENT_NOTEON) {
          param = ev->data.note.note;
          value = ev->data.note.velocity;
	  if (value == 0) {
	    continue;
	  }
        } else if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
          param = ev->data.control.param;
          value = ev->data.control.value;
	  if (value == 0) {
	    continue;
	  }
        } else {
          fprintf(stderr, "Unknown event.\n");
          continue;
        }

        printf("Param/Note: %d Value/Velocity: %d\n", param, value);

        /*
         * Handle notes.
         */
        for (i = 0; i < UNKNOWN; i++) {
          if (notes[i] == param) {
            switch (i) {
            case PLAY:
              /*
               * If jack is rolling, the PLAY will be PAUSE
               */
              switch (jack_transport_query(jack_client, NULL)) {
              case JackTransportRolling:
                jack_transport_stop(jack_client);
                printf("PAUSE\n");
                break;
              case JackTransportStopped:
                jack_transport_start(jack_client);
                printf("PLAY\n");
                break;
              default:
                printf("unknown\n");
                break;
              }
              break;
            case STOP:
              switch (jack_transport_query(jack_client, NULL)) {
              case JackTransportRolling:
                /*
                 * If jack is rolling, the STOP will be PAUSE
                 */
                jack_transport_stop(jack_client);
                printf("PAUSE\n");
                break;
              case JackTransportStopped:
                /*
                 * If jack is rolling, the STOP will be stop at the beginning
                 */
                jack_reposition(jack_client, 0);
                jack_transport_start(jack_client);
                jack_transport_stop(jack_client);
                jack_reposition(jack_client, 0);
                printf("STOP\n");
                break;
              default:
                printf("unknown\n");
                break;
              }
              break;
            case REV:
              jack_reposition(jack_client, jack_prev_beat(jack_client));
              printf("PREV BEAT\n");
              break;
            case FWD:
              jack_reposition(jack_client, jack_next_beat(jack_client));
              printf("NEXT BEAT\n");
              break;
            case WHEEL:
              if (value > 64) {
                value = (128 - value) * -1;
              }
              jack_reposition(jack_client, jack_move_partial_beat(jack_client, value));
              printf("JOG\n");
              break;
            default:
              printf("THIS SHOULD NEVER HAPPEN\n");
              break;
            }
            break;
          }
        }
        snd_seq_free_event(ev);
      } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
    }  
  }

  free(pfd);

  if (quit == GOT_SIGNAL) {
    jack_deactivate(jack_client);
    jack_client_close(jack_client);
  }  

  snd_seq_close(seq_handle);

  exit(EXIT_SUCCESS);

  return 0;
}
