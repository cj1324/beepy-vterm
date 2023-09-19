/* gcc main.c -std=c99 -lvterm -lSDL2 -lSDL2_ttf -lutil -o sdl 
   warnings: -Wall -Wextra -Wno-parentheses -Wno-unused-parameter -Wpedantic
 originally based on: https://gist.github.com/shimarin/71ace40e7443ed46387a477abf12ea70
 TODO:
 - window resize
 - color scheme
 */

#define _POSIX_C_SOURCE 200112L
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vterm.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <signal.h>


void sighandler(int);

static VTerm *vterm;
static VTermScreen *screen;
static SDL_Texture *texture = NULL;
static bool *changed;
static char *row_string;
static TTF_Font *font;
static int font_width, font_height;
static int win_width, win_height;
static short rows, cols;
static bool ringing = false;

static SDL_Color CBlack = (SDL_Color){0xFF, 0xFF, 0xFF, 255};
static SDL_Color CWhite = (SDL_Color){0x0, 0x0, 0x0, 255};

static VTermPos cursor_pos;

static void
output_callback(char const *s, size_t len, void *user)
{
	if (write(*(int*)user, s, len) < 0)
		perror("write failed");
}

static int
damage(VTermRect rect, void *user)
{
	if (texture) {
		SDL_DestroyTexture(texture);
		texture = NULL;
	}

	for (int row = rect.start_row; row < rect.end_row; row++)
		changed[row] = true;

	return 0;
}

static int
movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
	cursor_pos = pos;
	return 0;
}

static int
bell(void *user)
{
	ringing = true;
	return 0;
}

static int
resize(int newrows, int newcols, void *user)
{
	rows = newrows;
	cols = newcols;
	return 0;
}

static VTermScreenCallbacks const
screen_callbacks = {
	.damage = damage,
	.movecursor = movecursor,
	.bell = bell,
	.resize = resize,
};

/* Encode a code point using UTF-8
   author: Ondřej Hruška <ondra@ondrovo.com>
   license: MIT */
static int
utf8_encode(char *out, uint32_t utf)
{
	if (utf <= 0x7F) {
		out[0] = utf & 0x7F;
		out[1] = 0;
		return 1;
	} else if (utf <= 0x07FF) {
		out[0] = utf >> 6 & 0x1F | 0xC0;
		out[1] = utf & 0x3F | 0x80;
		out[2] = 0;
		return 2;
	} else if (utf <= 0xFFFF) {
		out[0] = utf >> 12 & 0x0F | 0xE0;
		out[1] = utf >> 6 & 0x3F | 0x80;
		out[2] = utf & 0x3F | 0x80;
		out[3] = 0;
		return 3;
	} else if (utf <= 0x10FFFF) {
		out[0] = utf >> 18 & 0x07 | 0xF0;
		out[1] = utf >> 12 & 0x3F | 0x80;
		out[2] = utf >> 6 & 0x3F | 0x80;
		out[3] = utf & 0x3F | 0x80;
		out[4] = 0;
		return 4;
	} else { 
		out[0] = 0;
		return 0;
	}
}

static void
get_style(VTermScreenCell cell, SDL_Color *color, SDL_Color *bgcolor, int *style)
{
	*color = (SDL_Color){200,200,200,255};
	*bgcolor = (SDL_Color){40,40,40,255};
	if (VTERM_COLOR_IS_INDEXED(&cell.fg)) {
		vterm_screen_convert_color_to_rgb(screen, &cell.fg);
	}
	if (VTERM_COLOR_IS_RGB(&cell.fg)) {
		*color = (SDL_Color){cell.fg.rgb.red, cell.fg.rgb.green, cell.fg.rgb.blue, 255};
	}
	if (VTERM_COLOR_IS_INDEXED(&cell.bg)) {
		vterm_screen_convert_color_to_rgb(screen, &cell.bg);
	}
	if (VTERM_COLOR_IS_RGB(&cell.bg)) {
		*bgcolor = (SDL_Color){cell.bg.rgb.red, cell.bg.rgb.green, cell.bg.rgb.blue, 255};
	}

	if (cell.attrs.reverse) {
		SDL_Color temp = *color;
		*color = *bgcolor;
		*bgcolor = temp;
	}

	*style = TTF_STYLE_NORMAL;
	if (cell.attrs.bold) *style |= TTF_STYLE_BOLD;
	if (cell.attrs.underline) *style |= TTF_STYLE_UNDERLINE;
	if (cell.attrs.italic) *style |= TTF_STYLE_ITALIC;
	if (cell.attrs.strike) *style |= TTF_STYLE_STRIKETHROUGH;
}



