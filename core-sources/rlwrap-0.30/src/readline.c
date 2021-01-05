/*  readline.c: interacting with the GNU readline library 
    (C) 2000-2007 Hans Lub
    
    This program is free software; you can redistribute it and/or modify
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

/* global vars */
int remember_for_completion = FALSE;	/* whether we should put al words from in/output on the list */
char *multiline_separator = NULL;	/* character sequence to use in lieu of newline when storing multi-line input in a single history line */
char *pre_given = NULL;                 /* pre-given user input when rlwrap starts up */
struct rl_state saved_rl_state = { "", "", 0, 0 };	/* saved state of readline */
static char return_key;	                /* Key pressed to enter line */
static int forget_line;
static char *colour_start, *colour_end;        /* colour codes */
int leave_prompt_alone = FALSE;
int multiline_prompts = TRUE;

/* forward declarations */
void line_handler(char *);
static void my_add_history(char *);
static int my_accept_line(int, int);
static int my_accept_line_and_forget(int, int);
static int dump_all_keybindings(int,int);
static int munge_line_in_editor(int, int);

void
init_readline(char *prompt)
{
  DPRINTF1(DEBUG_READLINE, "Initialising readline version %x", rl_readline_version);   
  rl_add_defun("rlwrap_accept_line", my_accept_line, -1);
  rl_bind_key('\n', my_accept_line);
  rl_bind_key('\r', my_accept_line); 

  rl_add_defun("insert_close", rl_insert_close, -1);
  rl_bind_key(')', rl_insert_close);
  rl_bind_key('}', rl_insert_close);
  rl_bind_key(']', rl_insert_close);

  rl_add_defun("rlwrap_accept_line_and_forget", my_accept_line_and_forget, -1);
  rl_bind_key(15, my_accept_line_and_forget);	/* ascii #15 (Control-O) is unused in readline's emacs and vi keymaps */

  rl_add_defun("rlwrap_dump_all_keybindings", dump_all_keybindings, -1);
  /* rl_bind_key(8, dump_all_keybindings); */
  
  if (multiline_separator != NULL) {
    rl_add_defun("rlwrap_call_editor", munge_line_in_editor, -1);
    rl_bind_key(30, munge_line_in_editor);	/* ascii #30 (Control-^) is unused in readline's emacs and vi keymaps */
  }
  /* rl_variable_bind("gnah","gnerp"); It is not possible to create new readline variables (only functions) */
  rl_initialize();		/* This has to happen *before* we set rl_redisplay_function, otherwise
				   readline will not bother to call tgetent(), will be agnostic about terminal
				   capabilities and hence not be able to honour e.g. a set horizontal-scroll-mode off
				   in .inputrc */
  /* printf("gnah = %s\n", rl_variable_value("gnah")); */ 
  using_history();
  rl_redisplay_function = my_redisplay;
  rl_already_prompted = 1;	/* we'll always write our own prompt */
  rl_completion_entry_function =
    (rl_compentry_func_t *) & my_completion_function;
  /* rl_getc_function = &my_rl_getc_function; */
  rl_catch_signals = FALSE;
  rl_catch_sigwinch = FALSE;
  if (pre_given) {
    saved_rl_state.text = mysavestring(pre_given); /* stuff per-given text into edit buffer */
    saved_rl_state.point =  strlen(pre_given);
    DPRINTF0(DEBUG_READLINE, "Starting line edit (because of -P option)");
    within_line_edit = TRUE;
    restore_rl_state();
  }
}


/* save readline internal stae in rl_state, redisplay the prompt
   (so that client output gets printed at the right place) */
void
save_rl_state()
{
  free(saved_rl_state.text); /* free(saved_rl_state.prompt) */;
  saved_rl_state.text = mysavestring(rl_line_buffer);
  /* saved_rl_state.prompt = mysavestring(rl_prompt); */
 
  saved_rl_state.point = rl_point;	/* and point    */
  rl_line_buffer[0] = '\0';
  if (saved_rl_state.already_saved)
    return;
  saved_rl_state.already_saved = 1;
  rl_delete_text(0, rl_end);	/* clear line  (after prompt) */
  rl_point = 0;
  my_redisplay();		/* and redisplay */
  rl_callback_handler_remove();	/* restore old term settings */
  rl_deprep_terminal();

}


