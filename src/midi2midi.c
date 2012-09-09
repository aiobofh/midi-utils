/*
 * midi2midi.c
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
 * Simple MIDI to MIDI converter. Original idea was to translate
 * digital percussion notes to other notes. For example to be able to
 * in a simple way map digital drums against various soft-wares and
 * hard-wares.
 *
 * Remember it is a hack!!! I do not take any responsibility for your
 * system. :)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/transport.h>

#include "error.h"
#include "debug.h"
#include "quit.h"
#include "sequencer.h"
#include "jack_transport.h"

#define APPNAME "midi2midi"
#define VERSION "1.1.0"


/*
 * Set this variable to 1 to exit the main loop cleanly.
 */
static int quit = 0;


/*
 * Type definition for all the supported translations that midi2midi can
 * perform.
 */
typedef enum {
  TT_NONE,
  TT_NOTE_TO_NOTE,
  TT_CC_TO_CC,
  TT_NOTE_TO_JACK,
  TT_NOTE_TO_MMC
} translation_type;


/*
 * Type definition for keeping track of the needed resource capabilities
 * for the running instance of this program.
 */
typedef enum {
  CB_NONE = 0,
  CB_ALSA_MIDI_IN = 1,
  CB_ALSA_MIDI_OUT = 2,
  CB_JACK_TRANSPORT_OUT = 4
} capability;


/*
 * Type definition for a translation table entry.
 */
typedef struct {
  translation_type type;
  char value;
  char last_value;
  int line;
} translation;


/*
 * Command usage providing a simple help for the user.
 */
static void usage(char *app_name) {
  printf("USAGE: %s [-c <file name>] [-n <client_name>] [-hvpd]\n\n"
	 " -h, --help                   Show this help text.\n"
	 " -v, --version                Display version information.\n"
	 " -c, --config=file            Note translation configuration file\n"
         "                              to load. See manual for file format.\n"
         " -n, --client_name=name       Name of the ALSA/Jack client. This\n"
         "                              overrides line 2 in the config file.\n"
         " -p, --program-repeat-prevent Prevent a program select on a MIDI\n"
         "                              device to repeated times.\n"
	 " -d, --debug                  Output debug information.\n"
	 "\n"
         "This tool is a useful MIDI proxy if you own studio equipment that\n"
         "will not speak to each other the way you want to. Just route your\n"
         "MIDI signals through an instance of this and make magic happen!\n"
         "\n"
	 "Author: AiO\n", app_name);
}


/*
 * Signal handler callback routine, basically just make the program exit
 * in a nice and proper way.
 */
static void quit_callback(int sig) {
  signal(sig, quit_callback);
  debug("DEBUG: Quitting with signal %d", sig);
  quit = 1;
}


/*
 * Parse the specified configuration file and construct translation tables
 * for both MIDI notes and MIDI Continuous Controls.
 */