static void
render_row(SDL_Renderer *renderer, SDL_Surface *surface, int row)
{
	for (int start = 0; start < cols; ) {
		int width = 0;

		VTermPos pos = { row, start };
		VTermScreenCell cell;
		vterm_screen_get_cell(screen, pos, &cell);

		SDL_Color color, bgcolor;
		int style;
		get_style(cell, &color, &bgcolor, &style);

		char *str = row_string;
		for (int col = start; col < cols; col++) {
			VTermPos pos = { row, col };
			VTermScreenCell cell;
			vterm_screen_get_cell(screen, pos, &cell);

			SDL_Color cur_color, cur_bgcolor;
			int cur_style;
			get_style(cell, &cur_color, &cur_bgcolor, &cur_style);

			if (cur_color.r != color.r
			|| cur_color.g != color.g
			|| cur_color.b != color.b
			|| cur_bgcolor.r != bgcolor.r
			|| cur_bgcolor.g != bgcolor.g
			|| cur_bgcolor.b != bgcolor.b
			|| cur_style != style)
				break;

			if (cell.chars[0] == 0xffffffff) continue;
			for (int i = 0; cell.chars[i] != 0 && i < VTERM_MAX_CHARS_PER_CELL; i++) {
				str += utf8_encode(str, cell.chars[i]);
			}
			width += cell.width;
		}

		SDL_Rect rect = { start * font_width, (row * font_height) + font_height, width * font_width, font_height };
		if (str - row_string > 0) {
			TTF_SetFontStyle(font, style);
			SDL_Surface *text_surface = TTF_RenderUTF8_Shaded(font, row_string, color, bgcolor);
			SDL_BlitSurface(text_surface, NULL, surface, &rect);
			SDL_FreeSurface(text_surface);
			if (rect.w < width * font_width) {
				rect.x += rect.w;
				rect.w = width * font_width - rect.w;
				SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, bgcolor.r, bgcolor.g, bgcolor.b));
			}
		} else {
			SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, bgcolor.r, bgcolor.g, bgcolor.b));
		}

		start += width;
		width = 0;
	}
}
static void
render_topbar(SDL_Renderer *renderer, SDL_Surface *surface, SDL_Rect window_rect, const char *msg)
{


	int cur_width = 0;

	TTF_SetFontStyle(font, TTF_STYLE_UNDERLINE | TTF_STYLE_BOLD);
	TTF_SizeUTF8(font, msg, &cur_width, NULL);
	SDL_Surface *text_surface = TTF_RenderUTF8_Shaded(font, msg, CBlack, CWhite);

	if (text_surface == NULL) {
		SDL_Log("error: %s", SDL_GetError());
	}

	SDL_Rect font_rect = { 0, 0, cur_width, font_height};
	SDL_BlitSurface(text_surface, &font_rect, surface, &window_rect);
	
	SDL_Texture *txt_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
	SDL_FreeSurface(text_surface);

	SDL_RenderCopy(renderer, txt_texture, NULL, &window_rect);
	SDL_DestroyTexture(txt_texture);
}

static void
render(SDL_Renderer *renderer, SDL_Surface *surface, SDL_Rect window_rect)
{
	if (!texture) {

		for (int row = 0; row < rows; row++) {
			if (!changed[row])
				continue;
			render_row(renderer, surface, row);
			changed[row] = false;
		}
		texture = SDL_CreateTextureFromSurface(renderer, surface);
	}
	SDL_RenderCopy(renderer, texture, &window_rect, &window_rect);
	// draw cursor
	VTermScreenCell cell;
	vterm_screen_get_cell(screen, cursor_pos, &cell);

	SDL_Rect rect = { cursor_pos.col * font_width, (cursor_pos.row * font_height) + font_height, font_width, font_height };
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	SDL_SetRenderDrawColor(renderer, 255,255,255,96 );
	SDL_RenderFillRect(renderer, &rect);

	SDL_SetRenderDrawColor(renderer, 255,255,255,255 );
	SDL_RenderDrawRect(renderer, &rect);

	if (ringing) {
		SDL_SetRenderDrawColor(renderer, 255,255,255,192 );
		SDL_RenderFillRect(renderer, &window_rect);
		ringing = false;
	}
}