/* Restore readline internal state from rl_state. As setting
   rl_already_prompted doesn't reliably prevent readline from
   reprinting the prompt,  we have to move to the start of
   the prompt and let readline do it anyway.  NB: this routine expects
   the prompt to have been printed already. */

void
restore_rl_state()
{
  
  char *newprompt;
  char *raw_prompt = (coloured_prompt ? saved_rl_state.coloured_prompt : saved_rl_state.prompt);  
  newprompt =  (ansi_colour_aware || coloured_prompt ? mark_invisible(raw_prompt) :  mysavestring(raw_prompt));
  rl_expand_prompt(newprompt);	/* We have to call this ourselves because rl_already_prompted is set */

  /* if (coloured_prompt && urxvt_hack) curs_down(); */ 
 
  mirror_slaves_echo_mode();	/* don't show passwords etc */
  move_cursor_to_start_of_prompt();
  DPRINTF1(DEBUG_READLINE,"newprompt now %s", mangle_string_for_debug_log(newprompt,MANGLE_LENGTH));
  rl_callback_handler_install(newprompt, &line_handler); /* this puts the cursor one line up on
  							    urxvt, if the prompt contains colour. strange....) */

  free(newprompt);             /* readline docs don't say it, but we can free newprompt now (readline apparently
				  uses its own copy) */
  rl_insert_text(saved_rl_state.text);
  rl_point = saved_rl_state.point;
  saved_rl_state.already_saved = 0;
  cr();
  rl_forced_update_display();
  rl_prep_terminal(1);
  
}

void
line_handler(char *line)
{
  char *rewritten_line; char *prompt;
  int i;
  
  if (line == NULL) {		/* EOF on input, forward it  */
    DPRINTF1(DEBUG_READLINE, "EOF detected, writing character %d", term_eof);
    /* coloured_prompt = FALSE; don't mess with the cruft that may come out of dying command @@@ but command may not die!*/
    write_EOF_to_master_pty();
  } else {
    if (*line && redisplay && !forget_line) {	/* forget empty lines, passwords, or lines entered with CTRL-O */
      my_add_history(line);
    }
    forget_line = 0; /* until CTRL-O is used again */

    /* Time for a design decision: when we send the accepted line to
       the client command, it will most probably be echoed back. We
       have two choices: we leave the entered line on screen and
       suppress just enough of the clients output (I believe this is
       what rlfe does), or we remove the entered input and let it be
       replaced by the echo.

       This is what we do; if the program doesn't echo, the **** which
       has been put there by our homegrown_redisplay function won't be
       touched.

       I think this method is more natural for multi-line input as well,
       but not everyone will agree with that.
       
       The only complication is that vi-mode will put us on the next
       line when line_handler is called (is this documented
       somewhere?) The client-suppression method won't have any
       difficulty with this, but we will have to move the cursor back
       up. Not all terminals can do this (or you may be telnetting to an
       old computer with a whizzbang terminal program that the oldie doesn't
       have in its terminal database).       
    */


    
    /* emacs-mode has the cursor 1 past end of prompt (at start of
       line).  vi-mode has it on start of new line (Is all of this
       documented somewhere?)  when in vi-mode, we put the cursor back
       at the same place it would have been in emacs-mode: */
       
    if (rl_editing_mode == 0) {  
      char *prompt_plus_line = add2strings(rl_prompt,line);
      int lines_below_start_of_prompt =
	1 +  (prompt_is_single_line() ?
	      0 :
	      colourless_strlen(prompt_plus_line, NULL) / window_size.ws_col);
      free(prompt_plus_line);
      for (i=0; i < lines_below_start_of_prompt; i++)
	curs_up();
      my_putstr(coloured_prompt ? saved_rl_state.coloured_prompt : saved_rl_state.prompt);
    }
    
    /* O.K, we now know for sure that cursor is at start of line. When
       clients output arrives, it will be printed at just the righ
       place */

    /* SHOWCURSOR; */
    
    rewritten_line =
      (multiline_separator ? 
       search_and_replace(multiline_separator, "\n", line, 0, NULL,
			  NULL) : mysavestring(line));

    /* do we have to adapt clients winzsize now? */
    if (deferred_adapt_clients_winsize) {
      adapt_tty_winsize(STDIN_FILENO, master_pty_fd);
      kill(-child_pid, SIGWINCH);
      deferred_adapt_clients_winsize = FALSE;
    }

    /*OK, now feed line to underlying command and wait for the echo to come back */
    put_in_output_queue(rewritten_line);
    DPRINTF2(DEBUG_READLINE, "writing %d bytes %s", (int) strlen(rewritten_line),
	     mangle_string_for_debug_log(rewritten_line, 40));
    write_EOL_to_master_pty(return_key ? &return_key : "\n");
    /* if (one_shot_rlwrap)
       write_EOF_to_master_pty(); */
    free_foreign(line);         /* free_foreign because line was malloc'ed by readline, not by rlwrap */
    free(rewritten_line);	/* we're done with them  */

    return_key = 0;
    within_line_edit = FALSE;

    rl_callback_handler_remove();
    set_echo(FALSE);
    free(saved_rl_state.text);
    free(saved_rl_state.prompt);
    free(saved_rl_state.coloured_prompt); 
    
    saved_rl_state.text = mysavestring("");
    saved_rl_state.prompt = mysavestring("");
    saved_rl_state.coloured_prompt =  mysavestring("");
    saved_rl_state.point = 0;
    saved_rl_state.already_saved = TRUE;
    leave_prompt_alone = TRUE;
  }
}


