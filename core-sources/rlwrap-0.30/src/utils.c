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



#include "rlwrap.h"




char slaves_working_directory[MAXPATHLEN+1]; 

static FILE *log_fp;



void
yield()
{
  #ifdef HAVE_SCHED_YIELD
    sched_yield();            
  #else
    { struct timeval ten_millisecs = { 0, 10000 }; 
      select(0, NULL, NULL, NULL, &ten_millisecs); /* poor man's sched_yield() */
    }	
  #endif
}

void
mysetenv(const char *name, const char *value)
{
  int return_value = 0;
  char *name_is_value;
  
  #ifdef HAVE_SETENV
     return_value = setenv(name, value, TRUE);   
  #elif defined(HAVE_PUTENV)
     name_is_value = add3strings (name, "=", value);
     return_value = putenv (name_is_value);
  #else /* won't happen, but anyway: */	   
     mywarn("setting environment variable %s=%s failed, as this system has neither setenv() nor putenv()", name, value);
  #endif
     
  if (return_value != 0)
    mywarn("setting environment variable %s=%s failed%s", name, value,
	   (errno ? "" : " (insufficient environment space?)"));     /* will setenv(...) = -1  set errno? */
}       
	  


/* private helper function for myerror() and mywarn() */
static void
utils_warn(const char *message, va_list ap)
{

  int saved_errno = errno;
  char buffer[BUFFSIZE];
  

  snprintf2(buffer, sizeof(buffer) - 1, "%s-%s: ", program_name, VERSION);
  vsnprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer) - 1,
	    message, ap);
  if (saved_errno)
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer) - 1,
	     ": %s", strerror(saved_errno));
  mystrlcat(buffer, "\n", sizeof(buffer));

  fflush(stdout);
  fputs(buffer, stderr);
  fflush(stderr);
  DPRINTF1(DEBUG_ALL,
	   "Error/warning: %s\n(use the -n option to suppress warnings)",
	   buffer);
  /* we want this because sometimes error messages (esp. from client) are dropped) */

}

/* myerror("utter failure in %s", where) prints a NL-terminated error
   message ("rlwrap: utter failure in xxxx\n") and exits rlwrap */


static char*
markup(const char*str)
{
  if (isatty(STDOUT_FILENO) && (ansi_colour_aware || coloured_prompt))
    return add3strings("\033[1;31m", str,"\033[0m");
  else
    return mysavestring(str);
}	

void
myerror(const char *message, ...)
{
  va_list ap;
  char *error_message = add2strings(markup("error: "), message);

  va_start(ap, message);
  utils_warn(error_message, ap);
  va_end(ap);
  if (!i_am_child)
    cleanup_rlwrap_and_exit(EXIT_FAILURE);
  else /* child: die and let parent clean up */
    exit(-1);
}


void
mywarn(const char *message, ...)
{
  va_list ap;
  char *warning_message;

  if (nowarn)
    return;
  warning_message = add2strings(markup("warning: "), message);
  va_start(ap, message);
  utils_warn(warning_message, ap);
  va_end(ap);
  fputs("use the --no-warnings option to suppress warnings\n", stderr);
}


void
open_logfile(const char *filename)
{
  time_t now;

  log_fp = fopen(filename, "a");
  if (!log_fp)
    myerror("Cannot write to logfile %s", filename);
  now = time(NULL);
  fprintf(log_fp, "\n\n[rlwrap] %s\n", ctime(&now));
}

void
write_logfile(const char *str)
{
  if (log_fp)
    fputs(str, log_fp);
}


size_t
filesize(const char *filename)
{
  struct stat buf;

  if (stat(filename, &buf))
    myerror("couldn't stat file %s", filename);
  return (size_t) buf.st_size;
}

void
close_logfile()
{
  if (log_fp)
    fclose(log_fp);
}

