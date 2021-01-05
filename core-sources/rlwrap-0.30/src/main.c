/* main.c: main(), initialisation and cleanup
 * (C) 2000-2007 Hans Lub
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License , or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  You may contact the author by e-mail:  hlub@knoware.nl
 */

#include "rlwrap.h"

/* global vars */
int master_pty_fd;		     /* master pty (rlwrap uses this to communicate with client) */
int slave_pty_fd;		     /* slave pty (client uses this to communicate with rlwrap,
				      * we keep it open after forking in order to keep track of
				      * client's terminal settings */
FILE *debug_fp = NULL;  	     /* filehandle of debugging log (!= stderr) */
char *program_name, *command_name;   /* "rlwrap" and (base-)name of command */
int within_line_edit = FALSE;	     /* TRUE while user is editing input */
int always_readline = FALSE;	     /* -a option */
char *password_prompt_search_string  /* argument of -a option (cf manpage) */
   = NULL;
char *history_format = NULL;         /* format for history entries */
int complete_filenames = FALSE;	     /* -c option */
int child_pid = 0;		     /* pid of child (client) */
int i_am_child = FALSE;		     /* Am I child or parent? after forking, child will set this to TRUE */
int nowarn = FALSE;		     /* argument of -n option (suppress warnings) */
int debug = 0;			     /* debugging mask (0 = no debugging) */
int ignore_queued_input = FALSE;     /* read and then ignore all characters in input queue until it is empty (i.e. read would block) */
int history_duplicate_avoidance_policy =
   ELIMINATE_SUCCESIVE_DOUBLES;      /* whether and how to avoid duplicate history entries */
int one_shot_rlwrap = FALSE;	     /* whether we should exit after first accepted input line */
int ansi_colour_aware = FALSE;       /* whether readline should be made aware of ANSI colour codes in prompt */
int coloured_prompt = FALSE;	     /* whether we should paint a suspected prompt */
int start_line_edit = FALSE;         /* flag set in SIGWINCH signal handler: start line edit as soon as possible */


/* private vars */
static char *history_filename = NULL;
static int  histsize = 300;
static int  write_histfile = TRUE;
static char *completion_filename, *default_completion_filename;
static char *full_program_name;
static int  last_option_didnt_have_optional_argument = FALSE;
static int  last_opt = -1;
static int  bleach_output = FALSE;
static char *client_term_name = NULL; /* we'll set TERM to this before exec'ing client command */


/* private functions */
static void init_rlwrap(void);
static void fork_child(char *command_name, char **argv);
static char *read_options_and_command_name(int argc, char **argv);
static void main_loop(void);
static void flush_output_queue(void);
static int  output_queue_is_nonempty(void);

/* options */
#ifdef GETOPT_GROKS_OPTIONAL_ARGS
static char optstring[] = "+:a::Ab:cC:d::D:f:F:hH:il:nm::p::P:q:rs:t:Tv"; /* +: is not really documented. configure checks wheteher it works as expected
					        			   if not, GETOPT_GROKS_OPTIONAL_ARGS is undefined. @@@ */
#else
static char optstring[] = "+:a:Ab:cC:d:D:f:F:hH:il:nm:p:P:q:rs:t:Tv";	
#endif