/* this function (drop-in replacement for readline's own accept-line())
   will be bound to RETURN key: */
static int
my_accept_line(int count, int key)
{
  rl_point = 0;			/* leave cursor on predictable place */
  my_redisplay();
  rl_done = 1;
  return_key = (char)key;
  return 0;
}

/* this function will be bound to rl_accept_key_and_forget key (normally CTRL-O) */
static int
my_accept_line_and_forget(int count, int key)
{
  forget_line = 1;
  return my_accept_line(count, '\n');
}


static int
dump_all_keybindings(int count, int key)
{
  rl_dump_functions(count,key);
  rl_variable_dumper(FALSE);
  rl_macro_dumper(FALSE);
  return 0;
}	

/* format line and add it to history list, avoiding duplicates if necessary */
static void
my_add_history(char *line)
{	
  int lookback, count, here;
  char *new_entry;

  
  switch (history_duplicate_avoidance_policy) { 
  case KEEP_ALL_DOUBLES:
    lookback = 0; break;
  case ELIMINATE_SUCCESIVE_DOUBLES:
    lookback = 1; break;
  case ELIMINATE_ALL_DOUBLES:
    lookback = history_length; break;
  }

  if (!history_format)  /* enter it as-is */
    new_entry = mysavestring(line);	
  else 
    new_entry = append_and_expand_history_format(line);

   
  lookback = min(history_length, lookback);      
  for (count = 0, here = history_length + history_base - 1;
       count < lookback ;
       count++, here--) {
    /* DPRINTF3(DEBUG_READLINE, "strcmping <%s> and <%s> (count = %d)", line, history_get(here)->line ,count); */
    if (strncmp(new_entry, history_get(here) -> line, 10000) == 0) {
      HIST_ENTRY *entry = remove_history (here - history_base); /* according to the history library doc this should be
								   remove_history(here) @@@?*/
      DPRINTF2(DEBUG_READLINE, "removing duplicate entry #%d (%s)", here, entry->line);
      free_foreign(entry->line);
      free_foreign(entry);
    }
  }
  add_history(new_entry);
  free(new_entry);
}









/* Homegrown redisplay function - erases current line and prints the
   new one.  Used for passwords (where we want to show **** instead of
   user input) and whenever HOMEGROWN_REDISPLAY is defined (for
   systems where rl_redisplay() misbehaves, like sometimes on
   Solaris). Otherwise we use the much faster and smoother
   rl_redisplay() This function cannot display multiple lines: it will
   only scroll horizontally (even if horizontal-scroll-mode is off in
   .inputrc)
*/


