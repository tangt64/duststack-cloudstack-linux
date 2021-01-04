/*  rlwrap.h: includes, definitions, declarations */

/*  This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License , or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; see the file COPYING.  If not, write to
    the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

    You may contact the author by:
       e-mail:  hlub@knoware.nl
*/

#include "../config.h"
#include <sys/types.h>
#if HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif



#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>

#include <errno.h>
#include <stdarg.h>



#define __USE_XOPEN
#define __USE_GNU
#include <stdlib.h>

#include <sched.h>



#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif


#ifdef HAVE_GETOPT_H
#  include <getopt.h>
#endif

#ifdef HAVE_CURSES_H
#  include <curses.h> /* this is normally where we find tgetent and friends */
#endif


#ifdef HAVE_NCURSES_TERM_H /* cygwin? */
#  include <ncurses/term.h>
#else
#  ifdef HAVE_TERM_H
#    include <term.h>
#  endif
#endif

#if STDC_HEADERS
#  include <string.h>
#else
#  ifndef HAVE_STRRCHR
#    define strrchr rindex
#  endif
char *strchr(), *strrchr();

#  ifndef HAVE_MEMMOVE
#    define memmove(d, s, n) bcopy ((s), (d), (n))
#  endif
#endif


#ifdef HAVE_PTY_H
#  include <pty.h>
#endif
#ifdef HAVE_LIBUTIL_H
#  include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#  include <util.h>
#endif




#define BUFFSIZE 512

#ifndef MAXPATHLEN
#define MAXPATHLEN 512
#endif


#ifdef  HAVE_SNPRINTF		/* don't rely on the compiler understanding variadic macros */
# define snprintf0(buf,bufsize,format)    		snprintf(buf,bufsize,format)
# define snprintf1(buf,bufsize,format,arg1) 		snprintf(buf,bufsize,format,arg1)
# define snprintf2(buf,bufsize,format,arg1,arg2) 	snprintf(buf,bufsize,format,arg1,arg2)
#else
# define snprintf0(buf,bufsize,format)      		sprintf(buf,format)
# define snprintf1(buf,bufsize,format,arg1) 		sprintf(buf,format,arg1)
# define snprintf2(buf,bufsize,format,arg1,arg2) 	sprintf(buf,format,arg1,arg2)
# define vsnprintf(buf,bufsize,format,ap)   		vsprintf(buf,format,ap)
#endif


#ifndef HAVE_STRNLEN
# define strnlen(s,l) strlen(s)
#endif



#include <readline/readline.h>
#include <readline/history.h>


#ifndef HAVE_RL_VARIABLE_VALUE
#  define rl_variable_value(s) "off"
#endif

#ifndef HAVE_RL_READLINE_VERSION
#  define rl_readline_version 0xbaddef
#endif

#ifdef SPY_ON_READLINE
# ifndef HOMEGROWN_REDISPLAY
#  define MAYBE_MULTILINE 1
# endif
#endif

#ifdef MAYBE_MULTILINE
extern int _rl_horizontal_scroll_mode;	/* Spying on readline's private life .... */

#  define redisplay_multiple_lines (!_rl_horizontal_scroll_mode)
#else
#  define redisplay_multiple_lines (strncmp(rl_variable_value("horizontal-scroll-mode"),"off",3) == 0)
#endif






/* in main.c: */
extern int master_pty_fd;
extern int slave_pty_fd;
extern FILE *debug_fp;
extern char *program_name, *command_name;
extern int always_readline;
extern int complete_filenames;
extern int child_pid;
extern int i_am_child;
extern int nowarn;
extern int debug;
extern char *password_prompt_search_string;
extern char *history_format;
extern int ignore_queued_input;
extern int history_duplicate_avoidance_policy;
extern int one_shot_rlwrap;
extern int ansi_colour_aware;
extern int coloured_prompt;
extern int start_line_edit;
/* now follow the possible values for history_duplicate_avoidance_policy: */
#define KEEP_ALL_DOUBLES 		0
#define ELIMINATE_SUCCESIVE_DOUBLES	1
#define ELIMINATE_ALL_DOUBLES  		2


void cleanup_rlwrap_and_exit(int status);
void put_in_output_queue(char *stuff);