#ifdef HAVE_GETOPT_LONG
static struct option longopts[] = {
  {"always-readline", 		optional_argument, 	NULL, 'a'},
  {"ansi-colour-aware",         no_argument,            NULL, 'A'},
  {"break-chars", 		required_argument, 	NULL, 'b'},
  {"complete-filenames", 	no_argument, 		NULL, 'c'},
  {"command-name", 		required_argument, 	NULL, 'C'},
  {"debug", 			optional_argument, 	NULL, 'd'},
  {"history-no-dupes", 		required_argument, 	NULL, 'D'},
  {"file", 			required_argument, 	NULL, 'f'},
  {"history-format", 		required_argument, 	NULL, 'F'},
  {"help", 			no_argument, 		NULL, 'h'},
  {"history-filename", 		required_argument, 	NULL, 'H'},
  {"case-insensitive", 		no_argument, 		NULL, 'i'},
  {"logfile", 			required_argument, 	NULL, 'l'},
  {"no-warnings", 		no_argument, 		NULL, 'n'},
  {"multi-line", 		optional_argument, 	NULL, 'm'},
  {"prompt-colour",             optional_argument,      NULL, 'p'},
  {"pre-given", 		required_argument, 	NULL, 'P'},
  {"quote-characters",          required_argument,      NULL, 'q'},
  {"remember", 			no_argument, 		NULL, 'r'},
  {"version", 			no_argument, 		NULL, 'v'},
  {"histsize", 			required_argument, 	NULL, 's'},
  {"set-terminal-name",         required_argument,      NULL, 't'},        
  {"test-terminal",  		no_argument, 		NULL, 'T'},
  {0, 0, 0, 0}
};
#endif


int
main(int argc, char **argv)
{ 
  char *command_name;
  init_completer();
  command_name = read_options_and_command_name(argc, argv);
  /* by now, optind points to <command>, and &argv[optind] is <command>'s argv */
  if (!isatty(STDIN_FILENO) && execvp(argv[optind], &argv[optind]) < 0)
       /* if stdin is not a tty, just execute <command> */ 
       myerror("Cannot execute %s", argv[optind]);	
  init_rlwrap();
  fork_child(command_name, argv);
  main_loop();
  return 42;			/* not reached, but some compilers are unhappy without this ... */
}


/*
 * create pty pair and fork using my_pty_fork; parent returns immediately; child
 * executes the part of rlwrap's command line that remains after
 * read_options_and_command_name() has harvested rlwrap's own options
 */  
static void
fork_child(char *command_name, char **argv)
{
  char *arg = argv[optind], *p;
  
  int pid =
    my_pty_fork(&master_pty_fd, &saved_terminal_settings, &window_size);
  if (pid > 0)			/* parent: */
    child_pid = pid;
  else {			/* child: */
    DPRINTF1(DEBUG_TERMIO, "preparing to execute %s", arg);
    close_open_files_without_writing_buffers();
    
    if (client_term_name)
      mysetenv("TERM", client_term_name);   
    if (execvp(argv[optind], &argv[optind]) < 0) {
      if (last_opt > 0 && last_option_didnt_have_optional_argument) { /* e.g. 'rlwrap -a Password: sqlpus' will try to exec 'Password:' */
	for (p=" '; !(){}"; *p; p++) /* does arg need shell quoting? */ 
	  if (strchr(arg,*p)) { 
	      arg = add3strings("'", arg,"'"); /* quote it  */
	      break;
	  }	
	fprintf(stderr, "Did you mean '%s' to be an option argument?\nThen you should write -%c%s, without the space(s)\n",
		      argv[optind], last_opt, arg); 
      }
      myerror("Cannot execute %s", argv[optind]);   	/* stillborn child, parent will live on and display child's last gasps */
    }
  }
}


/*
 * main loop: listen on stdin (for user input) and master pty (for command output),
 * and try to write output_queue to master_pty (if it is not empty)
 * This function never returns.
 */
