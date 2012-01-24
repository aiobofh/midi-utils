/*
 * quit.h
 *
 * Handle clean quits when a proess is killed.
 *
 */

#ifndef _QUIT_H_
#define _QUIT_H_

void quit_init(void (*quit_callback)(int sig));

#endif /* _QUIT_H_ */