static void
my_homegrown_redisplay(int hide_passwords)
{
  

  static int line_start = 0;	/* at which position of prompt_plus_line does the printed line start? */
  static int line_extends_right = 0;
  static int line_extends_left = 0;
  static char *previous_line = NULL;
  
  
  int width = window_size.ws_col;
  int skip = max(1, min(width / 5, 10));	/* jumpscroll this many positions when cursor reaches edge of terminal */
  
  char *prompt_without_ignore_markers;
  int colourless_promptlen = colourless_strlen(rl_prompt, &prompt_without_ignore_markers);
  int promptlen = strlen(prompt_without_ignore_markers);
  int invisible_chars_in_prompt = promptlen - colourless_promptlen;
  char *prompt_plus_line = add2strings(prompt_without_ignore_markers, rl_line_buffer);
  char *new_line;
  int total_length = strlen(prompt_plus_line);
  int curpos = promptlen + rl_point; /* cursor position within prompt_plus_line */
  int i, printed_length,
      new_curpos,                    /* cursor position on screen */
    keep_old_line, vlinestart, printwidth, last_column;


  /* In order to handle prompt with colour we either print the whole prompt, or start past it:
     starting in the middle is too difficult (i.e. I am too lazy) to get it right.
     We use a "virtual line start" vlinestart, which is the number of invisible chars in prompt in the former case, or
     linestart in the latter (which then must be >= strlen(prompt))

     At all times (before redisplay and after) the following is true:
     - the cursor is at column (curpos - vlinestart) (may be < 0 or > width)
     - the character under the cursor is prompt_plus_line[curpos]
     - the character at column 0 is prompt_plus_line[linestart]
     - the last column is at <number of printed visible or invisible chars> - vlinestart
     
     the goal of this function is to display (part of) prompt_plus_line such
     that the cursor is visible again */
     
  
  if (hide_passwords)
    for (i = promptlen; i < total_length; i++)
      prompt_plus_line[i] = '*';	/* hide a pasword by making user input unreadable  */


  if (rl_point == 0)		/* (re)set  at program start and after accept_line (where rl_point is zeroed) */
    line_start = 0;
  assert(line_start == 0 || line_start >= promptlen); /* the line *never* starts in the middle of the prompt (too complicated to handle)*/
  vlinestart = (line_start > promptlen ? line_start : invisible_chars_in_prompt); 
  

  if (curpos - vlinestart > width - line_extends_right)	/* cursor falls off right edge ?   */
    vlinestart = (curpos - width + line_extends_right) + skip;	/* jumpscroll left                 */

  else if (curpos < vlinestart + line_extends_left) {	/* cursor falls off left edge ?    */
    if (curpos == total_length)	/* .. but still at end of line?    */
      vlinestart = max(0, total_length - width);	/* .. try to display entire line   */
    else			/* in not at end of line ..        */
      vlinestart = curpos - line_extends_left - skip; /* ... jumpscroll right ..         */
  }	
  if (vlinestart <= invisible_chars_in_prompt) {
    line_start = 0;		/* ... but not past start of line! */
    vlinestart = invisible_chars_in_prompt;
  } else if (vlinestart > invisible_chars_in_prompt && vlinestart <= promptlen) {
    line_start = vlinestart = promptlen;
  } else {
    line_start = vlinestart;
  }

  printwidth = (line_start > 0 ? width : width + invisible_chars_in_prompt);
  printed_length = min(printwidth, total_length - line_start);	/* never print more than width     */
  last_column = printed_length - vlinestart;


  /* some invariants :     0 <= line_start <= curpos <= line_start + printed_length <= total_length */
  /* these are interesting:   ^                                                      ^              */

  assert(0 <= line_start);
  assert(line_start <= curpos);
  assert(curpos <= line_start + printed_length);	/* <=, rather than <, as cursor may be past eol   */
  assert(line_start + printed_length <= total_length);


  new_line = prompt_plus_line + line_start;
  new_line[printed_length] = '\0';
  new_curpos = curpos - vlinestart;

  /* indicate whether line extends past right or left edge  (i.e. whether the "interesting
     inequalities marked ^ above are really unequal) */

  line_extends_left = (line_start > 0 ? 1 : 0);
  line_extends_right = (total_length - vlinestart > width ? 1 : 0);
  if (line_extends_left)
    new_line[0] = '<';
  if (line_extends_right)
    new_line[printwidth - 1] = '>';



  keep_old_line = FALSE;
  if (term_cursor_hpos) {
    if (previous_line && strcmp(new_line, previous_line) == 0) {
      keep_old_line = TRUE;
    } else {
      if (previous_line)
	free(previous_line);
      previous_line = mysavestring(new_line);
    }
  }


  if (!keep_old_line) {
    clear_line();
    cr();
    write(STDOUT_FILENO, new_line, printed_length);
  }
  
  assert(term_cursor_hpos || !keep_old_line);	/* if we cannot position cursor, we must have reprinted ... */

  if (term_cursor_hpos)
    cursor_hpos(new_curpos);
  else				/* ... so we know we're 1 past last position on line */
    backspace(last_column - new_curpos);
  free(prompt_plus_line);
  free(prompt_without_ignore_markers);
}




