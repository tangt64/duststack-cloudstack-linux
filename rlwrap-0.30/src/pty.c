/*  pty.c: pty handling */

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



/*local vars */
static int always_echo = FALSE;

/* global var */
int slave_pty_fd;

pid_t
my_pty_fork(int *ptr_master_fd,
	    const struct termios *slave_termios,
	    const struct winsize *slave_winsize)
{
  int fdm, fds = -1;
  int ttyfd;
  pid_t pid;
  const char *slave_name;
  struct termios pterm;
  int only_sigchld[] = { SIGCHLD, 0 };


  ptytty_openpty(&fdm, &fds, &slave_name);

  slave_pty_fd = fds;

  block_signals(only_sigchld);	/* block SIGCHLD until we have had a chance to install a handler for it after the fork() */

  if ((pid = fork()) < 0) {
    myerror("Cannot fork");
    return(42); /* the compiler may not know that myeror() won't return */
  } else if (pid == 0) {		/* child */
    i_am_child = TRUE;		/* remember who I am */
    unblock_signals(only_sigchld);	/* we have to unblock SIGCHLD because a subsequent exec() will retain the signal mask */
    /* shouldn't we *restore* the signal mask? @@@ */
    close(fdm);			/* fdm not used in child */
    ptytty_control_tty(fds, slave_name);

    if (dup2(fds, STDIN_FILENO) != STDIN_FILENO)
      myerror("dup2 to stdin failed");
    if (isatty(STDOUT_FILENO) && dup2(fds, STDOUT_FILENO) != STDOUT_FILENO)
      myerror("dup2 to stdout failed");
    if (isatty(STDERR_FILENO) && dup2(fds, STDERR_FILENO) != STDERR_FILENO)
      myerror("dup2 to stderr failed");
    if (fds > STDERR_FILENO)
      close(fds);


    if (slave_termios != NULL)
      if (tcsetattr(STDIN_FILENO, TCSANOW, slave_termios) < 0)
	myerror("tcsetattr failed on slave pty");

    if (slave_winsize != NULL)
      if (ioctl(STDIN_FILENO, TIOCSWINSZ, slave_winsize) < 0)
	myerror("TIOCSWINSZ failed on slave pty");


    return (0);
  } else {			/* parent */
    install_signal_handlers();	/* by this time the child may be long dead! */
    unblock_signals(only_sigchld); /* ... and that's why we had blocked the signals before forking */

    *ptr_master_fd = fdm;
    if (!child_is_dead && tcgetattr(fdm, &pterm) < 0) {
      sleep(1);			/* we might be more succesful after the child command has
				   initialized its terminal. As there is no reliable way to sense this
				   from the parent, we just wait a little */
      if (tcgetattr(slave_pty_fd, &pterm) < 0) {
	fprintf(stderr,		/* don't use mywarn() because of the strerror() message *within* the text */
		"Warning: %s cannot determine terminal mode of %s\n"
		"(because: %s).\n"
		"Readline mode will always be on (as if -a option was set);\n"
		"passwords etc. *will* be echoed and saved in history list!\n\n",
		program_name, command_name, strerror(errno));
	always_echo = TRUE;
      }
    }
    if (!isatty(STDOUT_FILENO) || !isatty(STDERR_FILENO)) { 	/* stdout or stderr redirected? */
      ttyfd = open("/dev/tty", O_WRONLY);			/* open users terminal          */
      DPRINTF1(DEBUG_TERMIO, "stdout or stderr are not a terminal, onpening /dev/tty with fd=%d", ttyfd);	
      if (ttyfd <0)	
	myerror("Could not open /dev/tty");
      if (dup2(ttyfd, STDOUT_FILENO) != STDOUT_FILENO) 
	myerror("dup2 of stdout to ttyfd failed");  
      if (dup2(ttyfd, STDERR_FILENO) != STDERR_FILENO)
	myerror("dup2 of stderr to ttyfd failed");
      close (ttyfd);
    }	
    return (pid);
  }
}

static int been_warned = 0;


