/*
 * quit.c
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
