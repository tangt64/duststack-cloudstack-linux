/* rlwrap - a readline wrapper
   (C) 2000-2007 Hans Lub

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,


   **************************************************************

   string_utils.c: rlwrap uses a fair number of custom string-handling
   functions. A few of those are replacements for (or default to)
   GNU or POSIX standard funcions (with names like mydirname,
   mystrldup). Others are special purpose string manglers for
   debugging, removing colour codes and the construction of history
   entries)

   All of these functions work on basic types as char * and int, and
   none of them refer to any of rlwraps global variables (except debug)
*/ 

     
#include "rlwrap.h"




/* mystrlcpy and mystrlcat: wrappers around strlcat and strlcpy, if
   available, otherwise emulations of them. Both versions *assure*
   0-termination, but don't check for truncation: return type is
   void */

void
mystrlcpy(char *dst, const char *src, size_t size)
{
#ifdef HAVE_STRLCPY
  strlcpy(dst, src, size);
#else
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
#endif
}

void
mystrlcat(char *dst, const char *src, size_t size)
{
#ifdef HAVE_STRLCAT
  strlcat(dst, src, size);
  dst[size - 1] = '\0';		/* we don't check for truncation, just assure '\0'-termination. */
#else
  strncat(dst, src, size - strnlen(dst, size) - 1);
  dst[size - 1] = '\0';
#endif
}



/* mystrndup: strndup replacement that uses the safer mymalloc instead
   of malloc*/

static char *
mystrndup(const char *string, int len)
{
  /* allocate copy of string on the heap */
  char *buf;

  buf = (char *)mymalloc(len + 1);
  mystrlcpy(buf, string, len + 1);
  return buf;
}



/* mysavestring: allocate a copy of a string on the heap */

char *
mysavestring(const char *string)
{
  return mystrndup(string, strlen(string));
}


/* add3strings: allocate a sufficently long buffer on the heap and
   successively copy the three arguments into it */

char *
add3strings(const char *str1, const char *str2, const char *str3)
{
  int size = strlen(str1) + strlen(str2) + strlen(str3) + 1;	/* total length plus 0 byte */
  char *buf = (char *)mymalloc(size);

  /* DPRINTF3(DEBUG_TERMIO,"size1: %d, size2: %d, size3: %d",   (int) strlen(str1), (int) strlen(str2),  (int) strlen(str3)); */

  mystrlcpy(buf, str1, size);
  mystrlcat(buf, str2, size);
  mystrlcat(buf, str3, size);
  return buf;
}


/* mybasename and mydirname: wrappers around basename and dirname, if
   available, otherwise emulations of them */ 

char *
mybasename(char *filename)
{				/* determine basename of "filename" */

  char *p;

  /* find last '/' in name (if any) */
  for (p = filename + strlen(filename) - 1; p > filename; p--)
    if (*(p - 1) == '/')
      break;
  return p;
}

char *
mydirname(char *filename)
{				/* determine directory component of "name" */
  char *p;

  /* find last '/' in name (if any) */
  for (p = filename + strlen(filename) - 1; p > filename; p--)
    if (*(p - 1) == '/')
      break;
  return (p == filename ? "." : mystrndup(filename, p - filename));
}


/* search_and_replace() is a utilty for handling multi-line input
   (-m option), keeping track of the cursor position on rlwraps prompt
   in order to put the cursor on the very same spot in the external
   editor For example, when using NL as a newline substitute (rlwrap
   -m NL <command>):
   
   search_and_replace("NL", "\n", "To be NL ... or not to be", 11,
   &line, &col) will return "To be \n ... or not to be", put 2 in line
   and 3 in col because a cursor position of 11 in "To be NL ..."
   corresponds to the 3rd column on the 2nd line in "To be \n ..."

   col and line may be NULL pointers.
*/
   