void
main_loop()
{				
  int nfds;			
  fd_set readfds;	
  fd_set writefds;
  int nread;		
  struct timeval timeout, *timeoutptr,
                 immediately = { 0, 0 }, /* zero timeout when child is dead */
                 wait_a_little = {0, 40 * 1000},
                 *forever = NULL;
  char buf[BUFFSIZE], *last_nl, *new_prompt, *timeoutstr;
  int messysigs[] = { SIGWINCH, SIGTSTP, 0 };	/* we don't want to catch these
						   while readline is processing */
  int promptlen = 0;       
  int we_may_have_to_colour_a_prompt = FALSE;

  init_readline("");
  last_minute_checks();
  set_echo(FALSE);		/* This will also put the terminal in CBREAK mode */

  /* ------------------------------  main loop  -------------------------------*/
  while (TRUE) {
    /* listen on both stdin and pty_fd */
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(master_pty_fd, &readfds);

    /* try to write output_queue to master_pty (but only if it is nonempty) */
    FD_ZERO(&writefds);
    if (output_queue_is_nonempty())
      FD_SET(master_pty_fd, &writefds);




    if (child_is_dead || ignore_queued_input) {
      timeout = immediately;
      timeoutptr = &timeout;
      timeoutstr = "immediately";
    } else if (we_may_have_to_colour_a_prompt) {
      timeout = wait_a_little;
      timeoutptr = &timeout;
      timeoutstr = "wait_a_little";
    } else {
      timeoutptr = forever;
      timeoutstr = "forever";
    }
     
    DPRINTF1(DEBUG_TERMIO, "calling select() with timeout %s",  timeoutstr);
    unblock_signals(messysigs);	/* Handle the "messy" signals SIGWINCH and SIGTSTP now (and only now) */
    nfds = select(1 + master_pty_fd, &readfds, &writefds, NULL, timeoutptr);
    block_signals(messysigs);	/* Dont't disturb now */

    DPRINTF3(DEBUG_TERMIO, "select() returned  %d (%s ready), within_line_edit=%d", nfds,
	     (FD_ISSET(master_pty_fd, &readfds) ? "pty" : "stdin"),
	     within_line_edit);


    if (start_line_edit) {  /* start_line_edit flag means we've had a WINCH while within_line_edit was FALSE */
      DPRINTF0(DEBUG_READLINE, "Starting line edit as a result of WINCH ");
      within_line_edit = TRUE;
      if (coloured_prompt)  {
	saved_rl_state.coloured_prompt = colourise(saved_rl_state.prompt);
	my_putstr(saved_rl_state.coloured_prompt);
      } else {
	my_putstr(saved_rl_state.prompt);
      } 
      restore_rl_state();
      start_line_edit = FALSE;
      continue;
    }	
    
    if (nfds < 0) {		/* exception  */	
      if (errno == EINTR) {	/* interrupted by signal */
	continue;
      }	else
	myerror("select received exception");
    } else if (nfds == 0) {
      
        /* timeout, which can only happen when .. */
      if (ignore_queued_input) {       /* ... we have read all the input keystrokes that should
					  be ignored (i.e. those that accumulated on stdin while we
				          were calling an external editor) */
	ignore_queued_input = FALSE;
	continue;
      } else if (child_is_dead) {                         /* ... or else, if child is dead, ... */
	if (promptlen > 0)	/* ... and if its last words were not terminated by \n: ... */
	  my_putchar('\n');	/* ... print the \n ourselves */
	DPRINTF2(DEBUG_SIGNALS,
		 "select returned 0, child_is_dead=%d, childs_exit_status=%d",
		 child_is_dead, childs_exit_status);
	cleanup_rlwrap_and_exit(EXIT_SUCCESS);
      }	else if (coloured_prompt && we_may_have_to_colour_a_prompt) {
	if (within_line_edit) 	
	  save_rl_state();      
	reprint_prompt(TRUE);
	if (within_line_edit)
	  restore_rl_state();
	we_may_have_to_colour_a_prompt = FALSE;
      } else {
	myerror("unexpected timeout on stdin");
      }
    } else if (nfds > 0) {	/* Hah! something to read or write */


      /* -------------------------- write pty --------------------------------- */
      if (FD_ISSET(master_pty_fd, &writefds)) {
	flush_output_queue();
	yield(); /*  give  slave command time to respond. If we don't do this,
		     nothing bad will happen, but the "dialogue" on screen will be
		     out of order   */
      }	

      
      /* -------------------------- read pty --------------------------------- */
      if (FD_ISSET(master_pty_fd, &readfds)) { /* there is something to read on master pty: */
	if ((nread = read(master_pty_fd, buf, BUFFSIZE - 1)) <= 0) { /* read it */
	  if (nread == EINTR)	/* interrupted by signal ...*/	                     
	    continue;           /* ... don't worry */
	  else if (child_is_dead || nread < 0) { /* child is dead or has closed its stdout */
	    if (promptlen > 0)	/* commands dying words were not terminated by \n ... */
	      my_putchar('\n');	/* provide the missing \n */
	    cleanup_rlwrap_and_exit(EXIT_SUCCESS);
	  } else
	    myerror("read error on master pty");
	}
	buf[nread] = '\0';      /* buf contains nread chars worth of clients output, zero-terminate it */

	if (within_line_edit)	/* client output arrives while we're editing keyboard input:  */
	  save_rl_state();      /* temporarily disable readline */

        if (coloured_prompt && !we_may_have_to_colour_a_prompt) 
	  reprint_prompt(FALSE); /* reprint the (coloured) prompt without colour */
      
	my_putstr(buf); /* print the client output on screen */
	DPRINTF2(DEBUG_READLINE, "wrote %d bytes: %s", (int) strlen(buf),
		 mangle_string_for_debug_log(buf, 40));

	write_logfile(buf);


	/* now determine the text *after* the last newline. This wil be the
	   "prompt" for the readline input routine; we'll update
	   saved_rl_state.prompt accordingly   */
        last_nl = strrchr(buf, '\n');
	if (last_nl != NULL) {/* newline seen, will get new prompt: */
          new_prompt = mysavestring(last_nl +1);
	  leave_prompt_alone = FALSE;
	  if (remember_for_completion) {
	    *last_nl = '\0';
	    feed_line_into_completion_list(buf);
	  }
	}	
	else 	  /* no newline, extend old prompt: */
    	  new_prompt = add2strings(saved_rl_state.prompt, buf);
	
	unbackspace(new_prompt); /* replace e.g. 123\ra<BS>bcde by bcde. we do this before we figure
				    out whether the prompt has wrapped around, otherwise we will be wrong */	
	free(saved_rl_state.prompt);

	saved_rl_state.prompt = (prompt_is_single_line() ?
				   get_last_screenline(new_prompt,  window_size.ws_col) :
				   copy_and_unbackspace(new_prompt));	 

	free(new_prompt);
	DPRINTF2(DEBUG_READLINE, "Prompt now: '%s' (%d bytes)",
		 mangle_string_for_debug_log(saved_rl_state.prompt, 40),  (int) strlen(saved_rl_state.prompt));
	
	if (coloured_prompt) { /* compute coloured prompt  */
	  if (saved_rl_state.coloured_prompt)
	    free(saved_rl_state.coloured_prompt);
	  saved_rl_state.coloured_prompt = (*saved_rl_state.prompt ? /* is the prompt non_empty? */ 
					    colourise(saved_rl_state.prompt) : /* colour it */
					    mysavestring("")); /* empty string, new copy so that we can free it later */

	  we_may_have_to_colour_a_prompt = TRUE; /* but don't colour it yet: first wait a little
						    whether more command output arrives, or user presses a key*/
	}	
	if (within_line_edit)
	  restore_rl_state();

	yield();  /* wait for what client has to say .... */ 
	continue; /* ... and don't attempt to process keyboard input as long as it is talking ,
		     in order to avoid re-printing the current prompt (i.e. unfinished output line) */
      }

      
      /* ----------------------------- key pressed: read stdin -------------------------*/
      if (FD_ISSET(STDIN_FILENO, &readfds)) {	/* key pressed */
	nread = read(STDIN_FILENO, buf, 1);	/* read next byte of input   */
	if (nread < 0)
	  myerror("Unexpected error");
	else if (ignore_queued_input)
	  continue;             /* do nothing with it*/
	else if (nread == 0)	/* EOF on stdin */
	  cleanup_rlwrap_and_exit(EXIT_SUCCESS);

	assert(nread == 1);
	if (slave_is_in_raw_mode() && !always_readline) {	/* just pass it on */
	  buf[nread] = '\0';	/* only necessary for DPRINTF, 
				   the '\0' won't be written to master_pty */
	  DPRINTF2(DEBUG_READLINE,
		   "passed on %d bytes in transparent mode: %s", (int) strlen(buf),
		   mangle_string_for_debug_log(buf, 40));
	  write(master_pty_fd, buf, nread);
	  completely_mirror_slaves_terminal_settings(); /* this is of course 1 keypress too late: we should
							   mirror the terminal settings *before* the user presses a key.
							   (maybe using rl_event_hook??)   @@@FIXME */
	} else {		/* hand it over to readline */
	  if (!within_line_edit) {	/* start a new line edit    */
	    DPRINTF0(DEBUG_READLINE, "Starting line edit");
	    within_line_edit = TRUE;
	    restore_rl_state();
	  }

	  rl_pending_input = buf[0];	/* stuff it back in readline's input queue */

	  DPRINTF2(DEBUG_READLINE, "Character %d (%s)",
		   rl_pending_input, mangle_char_for_debug_log(rl_pending_input, TRUE)); /* @@@ memory leak when debugging */

	  if (buf[0] == term_eof && strlen(rl_line_buffer) == 0)	/* kludge: readline on Solaris does not interpret CTRL-D as EOF */
	    line_handler(NULL);
	  else
	    
	    rl_callback_read_char();
	}
      }
    

    }				/* if (ndfs > 0)         */
  }				/* while (1)             */
}				/* void main_loop()      */