static capability translation_table_init(const char *filename,
                                         translation note_table[255],
                                         translation cc_table[255],
                                         char *port_name,
                                         capability capabilities) {
  FILE *fd;
  int line_number = 0;
  int i;

  /*
   * Just set the translation tables for both notes and MIDI Continuous
   * Controls to defaults.
   */
  for (i = 0; i < 255; i++) {
    cc_table[i].type = note_table[i].type = TT_NONE;
    cc_table[i].value = note_table[i].value = i;
    cc_table[i].last_value = -1;
  }

  if (NULL == filename) {
    return capabilities;
  }

  /*
   * Open the specified configuration file.
   */
  if ((fd = fopen(filename, "r")) == NULL) {
    error("Unable to open file '%s'.", filename);
  }

  debug("Reading file '%s'", filename);

  /*
   * Read each line of the configuration file and assign notes accordingly.
   */
  while (!feof(fd)) {
    int from, to;
    char c;
    translation_type type;
    errno = 0;
    line_number++;

    /*
     * Parse the current line. First line is the file version, the second
     * line is the name of the MIDI-port to use and the rest is the actual
     * MIDI note conversion table definition.
     */
    if (1 == line_number) {
      /*
       * Make sure that we can handle the file version :)
       */
      fscanf(fd, "midi2midi-config-1.1\n");

      if (errno != 0) {
        debug("The file '%s' was no 1.1 file, trying 1.0", filename);

        /*
         * Just to be sure, start over.
         */
        rewind(fd);

        /*
         * We can also handle 1.0 configuration files.
         */
        fscanf(fd, "midi2midi-config-1.0\n");

        if (errno != 0) {
          error("The file '%s' is not a midi2midi configuration file.",
                filename);
        }
      }
      continue;
    }
    else if (2 == line_number) {
      char buf[255];
      /*
       * TODO: Make this a bit cleaner since there is a potential buffer
       *       overrun possibility here.
       */
      /*
       * Get the name of the instance from the configuration file.
       */
      fgets(buf, 255, fd);
      /*
       * Trim it and make sure that we have a null-terminated string.
       */
      buf[strlen(buf)-1] = 0;

      debug("Read port name '%s'", buf);

      /*
       * If the -n flag was not provided to the program, lets set the
       * port name to the line that was just found. Otherwise just ignore
       *i it.
       */
      if (strlen(port_name) > 1) {
        strncpy(port_name, buf, 255);
      }

      if (0 != errno) {
        error("Could not get MIDI port name from configuration file '%s'.",
              filename);
      }
      continue;
    }
    else {
      /*
       * Read each line of the configuration file and insert translations into
       * the table.
       */
      fscanf(fd, "%d%c%d\n", &from, &c, &to);

      debug("Reading line %d '%d%c%d'", line_number, from, c, to);

      if (0 != errno) {
	error("Error reading file '%s'.", filename);
      }

      /*
       * Valid separators are '>' for CC and ':' for notes.
       */
      switch (c) {
        case '>': {
          type = TT_CC_TO_CC;
	  debug("Identified line as TT_CC_TO_CC (%c)", c);
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_ALSA_MIDI_OUT);
          break;
        }
        case ':': {
          type = TT_NOTE_TO_NOTE;
	  debug("Identified line as TT_NOTE_TO_NOTE (%c)", c);
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_ALSA_MIDI_OUT);
          break;
        }
        case 'J': {
	  debug("Identified line as TT_NOTE_TO_JACK (%c)", c);
          type = TT_NOTE_TO_JACK;
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_JACK_TRANSPORT_OUT);
          break;
        }
        case 'M': {
	  debug("Identified line as TT_NOTE_TO_MMC (%c)", c);
          type = TT_NOTE_TO_MMC;
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_ALSA_MIDI_OUT);
          break;
        }
        default: {
          error("Separator '%c' is not valid in file '%s'.", c, filename);
          break;
        }
      }

      /*
       * Perform some sanity checking on the from-value (since it can only be
       * between 0 and 255 (or actually 0 and 127 in the MIDI world)
       */
      if ((0 > from) || (255 < from)) {
        error("Line %d of '%s' has an invalid from value (must be 0-255).",
              line_number, filename);
      }

      if (TT_NOTE_TO_JACK != type) {
        /*
         * Perform some sanity checking on the to-value (since it can only be
         * between 0 and 255 (or actually 0 and 127 in the MIDI world)
         */
        if ((0 > to) || (255 < to)) {
          error("Line %d of '%s' has an invalid to value (must be 0-255).",
                line_number, filename);
        }
      }
      else {
        /*
         * Hard wire translate all the possible jack translation values
         * and make them as similar as possible to MIDI Machine Control.
         */
        if (1 == to) {
          to = JT_STOP;
        }
        else if (2 == to) {
          to = JT_PLAY;
        }
        else if (4 == to) {
          to = JT_FWD;
        }
        else if (5 == to) {
          to = JT_REV;
        }
        else if (47 == to) {
          to = JT_WHEEL;
        }
        else {
          error("%d is not a valid jack transport value (1, 2, 4, 5, 47)",
                to);
        }
      }

      /*
       * Insert the note transformation in the translation table.
       */
      switch (type) {
        case TT_NOTE_TO_NOTE:
        case TT_NOTE_TO_JACK:
        case TT_NOTE_TO_MMC: {
          /*
           * Make sure that there are no duplicates in the note translation
           * table.
           */
          if (TT_NONE != note_table[from].type) {
            error("Note value %d is already translated on line %d with value "
                  "%d on line %d of file '%s'",
                  from, cc_table[from].line, cc_table[from].value, line_number,
                  filename);
          }
          note_table[from].type = type;
          note_table[from].value = to;

          break;
        }
        case TT_CC_TO_CC: {
          /*
           * Make sure that there are no duplicates in the MIDI Contentious
           * Control translation table.
           */
          if (TT_NONE != cc_table[from].type) {
            error("CC value %d is already translated on line %d with value %d"
                  "on line %d of file '%s'",
                  from, cc_table[from].line, cc_table[from].value, line_number,
                  filename);
          }
          cc_table[from].type = type;
          cc_table[from].value = to;

          break;
        }
        default: {
          error("Type %d is not implemented yet", type);

          break;
        }
      }
      continue;
    }
  }

  debug("Reached end of file '%s'", filename);

  fclose(fd);

  return capabilities;
}

