bin_PROGRAMS = rlwrap 
rlwrap_SOURCES =  main.c signals.c readline.c pty.c completion.c term.c ptytty.c  utils.c string_utils.c malloc_debug.c
AM_CFLAGS=-DDATADIR=\"@datadir@\" 

# use old-fashoned suffix rule for non-GNU make
.rb.c :
	rbgen $< $@ 

clean-local: 
	rm -f *~ 