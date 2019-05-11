CC		= gcc
CFLAGS		= -std=gnu99 -O3 -W -Wall `pkg-config --cflags xrandr`
CFLAGS		= -std=gnu99 -g3 -W -Wall `pkg-config --cflags xrandr`
LDFLAGS		= -L/usr/X11R6/lib
LIBS		= -lXpm -lXext -lX11 -lm `pkg-config --libs xrandr` -lpthread
OBJECTS		= misc.o config.o brightness.o ui_x.o mmkeys.o wmbright.o

# where to install this program (also for packaging stuff)
PREFIX		= /usr/local
INSTALL_BIN	= -m 755
INSTALL_DATA	= -m 644

wmbright: $(OBJECTS)
	$(CC) -o $@ $(LDFLAGS) $(OBJECTS) $(LIBS)

clean:
	rm -rf *.o wmbright *~

install: wmbright
	install $(INSTALL_BIN)	wmbright	$(PREFIX)/bin
	install $(INSTALL_DATA)	wmbright.1x	$(PREFIX)/man/man1