void
close_open_files_without_writing_buffers() /* called from child just before exec(command) */
{
  if(log_fp)
    close(fileno(log_fp));  /* don't flush buffers to avoid avoid double double output output */
  if (debug)
    close(fileno(debug_fp));
}


void
timestamp(FILE *fp)
{
  struct timeval now;
  static struct timeval firsttime;
  static int never_called = 1;
  long diff_usec;
  float diff_sec;
  
  gettimeofday(&now, NULL);
  if (never_called) {
    firsttime = now;
    never_called = 0;
  }
  diff_usec = 1000000 * (now.tv_sec -firsttime.tv_sec) + (now.tv_usec - firsttime.tv_usec);
  diff_sec = diff_usec / 1000000.0;
  fprintf(fp, "%4.3f", diff_sec);
}	
  

/* change_working_directory() changes rlwrap's working directory to /proc/<child_pid>/cwd
   (on systems where this makes sense, like linux and Solaris) */
   
void
change_working_directory()
{
#ifdef HAVE_PROC_PID_CWD
  static char proc_pid_cwd[MAXPATHLEN+1];
  static int initialized = FALSE;
  int linklen = 0;

  snprintf0(slaves_working_directory, MAXPATHLEN, "?");

  if (!initialized && child_pid > 0) { 	/* first time we're called after birth of child */
    snprintf1(proc_pid_cwd, MAXPATHLEN , "/proc/%d/cwd", child_pid);
    initialized = TRUE;
  }	
  if (chdir(proc_pid_cwd) == 0) {
#ifdef HAVE_READLINK
    linklen = readlink(proc_pid_cwd, slaves_working_directory, MAXPATHLEN);
    if (linklen > 0)
      slaves_working_directory[linklen] = '\0';
    else
      snprintf1(slaves_working_directory, MAXPATHLEN, "? (%s)", strerror(errno));
#endif /* HAVE_READLINK */
  }		    
#else
  /* do nothing at all */
#endif
}


#define isset(flag) ((flag) ? "set" : "unset")

/* print info about terminal settings */
void log_terminal_settings(struct termios *terminal_settings) {
  DPRINTF1(DEBUG_TERMIO, "clflag.ISIG is %s", isset(terminal_settings->c_cflag | ISIG));
  DPRINTF1(DEBUG_TERMIO, "cc_c[VINTR] is %d", terminal_settings->c_cc[VINTR]);
}

  

/* print info about option, considering whether we HAVE_GETOPT_LONG and whether GETOPT_GROKS_OPTIONAL_ARGS */
static void print_option(char shortopt, char *longopt, char*argument, int optional, char *comment) {
  int long_opts, optional_args;
  char *format;
  char *maybe_optional = "";
  char *longoptional = "";

  
#ifdef HAVE_GETOPT_LONG
  long_opts = TRUE;
#else
  long_opts = FALSE;
#endif

#ifdef GETOPT_GROKS_OPTIONAL_ARGS
  optional_args = TRUE;
#else
  optional_args = FALSE;
#endif

  if (argument) {
    maybe_optional = (optional_args && optional ? add3strings("[", argument,"]") :  add3strings(" <", argument,">"));
    longoptional = (optional ? add3strings("[=", argument,"]") : add3strings("=<", argument, ">"));
  }
  
  /* if we cannot use long options, use the long option as a reminder (no warnings) instead of "--no-warnings" */ 
  if (!long_opts)
    longopt = search_and_replace("-"," ", longopt, 0, NULL,NULL);
  format = add2strings ("  -%c%-24.24s", (long_opts  ? " --%s%s" : "(%s)")); 
  fprintf(stderr, format, shortopt, maybe_optional, longopt, longoptional);
  if (comment)
    fprintf(stderr, " %s", comment);
  fprintf(stderr, "\n");
  /* don't free allocated strings: we'll exit() soon */
}	


