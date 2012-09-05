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
 * make
 *
 * How-to run
 * ----------
 *
 * midi2midi -c configfile.m2m
 *
 * Example config file (roland_td9-mssiah_sid.m2m)
 * -----------------------------------------------
 *
 * midi2midi-config-1.0
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
 * Example config file (event_ezbus2jack_mixer.m2m)
 * ------------------------------------------------
 *
 * midi2midi-config-1.1
 * Event EZ-Bus/Jack Mixer
 * 12>13
 * 13>15
 * 14>17
 * 15>19
 * 16>21
 * 17>23
 * 18>25
 * 19J1
 * 20J2
 * 21M4
 *
 * Jack transport
 * --------------
 *
 * These transformations are quite simple and cryptic...
 *
 * You can convert a note into a jack transport command like this:
 *
 * <note>J<Jack-transport command>
 *
 * Or the other way around...
 *
 * <Jack-transport command>j<note>
 *
 * Command matrix
 * - - - - - - -
 *
 * 1 = PLAY
 * 2 = STOP
 * 4 = FAST FORWARD
 * 5 = REWIND
 * 47 = WHEEL
 *
 * MIDI Machine Control
 * --------------------
 *
 * This is quite similar to the jack transport translation but instead you
 * can translate notes into specific MIDI Machine Control commands, or the
 * other way around.
 *
 * That's about it :) Enjoy!
 *
 * Command repitition prevention
 * -----------------------------
 *
 * If you (like me) have a microKORG XL and are annoyed by seq24 not being
 * able to configure the synth in a usable way (if you set the program change
 * in every pattern for easy live performance) since the micro tend to scrap
 * all effects at a program change this feature is really nifty.
 *
 * 0P0
 *
 * Prevent MIDI control command 0 to repeat if the value ut sent is the same
 * as the last time it was sent.
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

static int quit = 0;

typedef enum {
  TT_NONE,
  TT_NOTE_TO_NOTE,
  TT_CC_TO_CC,
  TT_CC_TO_CC_IF_VALUE_CHANGE,
  TT_NOTE_TO_JACK,
  TT_NOTE_TO_MMC
} translation_type;

typedef struct {
  translation_type type;
  char value;
  char last_value;
  int line;
} translation;

/*
 * Command usage
 */