void
my_redisplay()
{
  int debug_force_homegrown_redisplay = 0;

#ifdef DEBUG
  debug_force_homegrown_redisplay = debug & FORCE_HOMEGROWN_REDISPLAY;
#endif
  
#ifndef HOMEGROWN_REDISPLAY
  if (redisplay && !debug_force_homegrown_redisplay) {
    rl_redisplay();
  } else
#endif
    my_homegrown_redisplay(!redisplay);
}


/* allocate and init array of 4 strings (helper for munge_line_in_editor() */
static char **
list4 (char *el0, char *el1, char *el2, char *el3)
{
  char **list = mymalloc(4*sizeof(char*));
  list[0] = el0;
  list[1] = el1;
  list[2] = el2;
  list[3] = el3;
  return list;
}



static int
munge_line_in_editor(int count, int key)
{
  int line_number = 0, column_number = 0, tmpfile_OK, ret, tmpfile_fd, bytes_read;
  size_t tmpfilesize;
  char *p, *tmpdir, *tmpfilename, *text_to_edit;
  char *editor_command1, *editor_command2, *editor_command3, *editor_command4,
    *line_number_as_string, *column_number_as_string;
  char *input, *rewritten_input, *rewritten_input2, **possible_tmpdirs, **possible_editor_commands;
  
#ifndef HAVE_SYSTEM
  return 0;    /* we could write our own system() using execv(), but we are too lazy to do that */
#else

  possible_tmpdirs = list4(getenv("TMPDIR"), getenv("TMP"), getenv("TEMP"), "/tmp");
  possible_editor_commands = list4(getenv("RLWRAP_EDITOR"), getenv("EDITOR"), getenv("VISUAL"), "vi +%L");

  /* create temporary filename */
#ifdef HAVE_MKSTEMP
  tmpdir = first_of(possible_tmpdirs);
  tmpfilename = add3strings(tmpdir, "/rlwrap_", "XXXXXX");
  tmpfile_OK = mkstemp(tmpfilename);
#else
  tmpfilename = mymalloc(L_tmpnam);
  tmpfile_OK = (int)tmpnam(tmpfilename); /* manpage says: Never use this function. Use mkstemp(3) instead */
#endif
  if (!tmpfile_OK)
    myerror("could not find unique temporary file name");

  /* write current input to it, replacing the newline substitute (multiline_separator) with the real thing */
  tmpfile_fd = open(tmpfilename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
  if (tmpfile_fd < 0)
    myerror("could not create temporary file %s", tmpfilename);
  text_to_edit =
    search_and_replace(multiline_separator, "\n", rl_line_buffer, rl_point,
		       &line_number, &column_number);
  if (write(tmpfile_fd, text_to_edit, strlen(text_to_edit)) < 0)
    myerror("could not write to temporary file %s", tmpfilename);

  if (close(tmpfile_fd) != 0) /* improbable */
    myerror("couldn't close temporary file %s", tmpfilename); 

  /* find out which editor command we have to use */

  editor_command1 = first_of(possible_editor_commands);
  line_number_as_string = as_string(line_number);
  column_number_as_string = as_string(column_number);
  editor_command2 =
    search_and_replace("%L", line_number_as_string, editor_command1, 0, NULL,
		       NULL);
  editor_command3 =
    search_and_replace("%C", column_number_as_string, editor_command2, 0,
		       NULL, NULL);
  editor_command4 = add3strings(editor_command3, " ", tmpfilename);

  /* call editor, temporarily restoring terminal settings */
  clear_line();
  cr();
  if (terminal_settings_saved && (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_terminal_settings) < 0))	/* reset terminal */
    myerror("tcsetattr error on stdin");
  DPRINTF1(DEBUG_READLINE, "calling %s", editor_command4);
  if ((ret = system(editor_command4))) {
    if (WIFSIGNALED(ret)) {
      fprintf(stderr, "\n"); errno = 0;
      myerror("editor killed by signal");
    } else {    
      myerror("failed to invoke editor with '%s'", editor_command4);
    }
  }
  completely_mirror_slaves_terminal_settings();
  ignore_queued_input = TRUE;  

  /* read back edited input, replacing real newline with substitute */
  tmpfile_fd = open(tmpfilename, O_RDONLY);
  if (tmpfile_fd < 0)
    myerror("could not read temp file %s", tmpfilename);
  tmpfilesize = filesize(tmpfilename);
  input = mymalloc(tmpfilesize + 1);
  bytes_read = read(tmpfile_fd, input, tmpfilesize);
  if (bytes_read < 0)
    myerror("unreadable temp file %s", tmpfilename);
  input[bytes_read] = '\0';
  rewritten_input = search_and_replace("\t", "    ", input, 0, NULL, NULL);	/* rlwrap cannot handle tabs in input lines */
  rewritten_input2 =
    search_and_replace("\n", multiline_separator, rewritten_input, 0, NULL,
		       NULL);
  for(p = rewritten_input2; *p ;p++)
    if(*p < ' ')
      *p = ' ';        /* replace all control characters (like \r) by spaces */
  
  rl_delete_text(0, strlen(rl_line_buffer));
  rl_point = 0;
  rl_insert_text(rewritten_input2);
  rl_point = 0;			/* leave cursor on predictable place */
  rl_done = 1;
  return_key = (char)'\n';

  

  /* wash those dishes */
  if (unlink(tmpfilename))
    myerror("could not delete temporary file %s", tmpfilename);
  free(editor_command2);
  free(editor_command3);
  free(editor_command4);
  free(line_number_as_string);
  free(column_number_as_string);
  free(tmpfilename);
  free(text_to_edit);
  free(input);
  free(rewritten_input);
  free(rewritten_input2);

  return 0;
#endif /* HAVE_SYSTEM */
}