char *
search_and_replace(char *patt, char *repl, const char *string, int cursorpos,
		   int *line, int *col)
{
  int i, j, k;
  int pattlen = strlen(patt);
  int replen = strlen(repl);
  int stringlen = strlen(string);
  int past_cursorpos = 0;
  int current_line = 1;
  int current_column = 1;
  size_t scratchsize;
  char *scratchpad, *result;


  DPRINTF2(DEBUG_READLINE, "string=%s, cursorpos=%d",
	   mangle_string_for_debug_log(string, 40), cursorpos);
  scratchsize = max(stringlen, (stringlen * replen) / pattlen) + 1;	/* worst case : repleng > pattlen and string consists of only <patt> */
  DPRINTF1(DEBUG_READLINE, "Allocating %d bytes for scratchpad", (int) scratchsize);
  scratchpad = mymalloc(scratchsize);


  for (i = j = 0; i < stringlen;) {
    if (strncmp(patt, string + i, pattlen) == 0) {
      i += pattlen;
      for (k = 0; k < replen; k++)
	scratchpad[j++] = repl[k];
      current_line++;
      current_column = 1;
    } else {
      scratchpad[j++] = string[i++];
      current_column++;
    }
    if (i >= cursorpos && !past_cursorpos) {
      past_cursorpos = 1;
      if (line)
	*line = current_line;
      if (col)
	*col = current_column;
    }
  }
  scratchpad[j] = '\0';
  result = mysavestring(scratchpad);
  free(scratchpad);
  return (result);
}


/* first_of(&string_array) returns the first non-NULL element of string_array  */
char *
first_of(char **strings)
{				
  char **p;

  for (p = strings; *p == NULL; p++);
  return *p;
}


/* allocate string representation of an integer on the heap */
char *
as_string(int i)
{
#define MAXDIGITS 10 /* let's pray no-one edits multi-line input more than 10000000000 lines long :-) */
  char *newstring = mymalloc(MAXDIGITS+1); 

  snprintf1(newstring, MAXDIGITS, "%d", i);
  return (newstring);
}



char *
mangle_char_for_debug_log
(char c, int quote_me) {
  char *special = NULL;
  char scrap[10], code, *format;
  char *remainder = "\\]^_"; 
  
  switch (c) {
  case 0: special = "<NUL>"; break;
  case 8: special  = "<BS>";  break;
  case 9: special  = "<TAB>"; break;
  case 10: special = "<NL>";  break;
  case 13: special = "<CR>";  break;
  case 27: special = "<ESC>"; break;
  case 127: special = "<DEL>"; break;
  }
  if (!special) {
    if (c > 0 && c < 27  ) {
      format = "<CTRL-%c>"; code =  c + 96;
    } else if (c > 27 && c < 32) {
      format = "<CTRL-%c>"; code =  remainder[c-28];
    } else {
      format = (quote_me ? "\"%c\"" : "%c"); code = c;
    }	
    snprintf1(scrap, sizeof(scrap), format, code);
  }
  return mysavestring (special ? special : scrap);
}	


/* mangle_string_for_debug_log(string, len) returns a printable
   representation of string for the debug log. It will truncate a
   resulting string longer than len, appending three dots ...  */

char *
mangle_string_for_debug_log(const char *string, int maxlen)
{
  int total_length;
  char *mangled_char, *old, *new;
  const char *p; /* good old K&R-style *p. I have become a fossil... */

  if (!string)
    return mysavestring("(null)");
  new = mysavestring("");
  for(p = string, total_length = 0, old = new; *p; p++) {
    mangled_char = mangle_char_for_debug_log(*p, FALSE);
    total_length +=  strlen(mangled_char);
    if (total_length > maxlen)
      break;
    new = add2strings(old, mangled_char);
    free(old); free(mangled_char);
    old = new; /* I hate this idiom. what's the clearest/most elegant way to do this ? */
  }	
  return new;
}


/* extract_separator is used for the --history-format option. It
 extracts the first few non-space characters from the format, which
 will be used to recognise and strip off the appended format from
 recalled history items.
 */

static char *
extract_separator(char *format)
{ 
  static char *separator = NULL, *p;
  if(! separator) {
    separator = mysavestring(format);
    for (p = separator; *p ; p++) {
      if(*p == ' ') {
	*p = '\0';
	break;
      }
    }
    assert (*separator);   
  }	
  return separator; 
}


static char *
mystrstr(const char *haystack, const char *needle)
{
  return strstr(haystack, needle);
}


/*  trim_from_last_separator is used for the --history-format option
    it recognises and strips off the appended format from
    recalled history items
*/

static char
*trim_from_last_separator(char *line, char *separator)
{
  char *savedline = mysavestring(line),
       *chopoff= savedline,
       *shorter, *retval;
  
  while ((strlen(separator) < strlen(chopoff)) &&
	 (shorter = mystrstr(chopoff + strlen(separator), separator)))
    chopoff = shorter;

  if(chopoff != savedline)
    *chopoff = '\0';
  retval = mysavestring(savedline); 

  free(savedline);
  return retval;
}



