/*
 * note2note.c
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
 * Simple MIDI note-to-note converter. Original idea was to translate
 * digital percusion notes to other notes. For example to be able to 
 * in a simple way map digital drums against varous softwares and hardwares.
 *
 * Remember it is a hack!!! I do not take any responsibility for your
 * system. :)
 *
 * Requirements
 * ------------
 *
 * libasound2-dev
 *
 * How-to Compile
 * --------------
 *
 * Just run GCC :)
 *
 * gcc -o note2note note2note.c -lasound -Wall -pedantic -std=c99
 *
 * How-to run
 * ----------
 *
 * note2note -c configfile.n2n
 *
 * Example config file (roland_td9-mssiah_sid.n2n)
 * -----------------------------------------------
 *
 * note2note-config-1.0
 * Roland TD-9/MSSIAH
 * 26:46
 * 47:44
 * 48:43
 * 46:42
 * 45:41
 * 43:41
 * 50:39
 * 38:38
 * 36:36
 *
 * That's about it :) Enjoy!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "quit.h"

#define APPNAME "note2note"
#define VERSION "1.0.0"

static int debug = 0;
static int quit = 0;


/*
 * Command usage
 */
static void usage(char *app_name) {
  printf("USAGE: %s -c <filename> [-dvh]\n"
	 " -h, --help        Show this help text.\n"
	 " -v, --version     Display version information.\n"
	 " -c, --config=file Note translation configuration file to load.\n"
	 " -d, --debug       Output debug information.\n"
	 "\n"
	 "Author: AiO\n", app_name);
}


/*
 * Signal handler callback routine, basically just make the program exit
 * in a nice and proper way.
 */
static void quit_callback(int sig) {
  signal(sig, quit_callback);
  if (debug) {
    printf("DEBUG: Quitting...\n");
  }
  quit = 1;
}


/*
 * Allocate and initialize the MIDI interfaces.
 */
static snd_seq_t *sequencer_new(int *in_port_ptr,
				int *out_port_ptr,
				char *port_name) {

  snd_seq_t *seq_handle;

  /*
   * Open an ALSA MIDI input and output ports.
   */
  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    fprintf(stderr, "Error opening ALSA sequencer.\n");
    exit(2);
  }
  snd_seq_set_client_name(seq_handle, port_name);

  *in_port_ptr = snd_seq_create_simple_port(seq_handle, "In",
					    SND_SEQ_PORT_CAP_WRITE |
					    SND_SEQ_PORT_CAP_SUBS_WRITE,
					    SND_SEQ_PORT_TYPE_APPLICATION);
  if (*in_port_ptr < 0) {
    fprintf(stderr, "Error creating sequencer input port.\n");
    exit(3);
  }

  *out_port_ptr = snd_seq_create_simple_port(seq_handle, "Out",
					     SND_SEQ_PORT_CAP_READ |
					     SND_SEQ_PORT_CAP_SUBS_READ,
					     SND_SEQ_PORT_TYPE_APPLICATION);
  if (*out_port_ptr < 0) {
    fprintf(stderr, "Error creating sequencer output port.\n");
    exit(4);
  }

  return seq_handle;
}


/*
 * Cleanup MIDI interfaces and locked resources.
 */
static void sequencer_delete(snd_seq_t *seq_handle) {
  snd_seq_close(seq_handle);
}


/*
 * Allocate and initiate a new MIDI event poller.
 */
