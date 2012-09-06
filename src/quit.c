/*
 * quit.c
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
 * Small helpers to handle signaling to an application.
 *
 */

#include <signal.h>

#include "quit.h"

void quit_init(void (*quit_callback)(int sig)) {
  /*
   * Set signal listeners - Any way the user tries to kill this application
   * make sure it exits cleanly.
   */
  signal(SIGINT, quit_callback);
  signal(SIGTERM, quit_callback);
  signal(SIGHUP, quit_callback);
  signal(SIGTSTP, quit_callback);
  signal(SIGCONT, quit_callback);
  signal(SIGUSR1, quit_callback);
  signal(SIGUSR2, quit_callback);
}