/* some last-minute checks before we can start */
void
last_minute_checks()
{
  if (rl_editing_mode == 0 && term_cursor_up == NULL)
    mywarn("\nWith this terminal (%s) %s will print all input lines twice in vi mode.\n"
	   "Try a different terminal (or try setenv TERM=ansi) to get rid of this!\n", term_name, program_name); 
  #ifdef DEBUG
  if (debug & DEBUG_MEMORY_MANAGEMENT)
    mywarn("\nDebugging memory management uses a\n"
	   "primitive home-grown malloc/free debugger which will leak \n"
	   "lots of memory; it may also crash easily");
  #endif
}

void
usage(int status)
{
  fprintf(stderr, "Usage: %s [options] command ...\n"
	          "\n"
	          "Options:\n", program_name);

  print_option('a', "always-readline", "password:", TRUE, NULL);
  print_option('A', "ansi-colour-aware", NULL, FALSE, NULL);
  print_option('b', "break-chars", "chars", FALSE, NULL);
  print_option('c', "complete-filenames", NULL, FALSE, NULL);
  print_option('C', "command-name", "name|N", FALSE, NULL);
  print_option('D', "history-no-dupes", "0|1|2", FALSE, NULL);
  print_option('f', "file", "completion list", FALSE,NULL);
  print_option('F', "history-format", "format string", FALSE,NULL);
  print_option('h', "help", NULL, FALSE, NULL);
  print_option('H', "history-filename", "file", FALSE, NULL);
  print_option('i', "case-insensitive", NULL, FALSE, NULL);
  print_option('l', "logfile", "file", FALSE, NULL);
  print_option('n', "no-warnings", NULL, FALSE, NULL);
  print_option('p', "prompt-colour", "ANSI colour spec", TRUE, NULL);
  print_option('P', "pre-given","input", FALSE, NULL);
  print_option('q', "quote-characters", "chars", FALSE, NULL);
  print_option('m', "multi-line", "newline substitute", TRUE, NULL);
  print_option('r', "remember", NULL, FALSE, NULL);
  print_option('v', "version", NULL, FALSE, NULL);
  print_option('s', "histsize", "N", FALSE,"(negative: readonly)");	
  print_option('t', "set-term-name", "name", FALSE, NULL);  

#ifdef DEBUG
  fprintf(stderr, "\n");
  print_option('T', "test-terminal", NULL, FALSE, NULL);
  print_option('d', "debug", "mask", TRUE, add3strings("(output sent to ", DEBUG_FILENAME,")"));
  fprintf(stderr,
	  "             \n"
	  "The -d or --debug option *must* come first\n"
	  "The debugging mask is a bit mask obtained by adding:\n"
	  "    %3d    if you want to debug termio\n"
	  "    %3d    if you want to debug signals\n"
	  "    %3d    if you want to debug readline handling\n"
	  "    %3d    if you want to debug memory management\n"
	  "    %3d    to add timestaps to the debug log\n"
	  "    %3d    to force the use of my_homegrown_redisplay()\n"
	  "    default debug mask = 7 (debug termio, signals and readline handling)\n",

	  DEBUG_TERMIO, DEBUG_SIGNALS, DEBUG_READLINE, DEBUG_MEMORY_MANAGEMENT,
	  DEBUG_WITH_TIMESTAMPS, FORCE_HOMEGROWN_REDISPLAY);	  
#endif

  fprintf(stderr,
	  "\n"
	  "bug reports, suggestions, updates:\n"
	  "http://utopia.knoware.nl/~hlub/uck/rlwrap/\n");

  exit(status);
}	    
  

#ifdef DEBUG
#undef mymalloc
#endif

/* malloc with simplistic error handling: just bail out when out of memory */
void *
mymalloc(size_t size)
{			
  void *ptr;
  ptr = malloc(size);
  if (ptr == NULL) {
    /* don't call myerror(), as this calls mymalloc() again */
    fprintf(stderr, "Out of memory: tried in vain to allocate %d bytes\n", (int) size);
    exit(EXIT_FAILURE);
  }	
  return ptr;
}

  
