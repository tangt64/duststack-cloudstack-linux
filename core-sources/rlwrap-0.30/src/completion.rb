/*  completion.c is generated from completion.rb by the program rbgen
    (cf. http://libredblack.sourceforge.net/)
    
    completion.rb: maintaining the completion list, my_completion_function()
    (callback for readline lib)

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

#ifdef assert
#undef assert
#endif


int completion_is_case_sensitive = 1;

int
compare(const char *string1, const char *string2)
{
  const char *p1;
  const char *p2; 
  int count;

  for (p1 = string1, p2 = string2, count = 0;
       *p1 && *p2 && count < BUFFSIZE; p1++, p2++, count++) {
    char c1 = completion_is_case_sensitive ? *p1 : tolower(*p1);
    char c2 = completion_is_case_sensitive ? *p2 : tolower(*p2);

    if (c1 != c2)
      return (c1 < c2 ? -1 : 1);
  }
  if ((*p1 && *p2) || (!*p1 && !*p2))
    return 0;
  return (*p1 ? 1 : -1);
}




#ifndef DEBUG
#  ifdef malloc
#    undef malloc
#  endif
#  define malloc(x) mymalloc(x) /* This is a bit evil, but there is no other way to make libredblack use mymalloc() */
#else
#  undef malloc
#  undef free
#  define mystrndup(s,n) strdup(s)     /* when debugging, don't use our debugging malloc as readline will free our malloced memory */
#  define mysavestring(s) strdup(s)
#endif

/* This file has to be processed by the program rbgen  */

%%rbgen
%type char
%cmp compare
%access pointer
%static
%omit
%%rbgen


/* forward declarations */
static struct rbtree *completion_tree;

static void add_word_to_completions(const char *word); 
char *my_history_completion_function(char *prefix, int state);
static void print_list(void);


void
my_rbdestroy(struct rbtree *rb)
{				/* destroy rb tree, freeing the keys first */
  const char *key, *lastkey;

  for (key = rbmin(rb);
       key;
       lastkey = key, key =
       rblookup(RB_LUGREAT, key, rb), free((void *)lastkey))
    rbdelete(key, rb);
  rbdestroy(rb);
}


void
print_list()
{
  const char *word;
  RBLIST *completion_list = rbopenlist(completion_tree);	/* uses mymalloc() internally, so no chance of getting a NULL pointer back */

  printf("Completions:\n");
  while ((word = rbreadlist(completion_list)))
    printf("%s\n", word);
  rbcloselist(completion_list);
}

void
init_completer()
{
  completion_tree = rbinit();
}


static void
add_word_to_completions(const char *word)
{
  rbsearch(mysavestring(word), completion_tree);	/* the tree stores *pointers* to the words, we have to allocate copies of them ourselves
							   freeing the tree will call free on the pointers to the words
							   valgrind reports the copies as lost, I don't understand this. */
}


void
feed_line_into_completion_list(const char *line)
{
  char *word;
  const char *p;
  char *scratchpad = mysavestring(line);
  for (p = scratchpad; (word = strtok((char *)p, rl_basic_word_break_characters)); p = NULL)	/* Aargh! strtok() */
    add_word_to_completions(word);
  free(scratchpad);
}

void
feed_file_into_completion_list(const char *completions_file)
{
  FILE *compl_fp;
  char buffer[BUFFSIZE];

  if ((compl_fp = fopen(completions_file, "r")) == NULL)
    myerror("Could not open %s", completions_file);
  while (fgets(buffer, BUFFSIZE - 1, compl_fp) != NULL) {
    buffer[BUFFSIZE - 1] = '\0';	/* make sure buffer is properly terminated (it should be anyway, according to ANSI) */
    feed_line_into_completion_list(buffer);
  }
  fclose(compl_fp);
  /* print_list(); */
}


#define COMPLETE_FILENAMES 1
#define COMPLETE_FROM_LIST 2
#define COMPLETE_USERNAMES 4
#define COMPLETE_PARANORMALLY 8 /* read user's thoughts */


int
get_completion_type()
{				/* some day, this function will inspect the current line and make rlwrap complete
				   differently according to the word *preceding* the one we're completing */
  return (COMPLETE_FROM_LIST | (complete_filenames ? COMPLETE_FILENAMES : 0));
}


/* helper function for my_completion_function */
static int
is_prefix(const char *s0, const char *s1)
{				/* s0 is prefix of s1 */
  const char *p0, *p1;
  int count;

  for (count = 0, p0 = s0, p1 = s1; *p0; count++, p0++, p1++) {
    char c0 = completion_is_case_sensitive ? *p0 : tolower(*p0);
    char c1 = completion_is_case_sensitive ? *p1 : tolower(*p1);

    if (c0 != c1 || count == BUFFSIZE)
      return FALSE;
  }
  return TRUE;
}

/* See readline doumentation: this function is called by readline whenever a completion is needed. The first time state == 0,
   whwnever the user presses TAB to cycle through the list, my_completion_function() is called again, but then with state != 0
   It should return the completion, which then will be freed by readline (so we'll hand back a copy instead of the real thing) */


char *
my_completion_function(char *prefix, int state)
{
  static struct rbtree *scratch_tree = NULL;
  static RBLIST *scratch_list = NULL;	/* should remain unchanged between invocations */
  int completion_type, count;
  const char *word;
  const char *completion;

  if (*prefix == '!')
    return my_history_completion_function(prefix + 1, state);

  if (state == 0) {		/* first time we're called for this prefix */
    if (scratch_list)
      rbcloselist(scratch_list);
    if (scratch_tree)
      my_rbdestroy(scratch_tree);
    scratch_tree = rbinit();	/* allocate scratch_tree. We will use this to get a sorted list of completions */
    /* now find all possible completions: */
    completion_type = get_completion_type();
    if (completion_type & COMPLETE_FROM_LIST) {
      for (word = rblookup(RB_LUGREAT, prefix, completion_tree);	/* start with first word > prefix */
	   word && is_prefix(prefix, word);	/* as long as prefix is really prefix of word */
	   word = rblookup(RB_LUGREAT, word, completion_tree)) {	/* find next word in list */
	rbsearch(mysavestring(word), scratch_tree);	/* insert fresh copy of the word */
	/* DPRINTF1(DEBUG_READLINE, "Adding %s to completion list ", word); */
      }
    }
    if (completion_type & COMPLETE_FILENAMES) {
      change_working_directory();
      for (count = 0; (word = rl_filename_completion_function(prefix, count)); count++) {	/* using rl_filename_completion_function means
											   that completing filenames will always be case-sensitive */
	rbsearch(word, scratch_tree);
      }
    }
    scratch_list = rbopenlist(scratch_tree);
  }

  /* we get here each time the user presses TAB to cycle through the list */
  assert(scratch_tree);
  assert(scratch_list);
  if ((completion = rbreadlist(scratch_list))) {	/* read next possible completion */
    return (mysavestring(completion));	/* we cannot just return it as  readline will free it (and make rlwrap explode) */
  } else {
    return NULL;
  }
}




char *
my_history_completion_function(char *prefix, int state)
{
  while (next_history());
  if (state || history_search_prefix(prefix, -1) < 0)
    return NULL;
  return mysavestring(current_history()->line);
}







/* The following sets edit modes for GNU EMACS
   Local Variables:
   mode:c
   End:  */
