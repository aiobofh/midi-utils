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
#ifdef USE_JACK
#include <jack/jack.h>
#include <jack/transport.h>
#endif
#include "error.h"
#include "debug.h"
#include "quit.h"
#include "sequencer.h"
#ifdef USE_JACK
#include "jack_transport.h"
#endif
#define APPNAME "midi2midi"
#define VERSION "1.3.0"


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
  TT_NOTE_TO_CC,
#ifdef USE_JACK
  TT_NOTE_TO_JACK,
#endif
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
#ifdef USE_JACK
  CB_JACK_TRANSPORT_OUT = 4
#endif
} capability;

/*
 * Type definition for different message types to filter on.
 */
typedef enum {
  MT_NONE = 0,
  MT_NOTE_ON = 1,
  MT_NOTE_OFF = 2,
  MT_POLYPHONIC_KEY_PRESSURE = 4,
  MT_CONTROL_CHANGE = 8,
  MT_PROGRAM_CHANGE = 16,
  MT_CHANNEL_PRESSURE = 32,
  MT_PITCH_BEND_CHANGE = 64,
  MT_CHANNEL_MODE_MESSAGES = 128,
  MT_SYSEX = 128,
  MT_MIDI_TIME_CODE_QUARTER_FRAME = 256,
  MT_SONG_POSITION_POINTER = 512,
  MT_SONG_SELECT = 1024,
  MT_TUNE_REQUEST = 2048,
  MT_TIMING_CLOCK = 4096,
  MT_MMC = 8196
} message_type;

/*
 * Type definition for a translation table entry.
 */
typedef struct {
  translation_type type;
  char value;
  char last_value;
  char channel;
  int line;
} translation;


/*
 * Command usage providing a simple help for the user.
 */