/* in signals.c */
extern int child_is_dead;
extern int childs_exit_status;
extern int sigterm_received;
extern int deferred_adapt_clients_winsize;
void install_signal_handlers(void);
void block_signals(int *sigs);
void unblock_signals(int *sigs);
void ignore_sigchld(void);
void suicide_by(int sig);
int adapt_tty_winsize(int from_fd, int to_fd);

/* in utils.c */
void  yield(void);
void  mysetenv(const char *name, const char *value);
void  usage(int status);
void  mywarn(const char *message, ...);
void  myerror(const char *message, ...);
void *mymalloc(size_t size);
void  close_open_files_without_writing_buffers(void);
size_t filesize(const char *filename);
void  open_logfile(const char *filename);
void  write_logfile(const char *str);
void  close_logfile(void);
void  timestamp(FILE *fp);
void  change_working_directory(void);
void  log_terminal_settings(struct termios *terminal_settings);
void  last_minute_checks(void);
extern char slaves_working_directory[];


/* in string_utils.h */

char *mybasename(char *filename);
char *mydirname(char *filename);
void  mystrlcpy(char *dst, const char *src, size_t size);
void  mystrlcat(char *dst, const char *src, size_t size);
char *mysavestring(const char *string);
char *add3strings(const char *str1, const char *str2, const char *str3);
#define add2strings(a,b)  add3strings(a,b,"")
char *mangle_char_for_debug_log(char c, int quote_me);
char *mangle_string_for_debug_log(const char *string, int maxlen);
char *search_and_replace(char *patt, char *repl, const char *string,
			 int cursorpos, int *line, int *col);
char *first_of(char **strings);
char *as_string(int i);
char *append_and_expand_history_format(char *line);
void unbackspace(char* buf);
char *mark_invisible(const char *buf);
char *copy_and_unbackspace(const char *original);
int colourless_strlen(const char *str, char **copy_without_ignore_markers);
int colourless_strlen_unmarked (const char *str);
char *get_last_screenline(char *long_line, int termwidth);

/* in readline.c: */
extern struct rl_state
{				/* struct to save readline state while we're processing output from slave command*/
  char *text;                   /* current input buffer */
  char *prompt;                 /* current prompt */  
  char *coloured_prompt;        /* ditto with colour added */
  int point;                    /* cursor position within input buffer */
  int already_saved;		/* flag set when saved, cleared when restored */
} saved_rl_state;

void save_rl_state(void);
void restore_rl_state(void);
void init_readline(char *);
void line_handler(char *);
void my_redisplay(void);
void initialise_colour_codes(char *colour);
void reprint_prompt(int coloured);
char *colourise (const char *prompt);
void move_cursor_to_start_of_prompt(void);
int prompt_is_single_line(void);

extern int within_line_edit, transparent;
extern char *multiline_separator;
extern char *pre_given;
extern int leave_prompt_alone;

/* in pty.c: */
pid_t my_pty_fork(int *, const struct termios *, const struct winsize *);
int slave_is_in_raw_mode(void);
struct termios *get_pterm_slave(void);
void mirror_slaves_echo_mode(void);
void completely_mirror_slaves_terminal_settings(void);
void write_EOF_to_master_pty(void);
void write_EOL_to_master_pty(char *);

/* in ptytty.c: */
int ptytty_get_pty(int *fd_tty, const char **ttydev);
int ptytty_get_tty(const char *ttydev);
int ptytty_control_tty(int fd_tty, const char *ttydev);
int ptytty_openpty(int *amaster, int *aslave, const char **name);



/* in completion.rb: */
void init_completer(void);
void feed_file_into_completion_list(const char *);
void feed_line_into_completion_list(const char *);
char *my_completion_function(char *, int);

extern int completion_is_case_sensitive;
extern int remember_for_completion;

/* in term.c: */
int redisplay;			/* TRUE when user input should be readable (instead of *******)  */
void init_terminal(void);
void set_echo(int);
void prepare_terminal(void);
void cr(void);
void backspace(int);
void clear_line(void);
void clear_the_screen(void);
void curs_up(void);
void curs_down(void);
void test_terminal(void);
int my_putchar(int c);
int my_putstr(const char *string);
void cursor_hpos(int col);
extern struct termios saved_terminal_settings;
extern int terminal_settings_saved;
extern struct winsize window_size;
extern char *term_name;
extern char *term_backspace, term_eof, term_stop, *term_cursor_hpos,
  *term_cursor_up, *term_cursor_down, *term_newline;