/* Read history and completion word lists */
void
init_rlwrap()
{

  char *homedir, *histdir, *homedir_prefix;

  /* open debug file if necessary */

  if (debug) {    
    debug_fp = fopen(DEBUG_FILENAME, "w");
    if (!debug_fp)
      myerror("Couldn't open debug file %s", DEBUG_FILENAME);
    setbuf(debug_fp, NULL);
  }

  DPRINTF0(DEBUG_TERMIO, "Initialising");
  init_terminal();


  /* Determine rlwrap home dir and prefix for default history and completion filenames */
  homedir = (getenv("RLWRAP_HOME") ? getenv("RLWRAP_HOME") : getenv("HOME"));
  homedir_prefix = (getenv("RLWRAP_HOME") ?                    /* is RLWRAP_HOME set?                */
		    add2strings(getenv("RLWRAP_HOME"), "/") :  /* use $RLWRAP_HOME/<command>_history */
		    add2strings(getenv("HOME"), "/."));	       /* if not, use ~/.<command>_history   */

  /* Determine history file name and check its existence and permissions */

  if (history_filename) {
    histdir = mydirname(history_filename);
  } else {
    histdir = homedir;
    history_filename = add3strings(homedir_prefix, command_name, "_history");
  }
  if (write_histfile) {
    if (access(history_filename, F_OK) == 0) {	/* already exists, can we read/write it? */
      if (access(history_filename, R_OK | W_OK) != 0) {
	myerror("cannot read and write %s", history_filename);
      }
    } else {			/* doesn't exist, can we create it? */
      if (access(histdir, W_OK) != 0) {
	myerror("cannot create history file in %s", histdir);
      }
    }
  } else {			/* ! write_histfile */
    if (access(history_filename, R_OK) != 0) {
      myerror("cannot read %s", history_filename);
    }
  }

  /* Initialize history */
  using_history();
  stifle_history(histsize);
  read_history(history_filename);	/* ignore errors here: history file may not yet exist, but will be created on exit */


  /* Determine completion file name (completion files are never written to,
     and ignored when unreadable or non-existent) */

  completion_filename =
    add3strings(homedir_prefix, command_name, "_completions");
  default_completion_filename =
    add3strings(DATADIR, "/rlwrap/", command_name);

  rl_readline_name = command_name;

  /* Initialise completion list (if <completion_filename> is readable) */
  if (access(completion_filename, R_OK) == 0) {
    feed_file_into_completion_list(completion_filename);
  } else if (access(default_completion_filename, R_OK) == 0) {
    feed_file_into_completion_list(default_completion_filename);
  }

  saved_rl_state.text = mysavestring("");
  saved_rl_state.prompt = mysavestring("");
  saved_rl_state.coloured_prompt = mysavestring("");
}

