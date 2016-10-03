/*
 * Copyright (c) 2016 Jonas 'Sortie' Termansen.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * langinfo/nl_langinfo_l.c
 * Language information.
 */

#include <langinfo.h>

char* nl_langinfo_l(nl_item item, locale_t locale)
{
	(void) locale; /* TODO */
	switch ( item )
	{
	case CODESET: return "UTF-8";
	case D_T_FMT: return "%a %b %e %T %Y";
	case D_FMT: return "%Y-%m-%d";
	case T_FMT: return "%H:%M:%S";
	case T_FMT_AMPM: return "%I:%M:%S %p";
	case AM_STR: return "AM";
	case PM_STR: return "PM";
	case DAY_1: return "Sunday";
	case DAY_2: return "Monday";
	case DAY_3: return "Tuesday";
	case DAY_4: return "Wednesday";
	case DAY_5: return "Thursday";
	case DAY_6: return "Friday";
	case DAY_7: return "Saturday";
	case ABDAY_1: return "Sun";
	case ABDAY_2: return "Mon";
	case ABDAY_3: return "Tue";
	case ABDAY_4: return "Wed";
	case ABDAY_5: return "Thu";
	case ABDAY_6: return "Fri";
	case ABDAY_7: return "Sat";
	case MON_1: return "January";
	case MON_2: return "February";
	case MON_3: return "March";
	case MON_4: return "April";
	case MON_5: return "May";
	case MON_6: return "June";
	case MON_7: return "July";
	case MON_8: return "August";
	case MON_9: return "September";
	case MON_10: return "October";
	case MON_11: return "November";
	case MON_12: return "December";
	case ABMON_1: return "Jan";
	case ABMON_2: return "Feb";
	case ABMON_3: return "Mar";
	case ABMON_4: return "Apr";
	case ABMON_5: return "May";
	case ABMON_6: return "Jun";
	case ABMON_7: return "Jul";
	case ABMON_8: return "Aug";
	case ABMON_9: return "Sep";
	case ABMON_10: return "Oct";
	case ABMON_11: return "Nov";
	case ABMON_12: return "Dec";
	case ERA: return "";
	case ERA_D_FMT: return "";
	case ERA_D_T_FMT: return "";
	case ERA_T_FMT: return "";
	case ALT_DIGITS: return "";
	case RADIXCHAR: return ".";
	case THOUSEP: return "";
	case YESEXPR: return "^[yY]";
	case NOEXPR: return "^[nN]";
	case CRNCYSTR: return "";
	}
	return "";
}