void
initialise_colour_codes(char *colour)
{
  int attributes, foreground, background;
  attributes = foreground = -1;
  background = 40; /* don't need to specify background; 40 passes the test automatically */
  sscanf(colour, "%d;%d;%d", &attributes, &foreground, &background);
  
#define OUTSIDE(lo,hi,val) (val < lo || val > hi) 
  if (OUTSIDE(0,8,attributes) || OUTSIDE(30,37,foreground) || OUTSIDE(40,47,background))
    myerror("\n"
	    "  prompt colour spec should be <attr>;<fg>[;<bg>]\n"
	    "  where <attr> ranges over [0...8], <fg> over [30...37] and <bg> over [40...47]\n"
            "  example: 0;33 for yellow on current background, 1;31;40 for bold red on black ");
  colour_start= add3strings("\033[", colour,"m");
  colour_end  = "\033[0m";
}

/* returns a colourised copy of prompt, trailing spcae is not colourised */
char*
colourise (const char *prompt)
{
  char *prompt_copy, *trailing_space, *colour_end_with_space, *result, *p;
  prompt_copy = mysavestring(prompt);
  if (strlen(prompt_copy) + strlen(colour_start) + strlen(colour_end) + 4 >= window_size.ws_col || /* prompt too long? */
      strchr(prompt_copy, '\033')) {     /* prompt contains escape codes? */
    DPRINTF1(DEBUG_READLINE, "colourise %s: left as-is", prompt);
    return prompt_copy; /* if so, leave prompt alone  */
  }
  for (p = prompt_copy + strlen(prompt_copy); p > prompt_copy && *(p-1) == ' '; p--)
    ; /* skip back over trailing space */
  trailing_space = mysavestring(p); /* p now points at first trailing space, or else the final NULL */
  *p = '\0';
  colour_end_with_space = add2strings(colour_end, trailing_space);
  result = add3strings(colour_start, prompt_copy, colour_end_with_space);
  free (prompt_copy); free(trailing_space); free(colour_end_with_space);
  DPRINTF1(DEBUG_READLINE, "colourise %s: colour added ", prompt);
  return result;
}