/*
 * On systems where getopt doens't handle optional argments, warn the user whenever an
 * argument of the form -<letter> is seen, or whenever the argument is the last item on the command line
 * (e.g. 'rlwrap -a command', which will be parsed as 'rlwrap --always-readline=command')
 */

static char *
check_optarg(char opt, int remaining)
{
  if (!optarg)
    last_option_didnt_have_optional_argument = TRUE; /* if command is not found, suggest that it may have been meant
						        as optional argument (e.g. 'rlwrap -a password sqlplus' will try to
						        execute 'password sqlplus' ) */
#ifndef GETOPT_GROKS_OPTIONAL_ARGS
  if (optarg &&    /* is there an optional arg? have a look at it: */
      ((optarg[0] == '-' && isalpha(optarg[1])) || /* looks like next option */
       remaining == 0)) /* or is last item on command line */

  mywarn
      ("on this system, the getopt() library function doesn't\n"
       "grok optional arguments, so '%s' is taken as an argument to the -%c option\n"
       "Is this what you meant? If not, please provide an argument.\n", optarg, opt);
#endif
  
  return optarg;
}


/* find name of current option
 */
static const char *
current_option(int opt, int longindex)
{
  static char buf[BUFFSIZE];
#ifdef HAVE_GETOPT_LONG
  if (longindex >=0) {
    sprintf(buf, "--%s", longopts[longindex].name);
    return buf;
  }	
#endif
  sprintf(buf, "-%c", opt);
  return buf;
}


