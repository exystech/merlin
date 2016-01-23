/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2012, 2015, 2016.

    This file is part of Sortix.

    Sortix is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along with
    Sortix. If not, see <http://www.gnu.org/licenses/>.

    sortix/termmode.h
    Defines constants for various terminal modes.

*******************************************************************************/

#ifndef SORTIX_TERMMODE_H
#define SORTIX_TERMMODE_H

#define TERMMODE_KBKEY (1U << 0) /* ISORTIX_ENABLE_KBKEY */
#define TERMMODE_UNICODE (1U << 1) /* !ISORTIX_DISABLE_CHARS */
#define TERMMODE_SIGNAL (1U << 2) /* ISIG */
#define TERMMODE_UTF8 (1U << 3) /* !ISORTIX_ENABLE_32BIT */
#define TERMMODE_LINEBUFFER (1U << 4) /* ICANON */
#define TERMMODE_ECHO (1U << 5) /* ECHO */
#define TERMMODE_NONBLOCK (1U << 6) /* ISORTIX_NONBLOCK */
#define TERMMODE_TERMIOS (1U << 7) /* !ISORTIX_TERMMODE */
#define TERMMODE_DISABLE (1U << 8) /* !CREAD */
#define TERMMODE_NORMAL (TERMMODE_UNICODE | TERMMODE_SIGNAL | TERMMODE_UTF8 | \
                         TERMMODE_LINEBUFFER | TERMMODE_ECHO | TERMMODE_TERMIOS)

#endif