/* if coloured == TRUE, compute the coloured prompt from saved_rl_state.prompt, put it in
    saved_rl_state.coloured_prompt and print it (but only if there is a prompt)
   if coloured == FALSE, reprint the uncoloured prompt and free the coloured prompt
 */
void
reprint_prompt(int coloured)
{
  DPRINTF1(DEBUG_TERMIO,"reprint_prompt(%s)",(coloured ? "TRUE":"FALSE"));
  if(leave_prompt_alone  || (!always_readline && slave_is_in_raw_mode())) {
    DPRINTF1(DEBUG_TERMIO, "returning without doing anything (leave_prompt_alone=%s)", (leave_prompt_alone ? "TRUE" : "FALSE"));
    return;
  }
  
  if (coloured) {
    if (saved_rl_state.coloured_prompt && *saved_rl_state.coloured_prompt) {
      move_cursor_to_start_of_prompt();
      my_putstr(saved_rl_state.coloured_prompt);
    }	
  } else { /*uncoloured */
    if (saved_rl_state.prompt && *saved_rl_state.prompt) { 
      move_cursor_to_start_of_prompt();
      my_putstr(saved_rl_state.prompt);
    }	
    if(saved_rl_state.coloured_prompt) {
      free(saved_rl_state.coloured_prompt);
      saved_rl_state.coloured_prompt = NULL;
    }
   
  }	
}	


void
move_cursor_to_start_of_prompt()
{
  char *marked_prompt;
  int termwidth = window_size.ws_col;
  int promptlen_on_screen, number_of_lines_in_prompt, curpos, count;

  promptlen_on_screen =  colourless_strlen_unmarked(coloured_prompt ? saved_rl_state.coloured_prompt : saved_rl_state.prompt);
  curpos = (within_line_edit ? 1 : 0); /* if the user has pressed a key the cursor will be 1 past the current prompt */ 
  number_of_lines_in_prompt = 1 +  ((promptlen_on_screen + curpos -1) / termwidth); /* integer arithmetic! (e.g. 171/80 = 2) */
  cr(); 
  for (count = 0; count < number_of_lines_in_prompt -1; count++)
    curs_up();
  DPRINTF3(DEBUG_READLINE,"moved cursor up %d lines (because len=%d, termwidth=%d)",
	   number_of_lines_in_prompt - 1, promptlen_on_screen, termwidth); 
}	


int
prompt_is_single_line(void)
{
  int homegrown_redisplay= FALSE;
  int force_homegrown_redisplay = FALSE;
  int retval;  
  
#ifndef SPY_ON_READLINE
#  define _rl_horizontal_scroll_mode FALSE
#  define rl_variable_value(s) "off"
#else
#  ifndef HAVE_RL_VARIABLE_VALUE
#    define rl_variable_value(s) "off"
#  endif
#endif
  
#ifdef HOMEGROWN_REDISPLAY
  homegrown_redisplay=TRUE;
#endif

#ifdef DEBUG
  force_homegrown_redisplay =  debug & FORCE_HOMEGROWN_REDISPLAY;
#endif

  retval = _rl_horizontal_scroll_mode ||
    strncmp(rl_variable_value("horizontal-scroll-mode"),"on",3) == 0 ||
    homegrown_redisplay ||
    force_homegrown_redisplay;
 
  DPRINTF1(DEBUG_READLINE, "prompt is %s-line", (retval ? "single" : "multi"));
  return retval;
}