static void
process_event(SDL_Event ev)
{
	if (ev.type == SDL_TEXTINPUT) {
		const Uint8 *state = SDL_GetKeyboardState(NULL);
		int mod = VTERM_MOD_NONE;
		if (state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL]) mod |= VTERM_MOD_CTRL;
		if (state[SDL_SCANCODE_LALT] || state[SDL_SCANCODE_RALT]) mod |= VTERM_MOD_ALT;
		if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) mod |= VTERM_MOD_SHIFT;
		size_t len = strlen(ev.text.text);
		for (size_t i = 0; i < len; i++) {
			vterm_keyboard_unichar(vterm, ev.text.text[i], (VTermModifier)mod);
		}
	} else if (ev.type == SDL_KEYDOWN) {
		switch (ev.key.keysym.sym) {
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			vterm_keyboard_key(vterm, VTERM_KEY_ENTER, VTERM_MOD_NONE);
			break;
		case SDLK_BACKSPACE:
			vterm_keyboard_key(vterm, VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
			break;
		case SDLK_ESCAPE:
			vterm_keyboard_key(vterm, VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
			break;
		case SDLK_TAB:
			vterm_keyboard_key(vterm, VTERM_KEY_TAB, VTERM_MOD_NONE);
			break;
		case SDLK_UP:
			vterm_keyboard_key(vterm, VTERM_KEY_UP, VTERM_MOD_NONE);
			break;
		case SDLK_DOWN:
			vterm_keyboard_key(vterm, VTERM_KEY_DOWN, VTERM_MOD_NONE);
			break;
		case SDLK_LEFT:
			vterm_keyboard_key(vterm, VTERM_KEY_LEFT, VTERM_MOD_NONE);
			break;
		case SDLK_RIGHT:
			vterm_keyboard_key(vterm, VTERM_KEY_RIGHT, VTERM_MOD_NONE);
			break;
		case SDLK_PAGEUP:
			vterm_keyboard_key(vterm, VTERM_KEY_PAGEUP, VTERM_MOD_NONE);
			break;
		case SDLK_PAGEDOWN:
			vterm_keyboard_key(vterm, VTERM_KEY_PAGEDOWN, VTERM_MOD_NONE);
			break;
		case SDLK_HOME:
			vterm_keyboard_key(vterm, VTERM_KEY_HOME, VTERM_MOD_NONE);
			break;
		case SDLK_END:
			vterm_keyboard_key(vterm, VTERM_KEY_END, VTERM_MOD_NONE);
			break;
		default:
			if (ev.key.keysym.mod & KMOD_CTRL && ev.key.keysym.sym < 127)
				vterm_keyboard_unichar(vterm, ev.key.keysym.sym, VTERM_MOD_CTRL);
			break;
		}
	} else if (ev.type == SDL_KEYUP){
		// ignore , only use KEYDOWN
		true;
	} else if (ev.type == SDL_MOUSEMOTION){
		// ignore , only use MOUSEMOTION
		true;
	} else {
		SDL_Log("> Event Type : %i\n", ev.type);
	}
}

static void
process_input(int fd)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	struct timeval timeout = { 0, 0 };
	if (select(fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
		char buf[4096];
		ssize_t size = read(fd, buf, sizeof(buf));
		if (size > 0)
			vterm_input_write(vterm, buf, size);
	}
}


static void
LogOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	printf("SDLLog: %s\n", message);
}




void sighandler(int signum) {
   printf("Caught signal %d, coming out...\n", signum);
   exit(1);
}