static struct pollfd *sequencer_poller_new(snd_seq_t *seq_handle,
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
static void sequencer_poller_delete(struct pollfd *pfd) {
  free(pfd);
}


/*
 * Parse configuration file and construct a translation table.
 */
static void translation_table_init(const char *filename,
				   int translation_table[256],
				   char *port_name) {
  FILE *fd;
  int line_number = 0;
  int i; 

  if ((fd = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "ERROR: Unable to open file '%s'.\n", filename);
    exit(EXIT_FAILURE);
  }

  if (debug) {
    printf("DEBUG: Reading file '%s'\n", filename);
  }
  
  /*
   * No translation, by default.
   */
  for (i = 0; i < 255; i++) {
    translation_table[i] = i;
  }

  /*
   * Read each line of the configuration file and assign notes accordingly.
   */
  while (!feof(fd)) {
    int from, to;
    errno = 0;
    line_number++;

    /*
     * Parse the current line. First line is the file version, the second
     * line is the name of th MIDI-port to upen and the rest is the actual
     * MIDI note conversion table defintion.
     */
    if (line_number == 1) {
      /*
       * Make sure that we can hande the file version :)
       */
      fscanf(fd, "note2note-config-1.0\n");

      if (errno != 0) {
	fprintf(stderr, "ERROR: The file is not a note2note config file.\n");
	exit(EXIT_FAILURE);
      }
      continue;
    }
    else if (line_number == 2) {
      /*
       * TODO: Make this a bit cleaner :)
       */
      /*
       * Get the name of the instance from the config file.
       */
      fgets(port_name, 255, fd);
      /*
       * Trim it and make sure that we have a null-terminated string.
       */
      port_name[strlen(port_name)-1] = 0;

      if (debug) {
	printf("DEBUG: Read port name '%s'\n", port_name);
      }

      if (errno != 0) {
	fprintf(stderr, "ERROR: Could not get MIDI port name from config "
		"file.n");
	exit(EXIT_FAILURE);
      }
      continue;
    }
    else {
      /*
       * Get note translation values.
       */
      fscanf(fd, "%d:%d\n", &from, &to);

      if (errno != 0) {
	fprintf(stderr, "ERROR: Error reading file '%s'.\n", filename);
	exit(EXIT_FAILURE);
      }

      if ((from < 0) || (from > 255)) {
	fprintf(stderr, "ERROR: Line %d of '%s' has an invalid from-note"
		" (must be 0-255).\n",
		line_number, filename);
	exit(EXIT_FAILURE);
      }
      
      if ((to < 0) || (to > 255)) {
	fprintf(stderr, "ERROR: Line %d of '%s' has an invalid to-note"
		" (must be 0-255).\n",
		line_number, filename);
	exit(EXIT_FAILURE);
      }
      
      if (debug) {
	printf("DEBUG: Note %d will %d\n", from, to);
      }

      /*
       * Inser the note tranform in the translation table.
       */
      translation_table[from] = to;
    }
  }

  if (debug) {
    printf("DEBUG: Reached end of file '%s'\n", filename);
  }

  fclose(fd);
}

/*
 * Main event loop.
 */
static void note2note(snd_seq_t *seq_handle,
		      struct pollfd *pfd,
		      int npfd,
		      int in_port,
		      int out_port,
		      int translation_table[256]) {
  /*
   * Note parameters
   */
  snd_seq_event_t *ev;

  /*
   * Poll the ALSA midi interface for events.
   */
  if (poll(pfd, npfd, 100) > 0) {      
    /*
     * Loop over all events. (While at the end)
     */
    do {
      /*
       * Get the event information.
       */
      snd_seq_event_input(seq_handle, &ev);
      snd_seq_ev_set_subs(ev);  
      snd_seq_ev_set_direct(ev);
      
      /*
       * Someone ordered a steaming plate of debug?
       */
      if (debug) {
	if ((ev->type == SND_SEQ_EVENT_NOTEON) ||
	    (ev->type == SND_SEQ_EVENT_NOTEOFF)) {
	  printf("DEBUG: Note: %d Velocity: %d Channel %d\n",
		 ev->data.note.note,
		 ev->data.note.velocity,
		 ev->data.note.channel);
	} else if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
	  printf("DEBUG: Param: %d Value: %d\n",
		 ev->data.control.param,
		 ev->data.control.value);
	}
      }

      /*
       * Translate note.
       */
      if ((ev->type == SND_SEQ_EVENT_NOTEON) ||
	  (ev->type == SND_SEQ_EVENT_NOTEOFF)) {
	if (debug) {
	  if (ev->data.note.note != translation_table[ev->data.note.note]) {
	    printf("DEBUG: Translating %d to %d\n",
		   ev->data.note.note,
		   translation_table[ev->data.note.note]);
	  }
	}
	ev->data.note.note = translation_table[ev->data.note.note];
	ev->data.note.channel = 0;
      }

      /*
       * Output the translated note to the MIDI output port.
       */
      snd_seq_ev_set_source(ev, out_port);
      snd_seq_event_output_direct(seq_handle, ev);
      
      snd_seq_free_event(ev);

    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
  }
}


/*
 * Main function of note2note.
 */
int main(int argc, char *argv[]) {

  char *app_name = APPNAME;
  char port_name[255];
  char *config_file = NULL;

  /*
   * Handles and IDs to ALSA stuff.
   */
  snd_seq_t *seq_handle;
  int in_port, out_port;
  int npfd;
  struct pollfd *pfd;

  /*
   * This is where the note translation-table is stored.
   */
  int translation_table[256];

  /*
   * Command line options variables.
   */
  static struct option long_options[] = {
    {"debug", no_argument, NULL,  'd'},
    {"config", required_argument, NULL,  'c'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {0, 0, 0,  0 }
  };

  /*
   * Handle command line options.
   */
  while(1) {
    int option_index = 0;
    int c;
    c = getopt_long(argc, argv, "dc:hv?",
		    long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
    case 'd':
      debug = 1;
      break;
    case 'c':
      config_file = optarg;
      break;
    case 'v':
      printf("%s %s", app_name, VERSION);
      exit(EXIT_SUCCESS);
      break;
    case '?':
    case 'h':
      usage(argv[0]);
      exit(EXIT_SUCCESS);
      break;
    default:
      fprintf(stderr, "Unknown parameter %c.\n", c);
      usage(argv[0]);
      exit(EXIT_FAILURE);
      break;
    }
  }

  /*
   * Make sure that the all so important configuration file is provided.
   */
  if (config_file == NULL) {
    fprintf(stderr, "ERROR: No configuration file provided.\n");
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }  

  /*
   * Read the configuration file and get all the essential information.
   */
  translation_table_init(config_file, translation_table, port_name);

  /*
   * Set-up ALSA MIDI.
   */
  seq_handle = sequencer_new(&in_port, &out_port, port_name);
  pfd = sequencer_poller_new(seq_handle, &npfd);
  
  /*
   * Ensure a clean exit in as many situations as possible.
   */
  quit_init(quit_callback);
  
  /*
   * Main loop.
   */
  while (!quit) {
    note2note(seq_handle, pfd, npfd, in_port, out_port, translation_table);
  }
  
  /*
   * Cleanup resources.
   */
  sequencer_poller_delete(pfd);
  sequencer_delete(seq_handle);

  /*
   * Make a clean exit.
   */
  exit(EXIT_SUCCESS);

  /*
   * Just to skip compiler warning about not returning from a non-void-
   * function.
   */
  return 0;
}