/* some handy macros */
#ifndef TRUE
#  define TRUE 1
#endif

#ifndef FALSE
#  define FALSE 0
#endif

#ifndef min
# define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
# define max(a,b) ((a) < (b) ? (b) : (a))
#endif


#include "malloc_debug.h" /* malloc_debug.{c,h} not ready for prime time */

#define DEBUG_FILENAME "/tmp/rlwrap.debug"

/* DPRINTF0 and its ilk  doesn't produce any output except when DEBUG is #defined (via --enable-debug configure option) */

#ifdef  DEBUG

#  define MANGLE_LENGTH                          50
#  define DEBUG_TERMIO                     	 1
#  define DEBUG_SIGNALS                    	 2
#  define DEBUG_READLINE                   	 4
#  define DEBUG_MEMORY_MANAGEMENT                8   /* used with malloc_debug.c */
#  define DEBUG_AD_HOC                           16  /* only used for quick and dirty printf-style debugging */

#  define DEBUG_WITH_TIMESTAMPS                  128 /* add timestamps to every line in debug log    */
#  define FORCE_HOMEGROWN_REDISPLAY              256 /* force use of my_homegrown_redisplay()        */

#  define DEBUG_ALL                        	 (DEBUG_TERMIO | DEBUG_SIGNALS | DEBUG_READLINE)
                                                                   
#  ifdef __GNUC__
#    define WHERE_AND_WHEN              	  int debug_saved = debug; \
                                                  if(debug & DEBUG_WITH_TIMESTAMPS) timestamp(debug_fp);\
                                                  debug = 0; /* don't debug while evaluating the DPRINTF arguments */ \
                                                  fprintf(debug_fp, "%15s:%-3d\t%-25s ", __FILE__, __LINE__, __FUNCTION__)
#  else
#    define WHERE_AND_WHEN                        int debug_saved = debug; \
                                                  if(debug & DEBUG_WITH_TIMESTAMPS) timestamp(debug_fp);\
                                                  debug = 0;\
                                                  fprintf(debug_fp, "%15s:%-3d\t", __FILE__, __LINE__)
#  endif


#  define NL_AND_FLUSH                            fputc('\n', debug_fp) ; fflush(debug_fp); debug = debug_saved;

#  define DPRINTF0(mask, format)\
  if (debug & mask && debug_fp) {WHERE_AND_WHEN; fprintf(debug_fp, format); NL_AND_FLUSH; }

#  define DPRINTF1(mask, format,arg)\
  if (debug & mask && debug_fp) {WHERE_AND_WHEN; fprintf(debug_fp, format, arg); NL_AND_FLUSH; }

#  define DPRINTF2(mask, format,arg1, arg2)\
  if (debug & mask && debug_fp) {WHERE_AND_WHEN; fprintf(debug_fp, format, arg1, arg2); NL_AND_FLUSH; }

#  define DPRINTF3(mask, format,arg1, arg2, arg3)\
  if (debug & mask && debug_fp) {WHERE_AND_WHEN; fprintf(debug_fp, format, arg1, arg2, arg3); NL_AND_FLUSH; }

#  define DPRINTF4(mask, format,arg1, arg2, arg3, arg4)\
  if (debug & mask && debug_fp) {WHERE_AND_WHEN; fprintf(debug_fp, format, arg1, arg2, arg3,arg4); NL_AND_FLUSH; }

#  define ERRMSG(b)                               (b && (errno != 0) ? add3strings("(", strerror(errno), ")") : "" )

#  define SHOWCURSOR                              {my_putchar('*'); DPRINTF0(DEBUG_ALL, "showcursor"); sleep(1); backspace(1);} /* (look out for last column!)*/



#else 

#  define DPRINTF0(mask, format)
#  define DPRINTF1(mask, format,arg)
#  define DPRINTF2(mask, format,arg1, arg2)
#  define DPRINTF3(mask, format,arg1, arg2, arg3)
#  define DPRINTF4(mask, format,arg1, arg2, arg3, arg4)
#  define ERRMSG(b)
#  define SHOWCURSOR
#  define NDEBUG		/* disable assertions */
#endif

#include <assert.h>