static void
replace_in(char **scrap, char *item, char*replacement)
{
  char *retval = search_and_replace(item, replacement, *scrap, 0, NULL, NULL);
  free(*scrap);
  *scrap = retval;
}



char *
append_and_expand_history_format(char *line_with_possible_appendage)
{
  char *scrap , *strftime_format, *line, *result;
  struct timeval now;
  struct tm *today;
  
  
  scrap = mysavestring(history_format);
  
  change_working_directory(); /* actualise slaves_working_directory */
  replace_in (&scrap, "%D", slaves_working_directory);
  replace_in (&scrap, getenv("HOME"), "~");
  replace_in (&scrap, "%P", saved_rl_state.prompt);
  replace_in (&scrap, "%C", command_name);
  
  gettimeofday(&now, NULL);
  today = localtime(& now.tv_sec);

  strftime_format = mysavestring(scrap);  
  replace_in(&scrap, "%", "Wed Nov  8 16:18:49 CET 2006      "); /* should be big enough for %+ */
  strftime(scrap, strlen(scrap), strftime_format, today);  
  free(strftime_format);

  line  = trim_from_last_separator(line_with_possible_appendage,
				   extract_separator(history_format));
  assert(strlen(line) > 0);  
  result = add3strings(line, (line[strlen(line) - 1] == ' ' ? "" : " "), scrap);
  free(scrap);
  
  return result;		
} 	
	
char ESCAPE = '\033';
char BACKSPACE = '\010';
char CARRIAGE_RETURN = '\015';

static int within_control_seq = FALSE;

/* bleach(&buf) will overwrite buf (up to and including the first
   '\0') with a copy of itself, omitting control sequences of the form
   "ESC[^:alpha:]*[:alpha:]" (think ANSI color codes) . If buf ends in
   the middle of a control sequence, this is remembered in the static
   variable within_control_seq, so that control sequences can be
   eliminated across buffer boundaries. Because the re-written string
   is always shorter than the original, we need not worry about
   writing outside buf */
   
void
bleach(char * buf)
{
  char *p, *q;
  for(p = q = buf; *p; p++)  {
    if (*p == ESCAPE)
      within_control_seq = TRUE;
    if (! within_control_seq) {
      assert (q <= p);
      assert (q >= buf); 
      *q++ = *p;
    }
    if (isalpha(*p))
      within_control_seq =  FALSE;
  }
  *q = '\0';
}


/* unbackspace(&buf) will overwrite buf (up to and including the first
   '\0') with a copy of itself. Backspaces will move the "copy
   pointer" one backwards, carriage returns will re-set it to the
   begining of buf.  Because the re-written string is always shorter
   than the original, we need not worry about writing outside buf */

void
unbackspace(char* buf) {
  char *p, *q;	
  for(p = q = buf; *p; p++)  {
    if (*p == BACKSPACE)
      q = (q > buf ? q - 1 : q);
    else if (*p == CARRIAGE_RETURN)
       q = buf; 
    else {
      assert (q <= p);
      assert (q >= buf); 	
      *q++ = *p;
    }
  }
  *q = '\0';
}



/* Readline allows to single out character sequences that take up no
   physical screen space when displayed by bracketing them with the
   special markers `RL_PROMPT_START_IGNORE' and `RL_PROMPT_END_IGNORE'
   (declared in `readline.h').

   mark_invisible(buf) returns a new copy of buf with sequences of the
   form ESC[;0-9]*m? marked in this way. 

*/

/*
  (Re-)definitions for testing   
#undef RL_PROMPT_START_IGNORE
#undef  RL_PROMPT_END_IGNORE
#undef isprint
#define isprint(c) (c != 'x')
#define RL_PROMPT_START_IGNORE '('
#define RL_PROMPT_END_IGNORE ')'
#define ESCAPE 'E'
*/

static void match_and_copy_unprintable (const char **original, char **copy);
static void  match_and_copy_ESC_sequence (const char **original, char **copy);
static void match_and_copy(const char *charlist, const char **original, char **copy);
static int matches (const char *charlist, char c) ;
static void copy_next(int n, const char **original, char **copy);

char *
mark_invisible(const char *buf)
{
    char *scratchpad = mymalloc(3 * strlen(buf) + 1); /* worst case: every char in buf gets surrounded by RL_PROMPT_{START,END}_IGNORE */
    char *result = scratchpad;
    const char **original = &buf;
    char **copy = &scratchpad;
    
    while (**original) {
      /* printf ("orig: %p, copy: %p, result: %s @ %p\n", *original, *copy, result, result); */
      match_and_copy_ESC_sequence(original, copy);
      /* match_and_copy_unprintable(original, copy); */
      copy_next(1, original, copy);
    }
    **copy = '\0';
    return(result);	
}	