int
main(int argc, char **argv)
{
	int ptsize = 16;
	int c;


	while ((c = getopt (argc, argv, "p:")) != -1){
		switch(c){
			case 'p':
				ptsize = atoi(optarg);
				break;

		}
	}

	signal(SIGINT, sighandler);

	SDL_LogSetOutputFunction(LogOutputFunction, NULL);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}
	if (TTF_Init() < 0) {
		fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
		return 1;
	}
	font = TTF_OpenFont("/usr/share/fonts/truetype/Unifont/Unifont-Medium.ttf", ptsize);
	if (font == NULL) {
		fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
		return 1;
	}

	SDL_ShowCursor(SDL_DISABLE);
	SDL_Window *window = SDL_CreateWindow("sdl2-term", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 400, 240, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
	if (window == NULL) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
	int windowID = SDL_GetWindowID(window);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	if (renderer == NULL) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GetWindowSize(window, &win_width, &win_height);

	// SDL_StartTextInput();
	font_height = TTF_FontHeight(font);
	TTF_SizeUTF8(font, "O", &font_width, NULL);

	cols = win_width / font_width;
	rows = (win_height / font_height) - 1; // 顶部留空一行.

	SDL_Log("> Window Size %i,%i", win_width, win_height);
	SDL_Log("> Font Size %i,%i", font_width, font_height);
	SDL_Log("> Term Size %i,%i", cols, rows);

	int fd;

	struct winsize win = { rows, cols, 0, 0 };

	pid_t pid = forkpty(&fd, NULL, NULL, &win);
	if (pid < 0) {
		perror("forkpty");
		return 1;
	}

	if (!pid) {

		setenv("TERM", "xterm-mono", 1);
		setenv("PS1", "\\u@\\h:\\w\\$", 1);

		char *prog = getenv("SHELL");
		char *argv[] = { prog, "-", NULL };
		execvp(prog, argv);
		return 1;
	}

	changed = calloc(rows, sizeof(*changed));
	row_string = calloc(cols * 4 * VTERM_MAX_CHARS_PER_CELL, sizeof(*row_string));


	vterm = vterm_new(rows, cols);
	vterm_set_utf8(vterm, 1);
	vterm_output_set_callback(vterm, output_callback, (void*)&fd);

	screen = vterm_obtain_screen(vterm);
	vterm_screen_set_callbacks(screen, &screen_callbacks, NULL);
	vterm_screen_reset(screen, 1);

	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, win_width, win_height, 32, SDL_PIXELFORMAT_RGBA32);

	int status;
	int fps;
	int oldtime = SDL_GetTicks();
	int newtime = SDL_GetTicks();

	while (waitpid(pid, &status, WNOHANG) != pid) {

		oldtime = newtime;
		newtime = SDL_GetTicks();

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255 );
		SDL_RenderClear(renderer);
		SDL_Event ev;
		while(SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE && (ev.key.keysym.mod & KMOD_CTRL))) {
				kill(pid, SIGTERM);
			} else if (ev.type == SDL_WINDOWEVENT && ev.window.windowID == windowID){
				switch (ev.window.event) {
					case SDL_WINDOWEVENT_SIZE_CHANGED:
						SDL_Log("New windows : %i,%i\n", ev.window.data1, ev.window.data2);
						break;
					case SDL_WINDOWEVENT_CLOSE:
						ev.type = SDL_QUIT;
						SDL_PushEvent(&ev);
						break;

					case SDL_WINDOWEVENT_ENTER:
						SDL_Log("Windows enter.");
						break;
					case SDL_WINDOWEVENT_FOCUS_GAINED:
						SDL_Log("Windows Focus.");
						break;
					case SDL_WINDOWEVENT_SHOWN:
						SDL_Log("Windows shown.");
						break;
					default:
						SDL_Log("Windows Event Default: %i", ev.window.event);
						break;
				}
			} else {
				process_event(ev);
			}
		}

		process_input(fd);



		SDL_Rect top_rect = { 0, 0, font_width * cols, font_height};  // 顶部
		render_topbar(renderer, surface, top_rect, "Title Page");

		SDL_Rect rect = { 0, font_height, font_width * cols, font_height * rows };  // 终端部分
		render(renderer, surface, rect);

		SDL_RenderPresent(renderer);

		fps = 1000.0f / (newtime - oldtime);
		//SDL_Log("> FPS : %i\n", fps);
	}

	vterm_free(vterm);
	if (texture)
		SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);

	TTF_Quit();
	SDL_Quit();
	return status;
}
