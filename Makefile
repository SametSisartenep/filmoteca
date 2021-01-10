CC=cc
CFLAGS=-Wall -Wno-missing-braces -Wno-parentheses -Wno-switch -Wno-pointer-to-int-cast -fno-diagnostics-color -I/usr/local/include -ggdb -c -O2
LDFLAGS=-static -L/usr/local/lib -pthread -lfmt -lutf
O=o

TARG=filmoteca
OFILES=\
	filmoteca.$O\
	util.$O\

HFILES=\
	dat.h\
	fns.h\
	args.h\

.PHONY: all clean
all: $(TARG)

%.$O: %.c
	$(CC) $(CFLAGS) $<

$(OFILES): $(HFILES)

$(TARG): $(OFILES)
	$(CC) -o $@ $(OFILES) $(LIBS) $(LDFLAGS)

install: $(TARG)
	cp $(TARG) $(HOME)/bin/
	cp filmsrv $(HOME)/bin/

uninstall:
	rm -f $(HOME)/bin/$(TARG) $(HOME)/bin/filmsrv

clean:
	rm $(OFILES) $(TARG)