int
slave_is_in_raw_mode()
{
  struct termios pterm;

  if (always_echo)
    return FALSE;

  if (!child_is_dead && tcgetattr(slave_pty_fd, &pterm) < 0) {
    /* race condition here: SIGCHLD may not yet heve been caught */
    if (been_warned++ == 1)	/* only warn once, but not the first time (as this usually means that the rlwrapped command has just died) */
      mywarn("tcgetattr error on slave pty (from parent process)");
    return FALSE;
  } else {
    return (!(pterm.c_lflag & ICANON));
  }
}

void
mirror_slaves_echo_mode()
{				/* important e.g. when slave command asks for password  */
  struct termios *pterm_slave = NULL;
  int should_echo_anyway = always_echo || always_readline;

  if (!child_is_dead && !always_echo && !(pterm_slave = get_pterm_slave()))
    /* race condition here: SIGCHLD may not yet have been caught */
    return;


  /* if the --always-readline option is set with argument "assword:", determine whether prompt ends with "assword:\s" */
  if (should_echo_anyway && password_prompt_search_string) {
    char *p, *q;

    assert(strlen(saved_rl_state.prompt) < BUFFSIZE);
    p = saved_rl_state.prompt + strlen(saved_rl_state.prompt) - 1;
    q =
      password_prompt_search_string + strlen(password_prompt_search_string) -
      1;
    while (*p == ' ')		/* skip trailing spaces in prompt */
      p--;
    while (p >= saved_rl_state.prompt && q >= password_prompt_search_string)
      if (*p-- != *q--)
	break;

    if (q < password_prompt_search_string)	/* found "assword:" */
      should_echo_anyway = FALSE;
  }


  if (!child_is_dead && (should_echo_anyway || pterm_slave->c_lflag & ECHO)) {
    redisplay = TRUE;
  } else {
    redisplay = FALSE;
  }
  if (pterm_slave)
    free(pterm_slave);
  set_echo(redisplay);		/* This is a bit weird: we want echo off all the time, because readline takes care
				   of echoing, but as readline uses the current ECHO mode to determine whether
				   you want echo or not, we must set it even if we know that readline will switch it
				   off immediately   */
}


struct termios *
get_pterm_slave()
{
  struct termios *pterm_slave =
    (struct termios *)mymalloc(sizeof(struct termios));
  if (tcgetattr(slave_pty_fd, pterm_slave) < 0) {
    if (been_warned++ == 1)	/* only warn once (but not the very first time, cf slave_is_in_raw_mode() above) */
      mywarn("tcgetattr error on slave pty (from parent process)");
    return NULL;
  }
  return pterm_slave;
}


void
write_EOF_to_master_pty()
{
  struct termios *pterm_slave = get_pterm_slave();
  char *sent_EOF = mysavestring("?");

  *sent_EOF = pterm_slave->c_cc[VEOF]; /*@@@ HL shouldn't we directly mysavestring(pterm_slave->c_cc[VEOF]) ??*/
  put_in_output_queue(sent_EOF);
  free(pterm_slave);
  free(sent_EOF);
}

void
write_EOL_to_master_pty(char *received_eol)
{
  struct termios *pterm_slave = get_pterm_slave();
  char *sent_eol = mysavestring("?");

  *sent_eol = *received_eol;
  switch (*received_eol) {
    case '\n':
      if (pterm_slave->c_iflag & INLCR)
	*sent_eol = '\r';
      break;
    case '\r':
      if (pterm_slave->c_iflag & IGNCR)
	return;
      if (pterm_slave->c_iflag & ICRNL)
	*sent_eol = '\n';
  }
  put_in_output_queue(sent_eol);
  free(pterm_slave);
  free(sent_eol);
}

void
completely_mirror_slaves_terminal_settings()
{
  struct termios *pterm_slave = get_pterm_slave();
  log_terminal_settings(pterm_slave);
  if (tcsetattr(STDIN_FILENO, TCSANOW, pterm_slave) < 0 && errno != ENOTTY)
    ;	/* myerror ("cannot prepare terminal (tcsetattr error on stdin)"); */
  free(pterm_slave);
}