char *
read_options_and_command_name(int argc, char **argv)
{
  int c;
  char *opt_C = NULL;
  int option_count = 0;
  int opt_b = FALSE;
  int opt_f = FALSE;
  int remaining = -1; /* remaining number of arguments on command line */
  int longindex = -1; /* index of current option in longopts[], set by getopt_long */
  
  
  full_program_name = mysavestring(argv[0]);
  program_name = mybasename(full_program_name);	/* normally "rlwrap"; needed by myerror() */
  rl_basic_word_break_characters = " \t\n\r(){}[],+-=&^%$#@\";|\\";

  opterr = 0;			/* we do our own error reporting */

  while (1) {
#ifdef HAVE_GETOPT_LONG
    c = getopt_long(argc, argv, optstring, longopts, &longindex);
#else
    c = getopt(argc, argv, optstring);
#endif

    if (c == EOF)
      break;
    option_count++;
    last_option_didnt_have_optional_argument = FALSE;
    remaining = argc - optind;
    last_opt = c;    
    switch (c) {
      case 'a':
	always_readline = TRUE;
	if (check_optarg('a', remaining))
	  password_prompt_search_string = mysavestring(optarg);
	break;
      case 'A':
	ansi_colour_aware = TRUE;
	break;
      case 'b':
	rl_basic_word_break_characters = add3strings("\r\n \t", optarg, "");
	opt_b = TRUE;
	break;
      case 'B':
	bleach_output = TRUE;
	break;
      case 'c':
	complete_filenames = TRUE;
	break;
      case 'C':
	opt_C = mysavestring(optarg);	
	break;
      case 'd':
#ifdef DEBUG
	if (option_count > 1)
	  myerror("-d or --debug option has to be the *first* rlwrap option");
	if (check_optarg('d', remaining))
	  debug = atoi(optarg);
	else
	  debug = DEBUG_ALL;
#else
	myerror
	  ("To use -d( for debugging), configure %s with --enable-debug and rebuild",
	   program_name);
#endif
	break;

      case 'D': 
        history_duplicate_avoidance_policy=atoi(optarg);
	if (history_duplicate_avoidance_policy < 0 || history_duplicate_avoidance_policy > 2)
	  myerror("%s option with illegal value %d, should be 0, 1 or 2",
		  current_option('D', longindex), history_duplicate_avoidance_policy);
	break;
      case 'f':
	feed_file_into_completion_list(optarg);
	opt_f = TRUE;
	break;
      case 'F':
        history_format = mysavestring(optarg);
	if (isspace(history_format[0]))
	  myerror("%s option argument should start with non-space", current_option('F', longindex));
	if(history_format[0] == '%' && history_format[1] != ' ')
	  myerror("if %s option argument starts with '%%', is should start with '%% ' i.e. '%%<space>'", current_option('F', longindex));
	break;
      case 'h':
	usage(EXIT_SUCCESS);		/* will call exit() */
      case 'H':
	history_filename = mysavestring(optarg);
	break;
      case 'i':
	if (opt_f)
	  myerror("-i option has to precede -f options");
	completion_is_case_sensitive = FALSE;
	break;
      case 'l':
	open_logfile(optarg);
	break;
      case 'n':
	nowarn = TRUE;
	break;
      case 'm':
#ifndef HAVE_SYSTEM
	mywarn("the -m option doesn't work on this system");
#endif
	multiline_separator =
	  (check_optarg('m', remaining) ? mysavestring(optarg) : " \\ ");
	break;
      case 'p':
	coloured_prompt = TRUE;
	initialise_colour_codes(check_optarg('p', remaining) ? optarg : "1;31"); /* bold red on current background is the default */  
	break;
      case 'q':
        rl_basic_quote_characters = mysavestring(optarg);
        break;
      case 'P':
        pre_given = mysavestring(optarg);
	always_readline = TRUE; /* pre_given does not work well with transparent mode */
	one_shot_rlwrap = TRUE;
	break;
      case 'r':
	remember_for_completion = TRUE;
	break;
      case 's':
	histsize = atoi(optarg);
	if (histsize < 0) {
	  write_histfile = 0;
	  histsize = -histsize;
	}
	break;
      case 't':
	client_term_name=mysavestring(optarg);
	break;
#ifdef DEBUG
      case 'T':
	test_terminal();
        exit(EXIT_SUCCESS);
#endif
      case 'v':
	printf("rlwrap %s\n",  VERSION);
	exit(EXIT_SUCCESS);
      case '?':
	assert(optind > 0);
	myerror("unrecognised option %s\ntry '%s --help' for more information",
		argv[optind-1], full_program_name);
      case ':':
	assert(optind > 0);
	myerror
	  ("option %s requires an argument \ntry '%s --help' for more information",
	   argv[optind-1], full_program_name);
      default:
	usage(EXIT_FAILURE);
    }
  }

  if (!complete_filenames && !opt_b) {	/* use / and . as default breaking characters whenever we don't complete filenames */
    rl_basic_word_break_characters =
      add3strings(rl_basic_word_break_characters, "/.", "");
  }

  
  if (optind >= argc) /* rlwrap -a -b -c with no command specified */
    usage(EXIT_FAILURE);

  if (opt_C) {
    int countback = atoi(opt_C);	/* try whether -C option is numeric */

    if (countback > 0) {	/* e.g -C 1 or -C 12 */
      if (argc - countback < optind)	/* -C 66 */
	myerror("when using -C %d you need at least %d non-option arguments",
		countback, countback);
      else if (argv[argc - countback][0] == '-')	/* -C 2 perl -d blah.pl */
	myerror("the last argument minus %d appears to be an option!",
		countback);
      else {			/* -C 1 perl test.cgi */
	command_name = mysavestring(mybasename(argv[argc - countback]));

      }
    } else if (countback == 0) {	/* -C name1 name2 or -C 0 */
      if (opt_C[0] == '0' && opt_C[1] == '\0')	/* -C 0 */
	myerror("-C 0 makes no sense");
      else if (strlen(mybasename(opt_C)) != strlen(opt_C))	/* -C dir/name */
	myerror("-C option argument should not contain directory components");
      else if (opt_C[0] == '-')	/* -C -d  (?) */
	myerror("-C option needs argument");
      else			/* -C name */
	command_name = opt_C;
    } else {			/* -C -2 */
      myerror
	("-C option needs string or positive number as argument, perhaps you meant -C %d?",
	 -countback);
    }
  } else {			/* no -C option given, use command name */
    command_name = mysavestring(mybasename(argv[optind]));
  }
  assert(command_name != NULL);
  return command_name;
}


