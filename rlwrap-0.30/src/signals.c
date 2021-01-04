/*  signals.c: signal handling */

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

int child_is_dead = FALSE;
int childs_exit_status = 0;
int deferred_adapt_clients_winsize = FALSE; /* whether we have to adapt clients winsize when accepting a line */

static void change_signalmask(int, int *);
static RETSIGTYPE do_nothing(int);
static RETSIGTYPE child_died(int);
static RETSIGTYPE pass_on_signal(int);
static RETSIGTYPE handle_sigtstp(int);
static RETSIGTYPE handle_segfault(int);

int adapt_tty_winsize(int, int);
static void wipe_textarea(struct winsize *old_size);


static int signals_to_be_passed_on[] = { SIGHUP, SIGINT,
  SIGQUIT, SIGABRT, SIGTERM, SIGCONT, SIGUSR1,
  SIGUSR2, SIGWINCH, 0
};
#ifdef DEBUG
static char *signal_names[] = { "SIGHUP", "SIGINT",
  "SIGQUIT", "SIGABRT", "SIGTERM", "SIGCONT", "SIGUSR1",
  "SIGUSR2", "SIGWINCH"
};				/* the *names* are inherently non-portable: signal numbers may
				   be different on different systems */

static void log_named_signal(int);

#endif


int sigterm_received = FALSE;



void
install_signal_handlers()
{
  int i;
  signal(SIGCHLD, &child_died);
  signal(SIGTSTP, &handle_sigtstp);
#ifndef DEBUG
  signal(SIGSEGV, &handle_segfault);	/* we want core dumps when debugging, no polite excuses! */
#endif

  for (i = 0; signals_to_be_passed_on[i]; i++)
    signal(signals_to_be_passed_on[i], &pass_on_signal);
}


static void uninstall_signal_handlers() {
  int i;
  signal(SIGCHLD, SIG_DFL); 
  signal(SIGTSTP, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);	
  for (i = 0; signals_to_be_passed_on[i]; i++)
    signal(signals_to_be_passed_on[i], SIG_DFL);
}


void
ignore_sigchld()
{
  signal(SIGCHLD, &do_nothing);
}


/* we'll call signals whose handlers mess with readline's internal state "messy signals"
   at present SIGTSTP and SIGWINCH are considered "messy". The following 2 functions are used
   in main.c to block messy signals except when waiting for I/O */

void
block_signals(int *sigs)
{
  change_signalmask(SIG_BLOCK, sigs);
}

void
unblock_signals(int *sigs)
{
  change_signalmask(SIG_UNBLOCK, sigs);
}


static void
change_signalmask(int how, int *sigs)
{				/* sigs should point to a *zero-terminated* list of signals */
  int i;
  sigset_t mask;

  sigemptyset(&mask);
  for (i = 0; sigs[i]; i++)
    sigaddset(&mask, sigs[i]);
  sigprocmask(how, &mask, NULL);
}



static RETSIGTYPE
handle_sigtstp(int signo)
{
  sigset_t mask;
  int saved_errno, error;

  DPRINTF0(DEBUG_SIGNALS, "got SIGTSTP");

  if (child_pid && (error = kill(-child_pid, SIGTSTP))) {
    myerror("Failed to deliver signal");
  }

  if (within_line_edit)
    save_rl_state();

  /* unblock SIGTSTP, before we can send it to ourself (it is blocked while we're handling it) */

  sigemptyset(&mask);
  sigaddset(&mask, SIGTSTP);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);
  signal(SIGTSTP, SIG_DFL);	/* reset disposition to default (i.e. suspend) */
  /* suspend myself (and supbprocess, which is in the same process group) */
  DPRINTF0(DEBUG_SIGNALS, "sending ourselves a SIGTSTP");
  kill(getpid(), SIGTSTP);

  /* keyboard gathers dust, kingdoms crumble,.... */

  /* Beautiful princess types "fg", (or her father tries to kill us...) and we wake up HERE: */

  DPRINTF0(DEBUG_SIGNALS, "woken up");
  saved_errno = errno;

  signal(signo, &handle_sigtstp);	/* re-install handler (old-style) */
  errno = saved_errno;
  /* we should really block SIGWINCH here ... */
  if (within_line_edit) {
    restore_rl_state();
    /* this is why we call SIGTSTP a "messy" signal */
  } else {
    set_echo(FALSE);
    cr();
    if (slave_is_in_raw_mode() && !always_readline)
      return;
    my_putstr(saved_rl_state.prompt);
  }
  adapt_tty_winsize(STDIN_FILENO, master_pty_fd);	/* just in case */

}



