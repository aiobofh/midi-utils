/*
 * error.h
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
 * Simple API for error output from a program.
 *
 */

#ifndef _ERROR_H_
#define _ERROR_H_


/*
 * Macro and function to output an error string with some valuable information
 * around where in the code the error macro was called and such.
 */
#define error(format, ...) __error(__FILE__, __LINE__, format, __VA_ARGS__)
void __error(const char *filename, int line_number, const char *format, ...);

#endif /* _ERROR_H_ */