/*
 * Since version 0.24, rlwrap only writes to master_pty
 * asynchronously, keeping a queue of pending output. The readline
 * line handler calls put_in_output_queue(usder_input) , while
 * main_loop calls flush_output_queue() as long as there is something
 * in the queue.
 */

static char *output_queue; /* NULL when empty */

int
output_queue_is_nonempty()
{
  return (output_queue ? TRUE : FALSE);
}

void
put_in_output_queue(char *stuff)
{
  if (!output_queue)
    output_queue = mysavestring(stuff);           /* allocate queue (no need to free anything) */
  else {
    char *old_queue = output_queue;
    output_queue = add2strings(old_queue, stuff); /* allocate new queue */
    free(old_queue);                              /* free the old one   */
  }
  DPRINTF3(DEBUG_AD_HOC,"put %d bytes in output queue (which now has %d bytes) : %s",
	   (int) strlen(stuff), (int) strlen(output_queue), mangle_string_for_debug_log(stuff, 20));

}

void
flush_output_queue()
{
  int nwritten, queuelen, how_much;
  char *old_queue = output_queue;

  if (!output_queue)
    return;
  queuelen = strlen(output_queue);
  how_much = min(BUFFSIZE, queuelen); /* never write more than BUFFSIZE in one go @@@ is this never too much? */
  nwritten = write(master_pty_fd, output_queue, how_much);
  DPRINTF3(DEBUG_AD_HOC,"flushed %d bytes from output queue (%d remain) : %s",
	   nwritten, (int) strlen(output_queue) - nwritten,
	    mangle_string_for_debug_log(output_queue, min(0, nwritten)));
  if (nwritten < 0) {
    switch (nwritten) {
      case EINTR:
	return;
      case EAGAIN:
	return;
      default:
	myerror("write to master pty failed");
    }
  }

  if (!output_queue[nwritten]) /* nothing left in queue */
    output_queue = NULL;
  else
    output_queue = mysavestring(output_queue + nwritten);	/* this much is left to be written */

  free(old_queue);
}