static void usage(char *app_name) {
  printf("USAGE: %s -c <filename> [-dvh]\n\n"
	 " -h, --help        Show this help text.\n"
	 " -v, --version     Display version information.\n"
	 " -c, --config=file Note translation configuration file to load.\n"
	 " -d, --debug       Output debug information.\n"
	 "\n"
         "This tool is a useful MIDI proxy if you own studio equipment that\n"
         "will not speak to eachother the way you want to. Just route your\n"
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
 * Parse configuration file and construct a translation table.
 */
static void translation_table_init(const char *filename,
				   translation note_table[255],
                                   translation cc_table[255],
				   char *port_name) {
  FILE *fd;
  int line_number = 0;
  int i;

  if ((fd = fopen(filename, "r")) == NULL) {
    error("Unable to open file '%s'.", filename);
  }

  debug("Reading file '%s'", filename);

  for (i = 0; i < 255; i++) {
    note_table[i].type = TT_NONE;
    note_table[i].value = i;
    cc_table[i].type = TT_NONE;
    cc_table[i].value = i;
    cc_table[i].last_value = -1;
  }

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
     * line is the name of th MIDI-port to upen and the rest is the actual
     * MIDI note conversion table defintion.
     */
    if (1 == line_number) {
      /*
       * Make sure that we can hande the file version :)
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
          error("The file '%s' is not a midi2midi config file.", filename);
        }
      }
      continue;
    }
    else if (2 == line_number) {
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

      debug("Read port name '%s'", port_name);

      if (0 != errno) {
        error("Could not get MIDI port name from config file '%s'.",
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

      debug("Reading line %d %d%c%d", line_number, from, c, to);

      if (0 != errno) {
	error("Error reading file '%s'.", filename);
      }

      /*
       * Valid separators are '>' for CC and ':' for notes.
       */
      switch (c) {
      case '>':
        type = TT_CC_TO_CC;
        break;
      case ':':
        type = TT_NOTE_TO_NOTE;
        break;
      case 'J':
        type = TT_NOTE_TO_JACK;
        break;
      case 'M':
        type = TT_NOTE_TO_MMC;
        break;
      case 'P':
        type = TT_CC_TO_CC_IF_VALUE_CHANGE;
        break;
      default:
        error("Separator '%c' is not valid in file '%s'.", c, filename);
      }

      if ((0 > from) || (255 < from)) {
        error("Line %d of '%s' has an invalid from value (must be 0-255).",
              line_number, filename);
      }

      if (TT_NOTE_TO_JACK != type) {
        /*
         * Perform some sanity checking.
         */

        if ((0 > to) || (255 < to)) {
          error("Line %d of '%s' has an invalid to value (must be 0-255).",
                line_number, filename);
        }
      }
      else {
        /*
         * Hard wire translate all the possible jack translation values
         * and make them as similar as posible to MMC.
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
       * Insert the note tranform in the translation table.
       */
      switch (type) {
      case TT_NOTE_TO_NOTE:
      case TT_NOTE_TO_JACK:
      case TT_NOTE_TO_MMC:
        /*
         * Make sure that there are no duplicates in the note translation
         * table.
         */
        if (TT_NONE != note_table[from].type) {
          error("Note value %d is already translated on line %d with value %d"
                "on line %d of file '%s'",
                from, cc_table[from].line, cc_table[from].value, line_number,
                filename);
        }
        note_table[from].type = type;
        note_table[from].value = to;

        /*
         * Just some debugging trivia.
         */
        switch(type) {
        case TT_NOTE_TO_NOTE:
          debug("Every %d note will be translated to note %d", from, to);
          break;
        case TT_NOTE_TO_JACK:
          debug("Every %d note will be translated to jack transport %d (raw)",
                from, to);
          break;
        case TT_NOTE_TO_MMC:
          debug("Every %d note will be translated to MIDI Machine Control %d",
                from, to);
          break;
        default:
          error("This should never happen type: %d.", type);
          break;
        }

        break;
      case TT_CC_TO_CC:
      case TT_CC_TO_CC_IF_VALUE_CHANGE:
        /*
         * Make sure that there are no duplicates in the CC translation
         * table.
         */
        if (TT_NONE != cc_table[from].type) {
          error("CC value %d is already translated on line %d with value %d"
                "on line %d of file '%s'",
                from, cc_table[from].line, cc_table[from].value, line_number,
                filename);
        }
        cc_table[from].type = type;
        cc_table[from].value = to;

        /*
         * Just some debugging trivia.
         */
        switch(type) {
        case TT_CC_TO_CC:
          debug("Every %d CC will be translated to %d", from, to);
          break;
        case TT_CC_TO_CC_IF_VALUE_CHANGE:
          debug("Every %d CC will be translated to %d if the value changes",
                from, to);
          break;
        default:
          error("This should never happen type: %d.", type);
          break;
        }
        break;

      default:
        error("Type %d is not implemented yet", type);
        break;
      }
      continue;
    }
  }

  debug("Reached end of file '%s'", filename);

  fclose(fd);
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
                      translation cc_table[256]) {
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
      int send_midi = 0;
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

        case TT_NOTE_TO_NOTE:
          /*
           * Prepare to just forward a translated note.
           */
          debug("Translating note %d to note %d",
                ev->data.note.note,
                note_table[ev->data.note.note].value);
          ev->data.note.note = note_table[ev->data.note.note].value;
          send_midi = 1;
          break;

        case TT_NOTE_TO_JACK:
          /*
           * Send a jack transport command.
           */
          jack_transport_send(jack_client,
                              note_table[ev->data.note.note].value,
                              ev->data.note.velocity);
          break;

        default:
          error("Note translation %d is not implemented yet.",
                note_table[ev->data.note.note].type);
          break;
        }

      }
      else if ((SND_SEQ_EVENT_CONTROLLER == ev->type) &&
               (TT_NONE != cc_table[ev->data.control.param].type)) {

        switch (note_table[ev->data.note.note].type) {

        case TT_CC_TO_CC:
          debug("Translating CC param %d to CC param %d",
                ev->data.control.param,
                cc_table[ev->data.control.param].value);
          ev->data.control.param = cc_table[ev->data.control.param].value;
          send_midi = 1;
          break;

        case TT_CC_TO_CC_IF_VALUE_CHANGE:
          /*
           * Only forward value if the parameter value actually changed.
           */
          if (cc_table[ev->data.control.param].last_value !=
              ev->data.control.value) {

            debug("Translating CC param %d to CC param %d",
                  ev->data.control.param,
                  cc_table[ev->data.control.param].value);
            ev->data.control.param = cc_table[ev->data.control.param].value;

            cc_table[ev->data.control.param].last_value =
              ev->data.control.value;

            send_midi = 1;
          }
          break;

        default:
          error("CC param translation %d is not implemented yet.",
                note_table[ev->data.note.note].type);
          break;

        }
      }

      if (1 == send_midi) {
        /*
         * Output the translated note to the MIDI output port.
         */
        snd_seq_ev_set_source(ev, out_port);
        snd_seq_event_output_direct(seq_handle, ev);
      }

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

  /*
   * Handles and IDs to ALSA stuff.
   */
  snd_seq_t *seq_handle;
  int in_port, out_port;
  int npfd;
  struct pollfd *pfd;

  /*
   * Handles for Jack client stuff.
   */
  jack_client_t *jack_client;

  /*
   * This is where the note translation-table is stored.
   */
  translation note_table[255];
  translation cc_table[255];

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
      debug_enable();
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
      error("Unknown parameter %c.", c);
      usage(argv[0]);
      exit(EXIT_FAILURE);
      break;
    }
  }

  /*
   * Make sure that the all so important configuration file is provided.
   */
  if (config_file == NULL) {
    error("No configuration file provided use %s -h for more information.",
          app_name);
  }

  /*
   * Read the configuration file and get all the essential information.
   */
  translation_table_init(config_file, note_table, cc_table, port_name);

  /*
   * Set-up ALSA MIDI.
   */
  seq_handle = sequencer_new(&in_port, &out_port, port_name);
  pfd = sequencer_poller_new(seq_handle, &npfd);
  jack_client = jack_transport_new(port_name);

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
              cc_table);
  }

  /*
   * Cleanup resources.
   */
  sequencer_poller_delete(pfd);
  sequencer_delete(seq_handle);
  jack_transport_delete(jack_client);

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
