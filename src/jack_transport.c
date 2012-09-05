/*
 * jack_transport.c
 */

#include <stdlib.h>
#include <math.h>
#include <jack/jack.h>
#include <jack/transport.h>

#include "debug.h"
#include "error.h"
#include "jack_transport.h"

static void jack_shutdown(void *arg) {
  debug("Jack died", arg);
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

jack_client_t *jack_transport_new(const char *app_name) {
  jack_client_t *jack_client;

  /*
   * Initiate as a jack client.
   *
   * TODO: Use jack2 jack_client_open() instead, since jack_client_new()
   *       is depricated.
   */
  if (!(jack_client = jack_client_new(app_name))) {
    error("Could not connect to the jack server as '%s'.", app_name);
  }
  jack_on_shutdown(jack_client, jack_shutdown, 0);
  jack_activate(jack_client);

  return jack_client;
}

void jack_transport_send(jack_client_t *jack_client,
                         jack_transport_command command,
                         char value) {
  switch (command) {
  case JT_PLAY:
    /*
     * If jack is rolling, the PLAY will be PAUSE
     */
    switch (jack_transport_query(jack_client, NULL)) {
    case JackTransportRolling:
      jack_transport_stop(jack_client);
      debug("Jack transport paused (%d)", value);
      break;
    case JackTransportStopped:
      jack_transport_start(jack_client);
      debug("Jack transport playing (%d)", value);
      break;
    default:
      break;
    }
    break;
  case JT_STOP:
    switch (jack_transport_query(jack_client, NULL)) {
    case JackTransportRolling:
      /*
       * If jack is rolling, the STOP will be PAUSE
       */
      jack_transport_stop(jack_client);
      debug("Jack transport paused (%d)", value);
      break;
    case JackTransportStopped:
      /*
       * If jack is rolling, the STOP will be stop at the beginning
       */
      jack_reposition(jack_client, 0);
      jack_transport_start(jack_client);
      jack_transport_stop(jack_client);
      jack_reposition(jack_client, 0);
      debug("Jack transport rewinded (%d)", value);
      break;
    default:
      break;
    }
    break;
  case JT_REV:
    jack_reposition(jack_client, jack_prev_beat(jack_client));
    debug("Jack transport previous beat (%d)", value);
    break;
  case JT_FWD:
    jack_reposition(jack_client, jack_next_beat(jack_client));
    debug("Jack transport next beat (%d)", value);
    break;
  case JT_WHEEL:
    if (value > 64) {
      value = (128 - value) * -1;
    }
    jack_reposition(jack_client, jack_move_partial_beat(jack_client, value));
    debug("Jack transport jog wheel value %d", value);
    break;
  default:
    break;
  }

}


void jack_transport_delete(jack_client_t *jack_client) {
  jack_deactivate(jack_client);
  jack_client_close(jack_client);
}