static void usage(char *app_name) {
  printf("USAGE: %s [-c <file name>] [-n <client_name>] [-hvpd] [-f \n\n"
         " -h, --help                   Show this help text.\n"
         " -v, --version                Display version information.\n"
         " -c, --config=file            Note translation configuration file\n"
         "                              to load. See manual for file format.\n"
         " -n, --client-name=name       Name of the client. This\n"
         "                              overrides line 2 in the config file.\n"
         " -p, --program-repeat-prevent Prevent a program select on a MIDI\n"
         "                              device to repeated times.\n"
         " -f, --filter         <what>  Filter all specified MIDI message types.\n"
#ifdef USE_JACK
         " -j, --jack                   Use Jack-specific fatures.\n"
#endif
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
#ifdef USE_JACK
static capability translation_table_init(const char *filename,
                                         translation note_table[255],
                                         translation cc_table[255],
                                         char *port_name,
                                         capability capabilities,
                                         int use_jack) {
#else
static capability translation_table_init(const char *filename,
                                         translation note_table[255],
                                         translation cc_table[255],
                                         char *port_name,
                                         capability capabilities) {
#endif
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
    cc_table[i].last_value = note_table[i].last_value = -1;
    cc_table[i].channel = note_table[i].channel = -1;
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
    int from, to, channel;
    char c;
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
      fscanf(fd, "midi2midi-config-1.3\n");

      if (errno != 0) {
        debug("The file '%s' was no 1.3 file, trying 1.2", filename);

        /*
         * Just to be sure, start over.
         */
        fscanf(fd, "midi2midi-config-1.2\n");
        if (errno != 0) {
          debug("The file '%s' was no 1.2 file, trying 1.1", filename);

          /*
           * Just to be sure, start over.
           */
          rewind(fd);

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
      int use_channel = 0;
      translation_type type = TT_NONE;
      /*
       * Read each line of the configuration file and insert translations into
       * the table.
       */
      if (4 != fscanf(fd, "%3d%1c%3d,%3d\n", &from, &c, &to, &channel)) {
        fscanf(fd, "%3d%1c%3d\n", &from, &c, &to);
      }
      else {
        use_channel = 1;
        if ((16 < channel) || (0 > channel)) {
          error("Channel number must be between 1 and 16, not %d", channel);
        }
      }

      if (use_channel == 0) {
        debug("Reading line %d '%d%c%d'", line_number, from, c, to);
      }
      else {
        debug("Reading line %d '%d%c%d,%d'", line_number, from, c, to, channel);
      }

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
        case '!': {
          type = TT_NOTE_TO_CC;
          debug("Identified line as TT_NOTE_TO_CC (%c)", c);
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_ALSA_MIDI_OUT);
          break;
        }
#ifdef USE_JACK
        case 'J': {
          debug("Identified line as TT_NOTE_TO_JACK (%c)", c);
          if (1 == use_jack) {
            error("Jack features are not enabled. Use -j, --jack to enable them\n");
            exit(EXIT_FAILURE);
          }
          type = TT_NOTE_TO_JACK;
          capabilities = capabilities | (CB_ALSA_MIDI_IN | CB_JACK_TRANSPORT_OUT);
          break;
        }
#endif
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
#ifdef USE_JACK
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
#endif

      /*
       * Insert the note transformation in the translation table.
       */
      switch (type) {
        case TT_NOTE_TO_NOTE:
        case TT_NOTE_TO_CC:
#ifdef USE_JACK
        case TT_NOTE_TO_JACK:
#endif
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
          if (use_channel) {
            cc_table[from].channel = channel;
          }

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

#define FILTERMODE(NAME, ALSANAME)                                      \
  filter |= ((0 != (filter & MT_ ## NAME)) && (SND_SEQ_EVENT_ ## ALSANAME == ev->type))

/*
 * Main event loop.
 */
#ifdef USE_JACK
static void midi2midi(snd_seq_t *seq_handle,
                      jack_client_t *jack_client,
                      struct pollfd *pfd,
                      int npfd,
                      int in_port,
                      int out_port,
                      translation note_table[256],
                      translation cc_table[256],
                      int program_change_prevention,
                      message_type filter,
                      int use_jack) {
#else
static void midi2midi(snd_seq_t *seq_handle,
                      struct pollfd *pfd,
                      int npfd,
                      int in_port,
                      int out_port,
                      translation note_table[256],
                      translation cc_table[256],
                      int program_change_prevention,
                      message_type filter) {
#endif
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
      int filter = 0;
      /*
       * Get the event information.
       */
      snd_seq_event_input(seq_handle, &ev);
      snd_seq_ev_set_subs(ev);
      snd_seq_ev_set_direct(ev);

      if (filter != MT_NONE) {
        FILTERMODE(NOTE_ON, NOTEON);
        FILTERMODE(NOTE_ON, NOTE);

        FILTERMODE(NOTE_OFF, NOTEOFF);
        FILTERMODE(NOTE_OFF, NOTE);

        FILTERMODE(POLYPHONIC_KEY_PRESSURE, KEYPRESS);

        FILTERMODE(CONTROL_CHANGE, CONTROLLER);

        FILTERMODE(PROGRAM_CHANGE, PGMCHANGE);

        FILTERMODE(CHANNEL_PRESSURE, CHANPRESS);

        FILTERMODE(PITCH_BEND_CHANGE, PITCHBEND);

        FILTERMODE(SYSEX, SYSEX);

        FILTERMODE(MIDI_TIME_CODE_QUARTER_FRAME, QFRAME);

        FILTERMODE(SONG_POSITION_POINTER, SONGPOS);

        FILTERMODE(SONG_SELECT, SONGSEL);

        FILTERMODE(TUNE_REQUEST, TUNE_REQUEST);

        FILTERMODE(TIMING_CLOCK, CLOCK);
        FILTERMODE(TIMING_CLOCK, TICK);

        FILTERMODE(MMC, START);
        FILTERMODE(MMC, STOP);
        FILTERMODE(MMC, CONTINUE);
      }

      /*
       * Translate either a note or a CC command.
       */
      if (0 != filter) {
        debug("Filtering event %d\n", ev->type);
        send_midi = 0;
      }
      else if (((SND_SEQ_EVENT_NOTEON == ev->type) ||
                (SND_SEQ_EVENT_NOTEOFF == ev->type)) &&
               (TT_NONE != note_table[ev->data.note.note].type)) {

        switch (note_table[ev->data.note.note].type) {

          case TT_NOTE_TO_NOTE: {
            /*
             * Prepare to just forward a translated note.
             */
            if (note_table[ev->data.note.note].channel > 0 && 
                note_table[ev->data.note.note].channel < 17) {
              debug("Translating note %d to note %d on channel %d",
                    ev->data.note.note,
                    note_table[ev->data.note.note].value,
                    note_table[ev->data.note.note].channel);
              ev->data.note.channel = note_table[ev->data.note.note].channel;
            }
            else {
              debug("Translating note %d to note %d",
                    ev->data.note.note,
                    note_table[ev->data.note.note].value);
            }
            ev->data.note.note = note_table[ev->data.note.note].value;

            break;
          }
          case TT_NOTE_TO_CC: {
            /*
             * Prepare to map not to a parameter id and velocity to the value.
             */
            if (note_table[ev->data.note.note].channel > 0 &&
                note_table[ev->data.note.note].channel < 17) {
              debug("Translating note %d to note %d on channel %d",
                    ev->data.note.note,
                    note_table[ev->data.note.note].value,
                    note_table[ev->data.note.note].channel);
              ev->data.note.channel = note_table[ev->data.note.note].channel;
            }
            else {
              debug("Translating note %d to note %d",
                    ev->data.note.note,
                    note_table[ev->data.note.note].value);
            }
            ev->data.note.note = note_table[ev->data.note.note].value;
            ev->type = SND_SEQ_EVENT_CONTROLLER;
            ev->data.control.param = note_table[ev->data.note.note].value;
            ev->data.control.value = ev->data.note.velocity;

            break;
          }
#ifdef USE_JACK
          case TT_NOTE_TO_JACK: {
            if (0 == use_jack) {
              send_midi = 0;
              break;
            }
            /*
             * Send a jack transport command.
             */
            jack_transport_send(jack_client,
                                note_table[ev->data.note.note].value,
                                ev->data.note.velocity);

            send_midi = 0;

            break;
          }
#endif
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
            if (cc_table[ev->data.control.param].channel > 0 && 
                cc_table[ev->data.control.param].channel < 17) {
              debug("Translating MIDI CC %d to MIDI CC %d on channel %d",
                    ev->data.control.param,
                    cc_table[ev->data.control.param].value,
                    cc_table[ev->data.control.param].channel);
              ev->data.control.channel = cc_table[ev->data.control.param].channel;
            }
            else {
              debug("Translating MIDI CC %d to MIDI CC %d",
                    ev->data.control.param,
                    cc_table[ev->data.control.param].value);
            }
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

#define MODE_CONV(NAME)                                  \
  strcmpret = strcmp(#NAME, &optarg[lastpos]);     \
  if (0 == strcmpret) {                                  \
    retval |= MT_##NAME;                                 \
    lastpos = i + 1;                                     \
    printf(#NAME "\n");                                  \
    continue;                                            \
  }

message_type lookup_capabilities(char* optarg) {
  message_type retval = MT_NONE;
  size_t i;
  int lastpos = 0;
  size_t len = strlen(optarg);
  int strcmpret = 0;
  for (i = 0; i < len; i++) {
    if ((',' == optarg[i]) || (i == (len - 1))) {
      if (optarg[i] == ',') {
        optarg[i] = 0;
      }
      MODE_CONV(NOTE_ON);
      MODE_CONV(NOTE_OFF);
      MODE_CONV(POLYPHONIC_KEY_PRESSURE);
      MODE_CONV(CONTROL_CHANGE);
      MODE_CONV(PROGRAM_CHANGE);
      MODE_CONV(CHANNEL_PRESSURE);
      MODE_CONV(PITCH_BEND_CHANGE);
      MODE_CONV(CHANNEL_MODE_MESSAGES);
      MODE_CONV(SYSEX);
      MODE_CONV(MIDI_TIME_CODE_QUARTER_FRAME);
      MODE_CONV(SONG_POSITION_POINTER);
      MODE_CONV(SONG_SELECT);
      MODE_CONV(TUNE_REQUEST);
      MODE_CONV(TIMING_CLOCK);
      MODE_CONV(MMC);
      error("Unknown message type.%c", '\n');
      exit(EXIT_FAILURE);
    }
  }
  return retval;
}

/*
 * Main function of midi2midi.
 */
int main(int argc, char *argv[]) {

  char *app_name = APPNAME;
  char port_name[255];
  char *config_file = NULL;
  int program_change_prevention = 0;
  message_type filter = MT_NONE;

#ifdef USE_JACK
  int use_jack = 0;
#endif

  /*
   * This variable holds a bit pattern describing what resources need to be
   * allocated. (ALSA/Jack)
   */
  capability capabilities = CB_NONE;

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
#ifdef USE_JACK
  jack_client_t *jack_client = NULL;
#endif
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
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {"config", required_argument, NULL,  'c'},
    {"client-name", required_argument, NULL, 'n'},
    {"program-repeat-prevent", no_argument, NULL, 'p'},
    {"filter-all-but", required_argument, NULL, 'f'},
#ifdef USE_JACK
    {"jack", no_argument, NULL, 'j'},
#endif
    {"debug", no_argument, NULL,  'd'},
    {0, 0, 0,  0 }
  };

  strcpy(port_name, "Unknown name");

  /*
   * Handle command line options.
   */
  while(1) {
    int option_index = 0;
    int c;
    c = getopt_long(argc, argv, "dn:c:hpv?f:j",
                    long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      case '?':
      case 'h': {
        usage(argv[0]);
        exit(EXIT_SUCCESS);
        break;
      }
      case 'v': {
        printf("%s %s", app_name, VERSION);
        exit(EXIT_SUCCESS);
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
      case 'f': {
        if (optarg[0] == '-') {
          error("Message type required for -f, --filter%c", '\n');
          exit(EXIT_FAILURE);
        }
        filter = lookup_capabilities(optarg);
        break;
      }
      case 'n': {
        strncpy(port_name, optarg, 254);
        break;
      }
#ifdef USE_JACK
      case 'j': {
        use_jack = 1;
        break;
      }
#endif
      case 'd': {
        debug_enable();
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
#ifdef USE_JACK
  capabilities = translation_table_init(config_file,
                                        note_table,
                                        cc_table,
                                        port_name,
                                        capabilities,
                                        use_jack);
#else
  capabilities = translation_table_init(config_file,
                                        note_table,
                                        cc_table,
                                        port_name,
                                        capabilities);
#endif

  if (MT_NONE != filter) {
    capabilities |= CB_ALSA_MIDI_IN;
    capabilities |= CB_ALSA_MIDI_OUT;
  }

  /*
   * Set-up ALSA MIDI and Jack Transport depending on how the program
   * instance is set-up.
   */
  if (CB_ALSA_MIDI_IN == (capabilities & (CB_ALSA_MIDI_IN))) {
    in_port_ptr = &in_port;
  }
  if (CB_ALSA_MIDI_OUT == (capabilities & (CB_ALSA_MIDI_OUT))) {
    out_port_ptr = &out_port;
  }
  if ((NULL != in_port_ptr) || (NULL != out_port_ptr)) {
    seq_handle = sequencer_new(in_port_ptr, out_port_ptr, port_name);
    pfd = sequencer_poller_new(seq_handle, &npfd);
  }
#ifdef USE_JACK
  if (1 == use_jack) {
    if (CB_JACK_TRANSPORT_OUT == (capabilities & CB_JACK_TRANSPORT_OUT)) {
      jack_client = jack_transport_new(port_name);
    }
  }
#endif

  /*
   * Ensure a clean exit in as many situations as possible.
   */
  quit_init(quit_callback);

  /*
   * Main loop.
   */
  while (!quit) {
#ifdef USE_JACK
    midi2midi(seq_handle,
              jack_client,
              pfd,
              npfd,
              in_port,
              out_port,
              note_table,
              cc_table,
              program_change_prevention,
              filter,
              use_jack);
#else
    midi2midi(seq_handle,
              pfd,
              npfd,
              in_port,
              out_port,
              note_table,
              cc_table,
              program_change_prevention,
              filter);
#endif
  }

  /*
   * Cleanup resources and return memory to system.
   */
  if ((NULL != in_port_ptr) || (NULL != out_port_ptr)) {
    sequencer_poller_delete(pfd);
    sequencer_delete(seq_handle);
  }
#ifdef USE_JACK
  if (use_jack) {
    if (NULL != jack_client) {
      jack_transport_delete(jack_client);
    }
  }
#endif

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