/*
 * Main event loop.
 */
static void midi2midi(snd_seq_t *seq_handle,
                      jack_client_t *jack_client,
		      struct pollfd *pfd,
		      int npfd,
		      int in_port,
		      int out_port,
		      translation note_table[256],
                      translation cc_table[256],
                      int program_change_prevention) {
  /*
   * Note parameters
   */
  snd_seq_event_t *ev;

  /*
   * For now an instance which can not handle ALSA MIDI input at all is not
   * supported.
   */
  if (NULL == seq_handle) {
    return;
  }

  /*
   * Poll the ALSA midi interface for events.
   */
  if (poll(pfd, npfd, 100) > 0) {
    /*
     * Loop over all events. (While at the end)
     */
    do {
      int send_midi = 1;
      /*
       * Get the event information.
       */
      snd_seq_event_input(seq_handle, &ev);
      snd_seq_ev_set_subs(ev);
      snd_seq_ev_set_direct(ev);

      /*
       * Translate either a note or a CC command.
       */
      if (((SND_SEQ_EVENT_NOTEON == ev->type) ||
           (SND_SEQ_EVENT_NOTEOFF == ev->type)) &&
          (TT_NONE != note_table[ev->data.note.note].type)) {

        switch (note_table[ev->data.note.note].type) {

          case TT_NOTE_TO_NOTE: {
            /*
             * Prepare to just forward a translated note.
             */
            debug("Translating note %d to note %d",
                  ev->data.note.note,
                  note_table[ev->data.note.note].value);

            ev->data.note.note = note_table[ev->data.note.note].value;

            break;
          }
          case TT_NOTE_TO_JACK: {
            /*
             * Send a jack transport command.
             */
            jack_transport_send(jack_client,
                                note_table[ev->data.note.note].value,
                                ev->data.note.velocity);

            send_midi = 0;

            break;
          }
          default: {
            /*
             * Some note-translation that is not supported should
             * end-up here.
             */
            error("Note translation %d is not implemented yet.",
                  note_table[ev->data.note.note].type);

            break;
          }
        }

      }
      else if ((SND_SEQ_EVENT_CONTROLLER == ev->type) &&
	       (TT_NONE != cc_table[ev->data.control.param].type)) {
        /*
         * When midi2midi receives a MIDI Continuous Controller message and
         * the from-value (index) is set in the translation table for MIDI
         */
        switch (note_table[ev->data.control.param].type) {

          case TT_CC_TO_CC: {
            /*
             * Prepare to translate a MIDI Continuous Controller into another
             * MIDI Continuous Controller according to the configuration file.
             */
            debug("Translating MIDI CC %d to MIDI CC %d",
                  ev->data.control.param,
                  cc_table[ev->data.control.param].value);

            ev->data.control.param = cc_table[ev->data.control.param].value;

            break;
          }
          default: {
            error("MIDI Continuous Controller translation %d is not "
                  "implemented yet.",
                  note_table[ev->data.note.note].type);

            break;
          }
        }
      }
      else if ((SND_SEQ_EVENT_PGMCHANGE == ev->type) &&
	       (1 == program_change_prevention)) {
	/*
	 * Only translate a MIDI Continuous Controller into another MIDI
	 * Continuous Controller value if the parameter value actually
	 * changed.
	 */
	if (cc_table[ev->data.control.param].last_value !=
	    ev->data.control.value) {
	  /*
	   * The new value seem to be different than the last translated
	   * one so lets produce the translation.
	   */

	  debug("Program changed to %d value changed",
		ev->data.control.param,
		cc_table[ev->data.control.param].value);

	  ev->data.control.param = cc_table[ev->data.control.param].value;

	  cc_table[ev->data.control.param].last_value =
	    ev->data.control.value;

	}
	else {
	  debug("Preventing program change to %d since value did not change",
		ev->data.control.param);
	  send_midi = 0;
	}

      }

      /*
       * If a new MIDI event was prepared it must be forwarded.
       */
      if (1 == send_midi) {
        /*
         * Output the translated note to the MIDI output port.
         */
        snd_seq_ev_set_source(ev, out_port);
        snd_seq_event_output_direct(seq_handle, ev);
      }

      /*
       * Get back the system memory allocated for the incoming MIDI event.
       */
      snd_seq_free_event(ev);

    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
  }
}


/*
 * Main function of midi2midi.
 */
int main(int argc, char *argv[]) {

  char *app_name = APPNAME;
  char port_name[255];
  char *config_file = NULL;
  int program_change_prevention = 0;

  /*
   * This variable holds a bit pattern describing what resources need to be
   * allocated. (ALSA/Jack)
   */
  capability capabilities;

  /*
   * Handles and IDs to ALSA stuff.
   */
  snd_seq_t *seq_handle = NULL;
  int in_port, out_port;
  int *in_port_ptr, *out_port_ptr;
  int npfd = 0;
  struct pollfd *pfd;

  /*
   * Handles for Jack client stuff.
   */
  jack_client_t *jack_client = NULL;

  /*
   * This is where the note translation-table is stored.
   */
  translation note_table[255];
  translation cc_table[255];

  in_port_ptr = out_port_ptr = NULL;

  /*
   * Command line options variables.
   */
  static struct option long_options[] = {
    {"debug", no_argument, NULL,  'd'},
    {"config", required_argument, NULL,  'c'},
    {"name", required_argument, NULL, 'n'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {0, 0, 0,  0 }
  };

  strcpy(port_name, "Unknown name");

  /*
   * Handle command line options.
   */
  while(1) {
    int option_index = 0;
    int c;
    c = getopt_long(argc, argv, "dn:c:hpv?",
		    long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'd': {
        debug_enable();
        break;
      }
      case 'c': {
        config_file = optarg;
        break;
      }
      case 'p': {
        program_change_prevention = 1;
        /*
         * The repeated-program-change-prevention feature requires MIDI in and
         * out.
         */
        capabilities = CB_ALSA_MIDI_IN | CB_ALSA_MIDI_OUT;
        break;
      }
      case 'n': {
        strncpy(port_name, optarg, 254);
        break;
      }
      case 'v': {
        printf("%s %s", app_name, VERSION);
        exit(EXIT_SUCCESS);
        break;
      }
      case '?':
      case 'h': {
        usage(argv[0]);
        exit(EXIT_SUCCESS);
        break;
      }
      default: {
        error("Unknown parameter %c.", c);
        break;
      }
    }
  }

  /*
   * Make sure that the all so important configuration file is provided.
   */
  if ((config_file == NULL) && (strlen(port_name) ==  0)) {
    error("No configuration file, nor a client name was provided use %s "
          "-h for more information.",
          app_name);
  }

  /*
   * Read the configuration file and get all the essential information.
   */
  capabilities = translation_table_init(config_file,
                                        note_table,
                                        cc_table,
                                        port_name,
                                        capabilities);

  /*
   * Set-up ALSA MIDI and Jack Transport depending on how the program
   * instance is set-up.
   */
  if (CB_ALSA_MIDI_IN == (capabilities & (CB_ALSA_MIDI_IN))) {
    in_port_ptr = &in_port;
  }
  if (CB_ALSA_MIDI_IN == (capabilities & (CB_ALSA_MIDI_OUT))) {
    out_port_ptr = &out_port;
  }
  if ((NULL != in_port_ptr) || (NULL != out_port_ptr)) {
    seq_handle = sequencer_new(in_port_ptr, out_port_ptr, port_name);
    pfd = sequencer_poller_new(seq_handle, &npfd);
  }
  if (CB_JACK_TRANSPORT_OUT == (capabilities & CB_JACK_TRANSPORT_OUT)) {
    jack_client = jack_transport_new(port_name);
  }

  /*
   * Ensure a clean exit in as many situations as possible.
   */
  quit_init(quit_callback);

  /*
   * Main loop.
   */
  while (!quit) {
    midi2midi(seq_handle,
              jack_client,
              pfd,
              npfd,
              in_port,
              out_port,
              note_table,
              cc_table,
              program_change_prevention);
  }

  /*
   * Cleanup resources and return memory to system.
   */
  if ((NULL != in_port_ptr) || (NULL != out_port_ptr)) {
    sequencer_poller_delete(pfd);
    sequencer_delete(seq_handle);
  }
  if (NULL != jack_client) {
    jack_transport_delete(jack_client);
  }

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