void
cleanup_rlwrap_and_exit(int status)
{
  DPRINTF0(DEBUG_TERMIO, "Cleaning up");
  if (write_histfile && (histsize==0 ||  history_total_bytes() > 0)) /* avoid creating empty .speling_eror_history file after typo */
    write_history(history_filename);	/* ignore errors */
  close_logfile();
  if (terminal_settings_saved && (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_terminal_settings) < 0))	/* reset terminal */
    /* ignore (almost dead anyway) */ ;	 /* mywarn ("tcsetattr error on stdin"); */
                                         /* NOT myerror, as this would call cleanup_rlwrap_and_exit again*/

#ifdef WTERMSIG                 /* is there any system without this macro? */
  if (status != EXIT_SUCCESS)
    exit(status);               /* rlwrap itself has failed, rather than the wrapped command */
  else if (WIFSIGNALED(childs_exit_status)) 
    suicide_by(WTERMSIG(childs_exit_status)); /* child terminated by signal, make rlwrap's
						 parent believe rlwrap was killed by it */ 
  else
    exit(WEXITSTATUS(childs_exit_status)); /* propagate child's exit status */
#else
  exit(status != EXIT_SUCCESS ? status : WEXITSTATUS(childs_exit_status)); /* WEXITSTATUS has a fallback definition in rlwrap.h */
#endif
  
}
