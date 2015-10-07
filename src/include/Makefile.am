## Process this file with automake to produce Makefile.in

# Copy semi-public header files here, to isolate library namespaces
# from eachother and from the main program.
all-local: ezxml.h fb_public.h waitress.h piano.h

ezxml.h: $(top_srcdir)/src/libezxml/ezxml.h
	cp $(top_srcdir)/src/libezxml/ezxml.h .

fb_public.h: $(top_srcdir)/src/libfootball/fb_public.h
	cp $(top_srcdir)/src/libfootball/fb_public.h .

waitress.h: $(top_srcdir)/src/libwaitress/waitress.h
	cp $(top_srcdir)/src/libwaitress/waitress.h .

piano.h: $(top_srcdir)/src/libpiano/piano.h
	cp $(top_srcdir)/src/libpiano/piano.h .

clean-local:
	rm  -f *.h
