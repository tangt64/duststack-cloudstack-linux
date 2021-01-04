/*  malloc_debug.c: attempt at malloc/free replacement for debugging
    configure with --enable-malloc-debugger to use it
 */

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

/* test this debugger by compiling stand-alone with gcc -otest -DTESTME malloc_debug.c */


#ifdef TESTME
   int debug=8;
#  undef  malloc
#  undef  mymalloc
#  undef  free
#  define mymalloc malloc
#  define DEBUG_MEMORY_MANAGEMENT 8
#  define USE_MALLOC_DEBUGGER
#endif


#ifdef DEBUG
#  define USE_MALLOC_DEBUGGER
#  undef mymalloc
#  undef free
#endif

#ifdef USE_MALLOC_DEBUGGER



#define SLAVERY_SLOGAN "rlwrap is boss!"
#define FREEDOM_SLOGAN "free at last!"
#define SLOGAN_MAXLEN 20

extern int debug;

typedef RETSIGTYPE (*sighandler_t)(int);

typedef struct freed_stamp
{
  char magic[SLOGAN_MAXLEN];  /* magical string that tells us this something about this memory: mallocced or freed? */
  char *file;                 /* source file where we were mallocced/freed */ 
  int line;                   /* source line where we were mallocced/freed */
  int size;
} *Freed_stamp;	

static int memory_usage = 0;

static char *offending_sourcefile;
static int offending_line;
sighandler_t old_segfault_handler;


static RETSIGTYPE
handle_segfault(int unused)
{
  fprintf(stderr, "free() called on bad (unallocated) memory at %s:%d\n", offending_sourcefile, offending_line);
  exit(1);
}


		      
/* allocates chunk of memory including a freed_stamp, in which we write
   line and file where we were alllocated, and a slogan to testify that
   we have been allocated and not yet freed. returns the address past the stamp
   If you think this memory will ever be freed by a normal free() use malloc_foreign instead
*/

void *
debug_malloc(size_t size,  char *file, int line)
{
  void *chunk;
  Freed_stamp stamp;

  if (!(debug & DEBUG_MEMORY_MANAGEMENT))
    return mymalloc(size);
  
  chunk = mymalloc(sizeof(struct freed_stamp) + size);
  stamp = (Freed_stamp) chunk;
  memory_usage += size;
  DPRINTF4(DEBUG_MEMORY_MANAGEMENT, "malloc size: %d at %s:%d (total usage now: %d)",  (int) size, file, line, memory_usage); 
  strncpy(stamp->magic, SLAVERY_SLOGAN, SLOGAN_MAXLEN);
  stamp -> file = file;
  stamp -> line = line;
  stamp -> size = size;

  return (char *) chunk + sizeof(struct freed_stamp); 
}	



/* Verifies that ptr indeed points to memory allocaded by debug_malloc,
   and has not yet been freed. Doesn't really free it, but marks it as freed
   so that we easily notice double frees */
void
debug_free(void *ptr, char *file, int line)
{
  Freed_stamp stamp;

  if (!(debug & DEBUG_MEMORY_MANAGEMENT)) {
    free(ptr);
    return;
  }

  stamp = ((Freed_stamp) ptr) - 1;
  offending_sourcefile = file; /* use static variables to communicate with signal handler */
  offending_line = line;
  old_segfault_handler = signal(SIGSEGV, &handle_segfault);
  * (char *) ptr = 'x'; /* this, or the next statement  will provoke a segfault when address is not writable, i.e. in read-only memory or
		     not in a mamory-mapped area */
  if (strcmp(FREEDOM_SLOGAN, stamp -> magic) == 0) { /* Ah! this memory has been freed before! */
    fprintf(stderr, "free() called twice, at %s:%d (previous free was at %s:%d)\n",
	    file, line, stamp->file, stamp->line);
    exit(1);
  } else if (strcmp(SLAVERY_SLOGAN, stamp -> magic) == 0) {
    DPRINTF4(DEBUG_MEMORY_MANAGEMENT, "free() (called at %s:%d) of memory malloced at %s:%d", file, line, stamp->file, stamp->line);
    strncpy(stamp->magic, FREEDOM_SLOGAN, SLOGAN_MAXLEN);
    stamp -> file = file;
    stamp -> line = line;
    memory_usage -= stamp -> size;
    signal(SIGSEGV, old_segfault_handler);
  } else {
    fprintf(stderr, "free() called (at %s:%d) on unmallocced memory, or memory not malloced by debug_malloc()\n",
	    file, line);
    exit(1);
  }	
}	

/* this function calls free() directly, and should be used on memory that was malloc'ed outside our own jurisdiction,
   i.e. not by debug_malloc(); */
void free_foreign(void *ptr) {
  /* free(ptr); */
}

/* this function calls malloc() directly, and should be used on memory that could be freed  outside our own jurisdiction,
   i.e. not by debug_free(); */
void * malloc_foreign(size_t size) {
  return malloc(size);
}	

#ifdef TESTME
#  define free(ptr) debug_free(ptr,__FILE__,__LINE__)
#  define malloc(size) debug_malloc(size,__FILE__,__LINE__)
#  define test_main main
#endif

int test_main() {
  char *bloh = malloc(33);
  free(bloh);
  free(bloh);
  exit(0);
}

#endif /* def USE_MALLOC_DEBUGGER */
