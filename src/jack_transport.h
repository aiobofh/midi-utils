/*
 * jack_transport.h
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
 * This is a simplified API for allocating resources and initialise a Jack
 * transport interface
 *
 */

#ifndef _JACK_TRANSPORT_H_
#define _JACK_TRANSPORT_H_

#ifdef USE_JACK

#include <jack/jack.h>

typedef enum {
  JT_STOP,
  JT_PLAY,
  JT_REV,
  JT_FWD,
  JT_WHEEL,
  JT_UNKNOWN
} jack_transport_command;


/*
 * Allocate a new Jack client and register callbacks.
 */
jack_client_t *jack_transport_new(const char *app_name);


/*
 * Send a translation value to jack transport.
 */
void jack_transport_send(jack_client_t *jack_client,
                         jack_transport_command command,
                         char value);


/*
 * Clean up a jack client.
 */
void jack_transport_delete(jack_client_t *jack_client);

#endif

#endif /* _JACK_TRANSPORT_H_ */