static void
match_and_copy_unprintable (const char **original, char **copy)
{
  if (isprint(**original))
    return;
  *(*copy)++ = RL_PROMPT_START_IGNORE;
  while(**original && !isprint(**original))
    *(*copy)++ = *(*original)++;
  *(*copy)++ = RL_PROMPT_END_IGNORE;
}

static void
match_and_copy_ESC_sequence (const char **original, char **copy)
{
  if (**original != ESCAPE || ! matches ("[]", *(*original + 1)))
    return;       /* not an ESC[ sequence */
  *(*copy)++ = RL_PROMPT_START_IGNORE;
  copy_next(2, original, copy);
  match_and_copy(";0123456789", original, copy);
  match_and_copy("m", original, copy);
  *(*copy)++ = RL_PROMPT_END_IGNORE;
}	
    
static void
match_and_copy(const char *charlist, const char **original, char **copy)
{
  while (matches(charlist, **original))
    *(*copy)++ = *(*original)++;
}	

static int
matches (const char *charlist, char c)
{
  const char *p;
  for (p = charlist; *p; p++)
    if (*p == c)
      return TRUE;
  return FALSE;
}	
  

static void
copy_next(int n, const char **original, char **copy)
{
  int i;
  for (i = 0; **original && (i < n); i++)
    *(*copy)++ = *(*original)++;
}




char *
copy_and_unbackspace(const char *original)
{
  char *copy = mysavestring(original);
#if 0  
  char *copy_start = copy;
  for( ; *original; original++) {
    if(*original == BACKSPACE)
      copy = (copy > copy_start ? copy - 1 : copy_start);
    else if (*original == CARRIAGE_RETURN)
      copy = copy_start;
    else
      *copy++ = *original;
  }	
  *copy = '\0';
  return copystart;
#else
    return copy;
#endif
}
  

/* helper function: returns the number of displayed characters (the
   "colourless length") of str (which has its unprintable sequences
   marked with RL_PROMPT_*_IGNORE).  Puts a copy without the
   RL_PROMPT_*_IGNORE characters in *copy_without_ignore_markers (if
   != NULL)
   */

int
colourless_strlen(const char *str, char **copy_without_ignore_markers)
{
  int counting = TRUE, count = 0;
  const char *p;
  char *q =  NULL; 
  if (copy_without_ignore_markers) 
    q = *copy_without_ignore_markers = mymalloc(strlen(str)+1);
    
  for(p = str; *p; p++) {
    switch (*p) {
    case RL_PROMPT_START_IGNORE:
      counting = FALSE;
      continue;
    case RL_PROMPT_END_IGNORE:
      counting = TRUE;
      continue;
    }	
    
    count += (counting ? 1 : 0);
    if (copy_without_ignore_markers)
      *q++ = *p;
  }
  if (copy_without_ignore_markers)
    *q = '\0';
  return count;
}

int
colourless_strlen_unmarked (const char *str)
{
  char *marked_str = mark_invisible(str);
  int colourless_length = colourless_strlen(marked_str, NULL);
  free(marked_str);
  return colourless_length;
}


/* skip a maximal number (possibly zero) of termwidth-wide
   initial segments of long_line and return the remainder
   (i.e. the last line of long_line on screen)
   if long_line contains an ESC character, return "" (signaling
   "don't touch") */   


char *
get_last_screenline(char *long_line, int termwidth)
{
  int line_length, removed;
  char *line_copy, *last_screenline;

  line_copy = copy_and_unbackspace(long_line);
  line_length = strlen(line_copy);
  
  if (termwidth == 0 ||              /* this may be the case on some weird systems */
      line_length <=  termwidth)  {  /* line doesn't extend beyond right margin
					@@@ are there terminals that put the cursor on the
					next line if line_length == termwidth?? */
     return line_copy; 
  } else if (strchr(long_line, '\033')) { /* <ESC> found, give up */
    free (line_copy);
    return mysavestring("Ehhmm..? > ");
  } else {	
    removed = (line_length / termwidth) * termwidth;   /* integer arithmetic: 33/10 = 3 */
    last_screenline  = mysavestring(line_copy + removed);
    free(line_copy);
    return last_screenline;
  }
}
 
			

