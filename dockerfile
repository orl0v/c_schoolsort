FROM debian:buster

RUN apt-get update && \
    apt-get install -y \
    mingw-w64 \
    mingw-w64-tools \
    mingw-w64-x86-64-dev \
    gcc-mingw-w64 \
    g++-mingw-w64 \
    wget \
    unzip \
    pkg-config \
    p7zip-full

# Download MSYS2 GTK3 package
RUN wget https://repo.msys2.org/mingw/x86_64/mingw-w64-x86_64-gtk3-3.24.38-1-any.pkg.tar.zst && \
    tar -I zstd -xf mingw-w64-x86_64-gtk3-*.pkg.tar.zst -C /usr/x86_64-w64-mingw32 && \
    rm mingw-w64-x86_64-gtk3-*.pkg.tar.zst

# Download dependencies
RUN wget https://repo.msys2.org/mingw/x86_64/mingw-w64-x86_64-glib2-2.76.4-1-any.pkg.tar.zst && \
    wget https://repo.msys2.org/mingw/x86_64/mingw-w64-x86_64-cairo-1.17.8-3-any.pkg.tar.zst && \
    wget https://repo.msys2.org/mingw/x86_64/mingw-w64-x86_64-pango-1.50.14-1-any.pkg.tar.zst && \
    wget https://repo.msys2.org/mingw/x86_64/mingw-w64-x86_64-atk-2.38.0-1-any.pkg.tar.zst && \
    tar -I zstd -xf mingw-w64-x86_64-glib2-*.pkg.tar.zst -C /usr/x86_64-w64-mingw32 && \
    tar -I zstd -xf mingw-w64-x86_64-cairo-*.pkg.tar.zst -C /usr/x86_64-w64-mingw32 && \
    tar -I zstd -xf mingw-w64-x86_64-pango-*.pkg.tar.zst -C /usr/x86_64-w64-mingw32 && \
    tar -I zstd -xf mingw-w64-x86_64-atk-*.pkg.tar.zst -C /usr/x86_64-w64-mingw32 && \
    rm mingw-w64-x86_64-*.pkg.tar.zst

WORKDIR /src

# Create compile script
RUN echo '#!/bin/bash\n\
x86_64-w64-mingw32-gcc sorter.c \
    -o sorter.exe \
    -I/usr/x86_64-w64-mingw32/include/gtk-3.0 \
    -I/usr/x86_64-w64-mingw32/include/glib-2.0 \
    -I/usr/x86_64-w64-mingw32/lib/glib-2.0/include \
    -I/usr/x86_64-w64-mingw32/include/pango-1.0 \
    -I/usr/x86_64-w64-mingw32/include/cairo \
    -I/usr/x86_64-w64-mingw32/include/gdk-pixbuf-2.0 \
    -I/usr/x86_64-w64-mingw32/include/atk-1.0 \
    -L/usr/x86_64-w64-mingw32/lib \
    -lgtk-3 -lgdk-3 -lgdi32 -limm32 -lshell32 -lole32 \
    -Wl,-luuid -lpango-1.0 -lpangocairo-1.0 -lpangowin32-1.0 \
    -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 \
    -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl \
    -mwindows' > /compile.sh && \
    chmod +x /compile.sh

ENTRYPOINT ["/compile.sh"]