/* signal handler, passes the signal to the child process group
   SIGWINCH is only passed after calling adapt_tty_winsize()
*/

static RETSIGTYPE
pass_on_signal(int signo)
{
  int ret, saved_errno = errno, pass_it_on = TRUE;

#ifdef DEBUG
  log_named_signal(signo);
#endif

  switch (signo) {
    case SIGWINCH:
      /* Make slave pty's winsize equal to that of STDIN. Pass the signal on *only if*
         winsize has changed. This is particularly important because we have the slave pty
         still open - when we pass on the signal the child will probably do a TIOCSWINSZ ioctl()
         on the slave pty  - causing us (the parent) to get a SIGWINCH again, thus getting stuck in a loop */
      pass_it_on = adapt_tty_winsize(STDIN_FILENO, master_pty_fd);
      break;
    case SIGTERM:
      sigterm_received = TRUE;
      break;
    default:
      break;
  }
  if (!child_pid)
    pass_it_on = FALSE;		/* no child (yet) to pass it on to */
  if (pass_it_on) {		/* we resend the signal to the proces *group* of the child */
    ret = kill(-child_pid, signo);
    DPRINTF3(DEBUG_SIGNALS, "kill(%d,%d) = %d", -child_pid, signo, ret);
    signal(signo, &pass_on_signal);	/* re-install handler (old-style) */
    if (within_line_edit)
      leave_prompt_alone = TRUE;          /* signals to interactive programs are usually sent from the terminal, and then it is
					   natural to leave a coloured prompt alone. */
  }
  errno = saved_errno;
}



/* This function handles a termnal resize by copying from_fd's winsize
   to that of to_fd, keeping displayed line tidy, if possible
   (i.e. if terminal is capable enough, and we know enough about
   readlines internals).  A return value != 0 means that the size has
   changed.
*/

int
adapt_tty_winsize(int from_fd, int to_fd)
{
  int ret;

  struct winsize old_winsize = window_size;

  ret = ioctl(from_fd, TIOCGWINSZ, &window_size);
  DPRINTF1(DEBUG_SIGNALS, "ioctl (..., TIOCGWINSZ) = %d", ret);
  if (window_size.ws_col != old_winsize.ws_col || window_size.ws_row != old_winsize.ws_row) {
    if (always_readline)                       /* if --always_readline option is set, the client will probably spew a */
      deferred_adapt_clients_winsize = TRUE;   /* volley of control chars at us when its terminal is resized. Hence we don't do it now */
    else {  
      ret = ioctl(to_fd, TIOCSWINSZ, &window_size); 
      DPRINTF1(DEBUG_SIGNALS, "ioctl (..., TIOCSWINSZ) = %d", ret);
    }	
    DPRINTF2(DEBUG_READLINE, "rl_prompt: %s, line_buffer: %s", mangle_string_for_debug_log(rl_prompt, 100), rl_line_buffer);
    rl_set_screen_size(window_size.ws_row, window_size.ws_col);	/* this is safe: we know that right now rlwrap is not calling
                                                                   the readline library because we keep SIGWINCH blocked all the time */

    if (!within_line_edit && (always_readline || !slave_is_in_raw_mode())) {
      wipe_textarea(&old_winsize);
      start_line_edit = TRUE;           /* we can't start line edit in signal handler, so we only set a flag */
    } else if (within_line_edit) {	/* try to keep displayed line tidy */
      wipe_textarea(&old_winsize);

      if (coloured_prompt) {
	saved_rl_state.coloured_prompt = colourise(saved_rl_state.prompt); /* result depends on terminal width */
	rl_set_prompt(mark_invisible(saved_rl_state.coloured_prompt));
      }
      rl_on_new_line();
      rl_forced_update_display();
    }
    return !always_readline; /* pass the signal on (except when always_readline is set) */
  } else {			/* window size has not changed */
    return FALSE;
  }
}

