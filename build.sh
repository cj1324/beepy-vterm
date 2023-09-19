
SDL2=$(sdl2-config --libs)
VTERM=$(pkg-config --libs vterm)

gcc -o sdl main.c ${SDL2} ${VTERM} -lSDL2_ttf -lutil -I /usr/local/include -D_REENTRANT

