/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * sortix/syscall.h
 * Numeric constants identifying each system call.
 */

#ifndef INCLUDE_SORTIX_SYSCALLNUM_H
#define INCLUDE_SORTIX_SYSCALLNUM_H

#define SYSCALL_BAD_SYSCALL 0
#define SYSCALL_EXIT 1 /* OBSOLETE */
#define SYSCALL_SLEEP 2 /* OBSOLETE */
#define SYSCALL_USLEEP 3 /* OBSOLETE */
#define SYSCALL_PRINT_STRING 4 /* OBSOLETE */
#define SYSCALL_CREATE_FRAME 5 /* OBSOLETE */
#define SYSCALL_CHANGE_FRAME 6 /* OBSOLETE */
#define SYSCALL_DELETE_FRAME 7 /* OBSOLETE */
#define SYSCALL_RECEIVE_KEYSTROKE 8 /* OBSOLETE */
#define SYSCALL_SET_FREQUENCY 9 /* OBSOLETE */
#define SYSCALL_EXECVE 10
#define SYSCALL_PRINT_PATH_FILES 11 /* OBSOLETE */
#define SYSCALL_FORK 12 /* OBSOLETE */
#define SYSCALL_GETPID 13
#define SYSCALL_GETPPID 14
#define SYSCALL_GET_FILEINFO 15 /* OBSOLETE */
#define SYSCALL_GET_NUM_FILES 16 /* OBSOLETE */
#define SYSCALL_WAITPID 17
#define SYSCALL_READ 18
#define SYSCALL_WRITE 19
#define SYSCALL_PIPE 20 /* OBSOLETE */
#define SYSCALL_CLOSE 21
#define SYSCALL_DUP 22
#define SYSCALL_OPEN 23 /* OBSOLETE */
#define SYSCALL_READDIRENTS 24
#define SYSCALL_CHDIR 25 /* OBSOLETE */
#define SYSCALL_GETCWD 26 /* OBSOLETE */
#define SYSCALL_UNLINK 27 /* OBSOLETE */
#define SYSCALL_REGISTER_ERRNO 28 /* OBSOLETE */
#define SYSCALL_REGISTER_SIGNAL_HANDLER 29 /* OBSOLETE */
#define SYSCALL_SIGRETURN 30 /* OBSOLETE */
#define SYSCALL_KILL 31
#define SYSCALL_MEMSTAT 32
#define SYSCALL_ISATTY 33
#define SYSCALL_UPTIME 34 /* OBSOLETE */
#define SYSCALL_SBRK 35 /* OBSOLETE */
#define SYSCALL_LSEEK 36
#define SYSCALL_GETPAGESIZE 37
#define SYSCALL_MKDIR 38 /* OBSOLETE */
#define SYSCALL_RMDIR 39 /* OBSOLETE */
#define SYSCALL_TRUNCATE 40 /* OBSOLETE */
#define SYSCALL_FTRUNCATE 41
#define SYSCALL_SETTERMMODE 42
#define SYSCALL_GETTERMMODE 43
#define SYSCALL_STAT 44 /* OBSOLETE */
#define SYSCALL_FSTAT 45
#define SYSCALL_FCNTL 46
#define SYSCALL_ACCESS 47 /* OBSOLETE */
#define SYSCALL_KERNELINFO 48
#define SYSCALL_PREAD 49
#define SYSCALL_PWRITE 50
#define SYSCALL_TFORK 51
#define SYSCALL_TCGETWINSIZE 52
#define SYSCALL_RAISE 53
#define SYSCALL_OPENAT 54
#define SYSCALL_DISPMSG_ISSUE 55
#define SYSCALL_FSTATAT 56
#define SYSCALL_CHMOD 57 /* OBSOLETE */
#define SYSCALL_CHOWN 58 /* OBSOLETE */
#define SYSCALL_LINK 59 /* OBSOLETE */
#define SYSCALL_DUP2 60
#define SYSCALL_UNLINKAT 61
#define SYSCALL_FACCESSAT 62
#define SYSCALL_MKDIRAT 63
#define SYSCALL_FCHDIR 64
#define SYSCALL_TRUNCATEAT 65
#define SYSCALL_FCHOWNAT 66
#define SYSCALL_FCHOWN 67
#define SYSCALL_FCHMOD 68
#define SYSCALL_FCHMODAT 69
#define SYSCALL_LINKAT 70
#define SYSCALL_FSM_FSBIND 71
#define SYSCALL_PPOLL 72
#define SYSCALL_RENAMEAT 73
#define SYSCALL_READLINKAT 74
#define SYSCALL_FSYNC 75
#define SYSCALL_GETUID 76
#define SYSCALL_GETGID 77
#define SYSCALL_SETUID 78
#define SYSCALL_SETGID 79
#define SYSCALL_GETEUID 80
#define SYSCALL_GETEGID 81
#define SYSCALL_SETEUID 82
#define SYSCALL_SETEGID 83
#define SYSCALL_IOCTL 84
#define SYSCALL_UTIMENSAT 85
#define SYSCALL_FUTIMENS 86
#define SYSCALL_RECV 87
#define SYSCALL_SEND 88
#define SYSCALL_ACCEPT4 89
#define SYSCALL_BIND 90
#define SYSCALL_CONNECT 91
#define SYSCALL_LISTEN 92
#define SYSCALL_READV 93
#define SYSCALL_WRITEV 94
#define SYSCALL_PREADV 95
#define SYSCALL_PWRITEV 96
#define SYSCALL_TIMER_CREATE 97
#define SYSCALL_TIMER_DELETE 98
#define SYSCALL_TIMER_GETOVERRUN 99
#define SYSCALL_TIMER_GETTIME 100
#define SYSCALL_TIMER_SETTIME 101
#define SYSCALL_ALARMNS 102
#define SYSCALL_CLOCK_GETTIMERES 103
#define SYSCALL_CLOCK_SETTIMERES 104
#define SYSCALL_CLOCK_NANOSLEEP 105
#define SYSCALL_TIMENS 106
#define SYSCALL_UMASK 107
#define SYSCALL_FCHDIRAT 108
#define SYSCALL_FCHROOT 109
#define SYSCALL_FCHROOTAT 110
#define SYSCALL_MKPARTITION 111
#define SYSCALL_GETPGID 112
#define SYSCALL_SETPGID 113
#define SYSCALL_TCGETPGRP 114
#define SYSCALL_TCSETPGRP 115
#define SYSCALL_MMAP_WRAPPER 116
#define SYSCALL_MPROTECT 117
#define SYSCALL_MUNMAP 118
#define SYSCALL_GETPRIORITY 119
#define SYSCALL_SETPRIORITY 120
#define SYSCALL_PRLIMIT 121
#define SYSCALL_DUP3 122
#define SYSCALL_SYMLINKAT 123
#define SYSCALL_TCGETWINCURPOS 124
#define SYSCALL_PIPE2 125
#define SYSCALL_GETUMASK 126
#define SYSCALL_FSTATVFS 127
#define SYSCALL_FSTATVFSAT 128
#define SYSCALL_RDMSR 129
#define SYSCALL_WRMSR 130
#define SYSCALL_SCHED_YIELD 131
#define SYSCALL_EXIT_THREAD 132
#define SYSCALL_SIGACTION 133
#define SYSCALL_SIGALTSTACK 134
#define SYSCALL_SIGPENDING 135
#define SYSCALL_SIGPROCMASK 136
#define SYSCALL_SIGSUSPEND 137
#define SYSCALL_SENDMSG 138
#define SYSCALL_RECVMSG 139
#define SYSCALL_GETSOCKOPT 140
#define SYSCALL_SETSOCKOPT 141
#define SYSCALL_TCGETBLOB 142
#define SYSCALL_TCSETBLOB 143
#define SYSCALL_GETPEERNAME 144
#define SYSCALL_GETSOCKNAME 145
#define SYSCALL_SHUTDOWN 146
#define SYSCALL_GETENTROPY 147
#define SYSCALL_GETHOSTNAME 148
#define SYSCALL_SETHOSTNAME 149
#define SYSCALL_UNMOUNTAT 150
#define SYSCALL_FSM_MOUNTAT 151
#define SYSCALL_CLOSEFROM 152
#define SYSCALL_MKPTY 153
#define SYSCALL_PSCTL 154
#define SYSCALL_TCDRAIN 155
#define SYSCALL_TCFLOW 156
#define SYSCALL_TCFLUSH 157
#define SYSCALL_TCGETATTR 158
#define SYSCALL_TCGETSID 159
#define SYSCALL_TCSENDBREAK 160
#define SYSCALL_TCSETATTR 161
#define SYSCALL_SCRAM 162
#define SYSCALL_GETSID 163
#define SYSCALL_SETSID 164
#define SYSCALL_SOCKET 165
#define SYSCALL_GETDNSCONFIG 166
#define SYSCALL_SETDNSCONFIG 167
#define SYSCALL_MAX_NUM 168 /* index of highest constant + 1 */

#endif