/* After a resize, clear all the lines that were occupied by prompt + line buffer before the resize */
static
void wipe_textarea(struct winsize *old_winsize)
{
  int point, lineheight, linelength, cursor_height, i, promptlength;
  if (!prompt_is_single_line()) {	/* Don't need to do anything in horizontal_scroll_mode  */
    promptlength = colourless_strlen(saved_rl_state.prompt, NULL);
    linelength = (within_line_edit ? strlen(rl_line_buffer) : 0) + promptlength;
    point = (within_line_edit ? rl_point : 0) + promptlength;
    lineheight = (linelength == 0 ? 0 : (1 + (max(point, (linelength - 1)) / old_winsize -> ws_col)));
    if (lineheight > 1 && term_cursor_up != NULL && term_cursor_down != NULL) {
      /* i.e. if we have multiple rows displayed, and we can clean them up first */
      cr(); 
      cursor_height = point / old_winsize -> ws_col;    /* cursor is still on old line */
      DPRINTF2(DEBUG_SIGNALS, "lineheight: %d, cursor_height: %d", lineheight, cursor_height);
      for (i = 1 + cursor_height; i < lineheight; i++)
	curs_down();	/* ...move it down to last line */
      for (i = lineheight; i > 1; i--) {	/* go up again, erasing every line */
	clear_line(); 
	curs_up();
      }
    }
    clear_line();
    cr();
  }
}	


static RETSIGTYPE
child_died(int unused)
{
  int saved_errno;
  pid_t dead_child_pid;

  saved_errno = errno;
  DPRINTF0(DEBUG_SIGNALS, "Caught SIGCHLD");
  while ((-1 == (dead_child_pid = waitpid(-1, &childs_exit_status, WNOHANG)))
	 && (errno == EINTR));
  if (child_pid != 0 && dead_child_pid != child_pid)	/* the dead child is not the rlwrapped command */
    return;			/* ignore */
  child_is_dead = TRUE;
  child_pid = 0;		/* thus we know that there is no child anymore to pass signals to */
  errno = saved_errno;

  return;			/* allow remaining output from child to be processed in main loop */
  /* (so that we will see childs good-bye talk)                     */
  /* this will then clean up and terminate                          */

}

static RETSIGTYPE
handle_segfault(int unused)
{				/* Even after sudden and unexpected death, leave the terminal in a tidy state */
  printf("\nrlwrap: Oops, segfault -  this should not have happened!\n"
	 "If you need a core dump, re-configure with --enable-debug and rebuild\n"
	 "Resetting terminal and cleaning up...\n");
  if (coloured_prompt)
    write(STDOUT_FILENO,"\033[0m",4); /* reset terminal colours */
  if (terminal_settings_saved)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_terminal_settings);
 
  exit(EXIT_FAILURE);
}


static RETSIGTYPE
do_nothing(int unused)
{
  ; /* yawn.... */
}


#ifdef DEBUG
static void
log_named_signal(int signo)
{
  if (debug) {
    int *p, i;

    for (i = 0, p = signals_to_be_passed_on; *p; p++, i++) {
      if (*p == signo) {
	DPRINTF2(DEBUG_SIGNALS, "got signal %d (%s on most systems)", signo,
		 signal_names[i]);
	break;
      }
    }
    if (!*p)
      DPRINTF1(DEBUG_SIGNALS, "got signal %d", signo);
  }
}
#endif


void suicide_by(int signal) {
  uninstall_signal_handlers();
  DPRINTF1(DEBUG_SIGNALS, "suicide by signal  %d", signal);
  kill(getpid(), signal);
  /* if still alive */
  exit(0);
}
