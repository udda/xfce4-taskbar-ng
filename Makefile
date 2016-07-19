PP=-DPACKAGE_NAME="\"Taskbar\"" -DLC_MESSAGES="\"C\"" -DHELPDIR="\"/usr/share/doc/xfce4-taskbar/\""
CFLAGS=-fPIC $(PP) -g -I. `pkg-config --cflags-only-I gtk+-2.0 exo-1 libwnck-1.0 libxfce4panel-1.0 libxfce4ui-1 libxfconf-0 gtkhotkey-1.0`
#CFLAGS=-fPIC $(PP) -O3 -I. `pkg-config --cflags-only-I gtk+-2.0 exo-1 libwnck-1.0 libxfce4panel-1.0 libxfce4ui-1 libxfconf-0 gtkhotkey-1.0`
LINKFLAGS=`pkg-config --libs gtk+-2.0 exo-1 libwnck-1.0 libxfce4panel-1.0 libxfce4ui-1 libxfconf-0 gobject-2.0 gtk+-x11-2.0 glib-2.0 gtkhotkey-1.0`

OBJ=taskbar.o taskbar-widget.o hotkeys.o panel-debug.o panel-utils.o panel-xfconf.o

all:libtaskbar.so

taskbar-dialog_ui.h:taskbar-dialog.glade
	python convertGladeToC.py

taskbar.o:taskbar-dialog_ui.h

libtaskbar.so:$(OBJ)
	g++ -shared  -o libtaskbar.so $(OBJ) $(LINKFLAGS)

install:
	cp `pwd`/libtaskbar.so /usr/lib/xfce4/panel-plugins/libtaskbar.so
	cp `pwd`/taskbar.desktop /usr/share/xfce4/panel/plugins/taskbar.desktop

uninstall:
	rm -f /usr/lib/xfce4/panel-plugins/libtaskbar.so
	rm -f /usr/share/xfce4/panel/plugins/taskbar.desktop

clean:
	rm -f *.o *.so

devenv:
	sudo apt-get install libgtk2.0-dev libexo-1-dev libxfce4ui-1-dev libxfce4util-dev libxfcegui4-dev libxfconf-0-dev xfce4-panel-dev libwnck-dev libgtkhotkey-dev libxfce4ui-1-dev

restart:
	xfce4-panel -r

	
