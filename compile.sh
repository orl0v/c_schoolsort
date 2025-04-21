#!/bin/bash
x86_64-w64-mingw32-gcc sorter.c \
    -o sorter.exe \
    -I/opt/gtk-win64/include/gtk-3.0 \
    -I/opt/gtk-win64/include/glib-2.0 \
    -I/opt/gtk-win64/lib/glib-2.0/include \
    -I/opt/gtk-win64/include/pango-1.0 \
    -I/opt/gtk-win64/include/cairo \
    -I/opt/gtk-win64/include/gdk-pixbuf-2.0 \
    -I/opt/gtk-win64/include/atk-1.0 \
    -L/opt/gtk-win64/lib \
    -lgtk-3 -lgdk-3 -lgdi32 -limm32 -lshell32 -lole32 \
    -Wl,-luuid -lpango-1.0 -lpangocairo-1.0 -lpangowin32-1.0 \
    -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 \
    -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl \
    -mwindows