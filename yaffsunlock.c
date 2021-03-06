#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <assert.h>
#include <sys/wait.h>

#include "minui/minui.h"

//#define TOUCH_DEBUGGING_ENABLED

// not sure what to call this, but on my Nexus One it looks like touches are captured at 8x screen res
#define TOUCH_RESOLUTION 8

// these are essentially constants, but initialized at runtime
int CHAR_WIDTH, CHAR_HEIGHT, FB_WIDTH, FB_HEIGHT;

#define SOFTKEYBD_TOP (FB_HEIGHT-260)

char passphrase[1024];
bool newchar_frame = false;

gr_surface img_softkeybd[4];

char **cmd_passphrase_argv;
char *cmd_mountramdisk;

#define SHIFT_STATE_NONE 0
#define SHIFT_STATE_SHIFTED 1
#define SHIFT_STATE_NUMSYM 2
#define SHIFT_STATE_ALT 3

struct {
    int shift_state;
} hard_keybd, soft_keybd;

typedef struct {
    unsigned char code_to_ch[255];
} keymap;

// index by shift state and then key code to get character
keymap hard_keymap[2];
keymap soft_keymap[4];

typedef struct {
    unsigned char code;
    unsigned short int x, y, width, height;
} soft_keypos;

soft_keypos soft_keybd_top_row[10], soft_keybd_middle_row[9], soft_keybd_middle_row_wide[10], soft_keybd_bottom_row[9], soft_keybd_space_row[5];

#ifdef TOUCH_DEBUGGING_ENABLED
int debug_touch_x, debug_touch_y;
#endif

bool hide_all_passphrase_chars;

void *guaranteed_memset(void *v,int c,size_t n);

int on_input_event(int fd, short revents, void *data);

void on_end() {
    guaranteed_memset(passphrase, 0, sizeof(passphrase));

    ev_exit();
    gr_exit();
}

void ui_init() {
    gr_init(true);
    ev_init(on_input_event, NULL);

    atexit(on_end);

    res_create_surface("softkeyboard1", &img_softkeybd[0]);
    res_create_surface("softkeyboard2", &img_softkeybd[1]);
    res_create_surface("softkeyboard3", &img_softkeybd[2]);
    res_create_surface("softkeyboard4", &img_softkeybd[3]);
}

void draw_screen() {
    unsigned int cols, i;

    gr_color(255, 255, 255, 255);
    gr_fill(0, 0, FB_WIDTH, SOFTKEYBD_TOP);

    if (img_softkeybd[soft_keybd.shift_state]) { // will be 0 iff loading failed
        gr_blit(img_softkeybd[soft_keybd.shift_state], 0, 0, 800, 260, 0, SOFTKEYBD_TOP);
    }

    gr_color(0, 0, 0, 255);
    gr_text(0, CHAR_HEIGHT, "Enter unlock phrase: ");

    gr_color(255, 0, 0, 255);
    gr_text(0, 6 * CHAR_HEIGHT, "Press Power to boot with blank ramdisk");
    if (!hide_all_passphrase_chars) {
        gr_text(0, 7 * CHAR_HEIGHT, "Press Volume Down to hide all passphrase chars");
    }

    gr_color(0, 0, 0, 255);
    cols = FB_WIDTH / CHAR_WIDTH;

	i = 0;
	if (passphrase[0]) {
		for(; i < strlen(passphrase)-1; i++) {
			gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, "*");
		}
		if (!hide_all_passphrase_chars && newchar_frame) {
			gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, &passphrase[i]); // show last char
		} else {
			gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, "*");
		}
		i++;
    }
	newchar_frame = false;

    for(; i < cols; i++) {
        gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, "_");
    }

#ifdef TOUCH_DEBUGGING_ENABLED
    gr_color(255, 0, 0, 255);
    gr_fill(debug_touch_x-32, debug_touch_y-32, debug_touch_x+32, debug_touch_y+32);
    gr_color(0, 255, 0, 255);
    gr_fill(debug_touch_x-4, debug_touch_y-4, debug_touch_x+4, debug_touch_y+4);
#endif

    gr_flip();
}

void write_centered_text(char *text, int line_offset) {
    gr_text((FB_WIDTH / 2) - ((strlen(text) / 2) * CHAR_WIDTH), (FB_HEIGHT / 2) + line_offset * CHAR_HEIGHT, text);
}

void write_modal_status_text(char *text) {
    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, FB_WIDTH, FB_HEIGHT);
    gr_color(255, 255, 255, 255);

    write_centered_text(text, 0);
    gr_flip();
}

bool unlock() {
    int status;
	pid_t pid;
	int pipefds[2];

    write_modal_status_text("Unlocking...");

	// pipe
	if (pipe(pipefds) == -1) {
		perror("pipe()");
		return false;
	}

	// fork and exec
	pid = fork();
	if (pid == -1) {
		perror("fork()");
		return false;
	} else if (pid == 0) {
		// child
		close(pipefds[1]);
		dup2(pipefds[0], 0);
		execv(cmd_passphrase_argv[0], cmd_passphrase_argv);
		perror("execv()");
		exit(255);
		return false;
	} else {
		// parent
		close(pipefds[0]);
		write(pipefds[1], passphrase, strlen(passphrase));
		close(pipefds[1]);
		if (waitpid(pid, &status, 0) == -1) {
			perror("waitpid()");
			return false;
		}
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}

	return false;
}

void boot_with_ramdisk() {
    write_modal_status_text("Booting with ramdisk...");

    system(cmd_mountramdisk);
    exit(0);
}

bool handle_passphrase_key(int keycode, keymap *keymap, int shift_state) {
    if (keycode == KEY_BACKSPACE) {
        passphrase[strlen(passphrase) - 1] = '\0';
        return true;
    } else if(keycode == KEY_ENTER || keycode == KEY_KPENTER) {
        if (unlock()) {
            write_centered_text("Success!", 1);
            gr_flip();
            exit(0);
        } else {
            write_centered_text("Failed!", 1);
            gr_flip();

            sleep(2);
            passphrase[0] = '\0';
            return true;
        }
    } else {
        char ch = keymap[shift_state].code_to_ch[keycode];
        if (ch) {
            snprintf(passphrase, sizeof(passphrase), "%s%c", passphrase, ch);
			newchar_frame = true;
            return true;
        }
    }

    return false;
}

bool on_touch(int x, int y, int down);

// Raw input events come in here. Manages the hard keyboard state, a few state changes of the app.
// Dispatches passphrase keystrokes and touch events elsewhere.
bool handle_input(struct input_event *pEvent) {
    static int partial_touch_x = -1, partial_touch_y = -1;
    static bool partial_touch_down = 0;

    if(pEvent->type == EV_KEY) {
        if (pEvent->code == KEY_LEFTSHIFT || pEvent->code == KEY_RIGHTSHIFT) {
            // shift key up/down
            hard_keybd.shift_state = !pEvent->value ? SHIFT_STATE_NONE : SHIFT_STATE_SHIFTED;
        } else if(pEvent->code == BTN_TOUCH) {
            partial_touch_down = !!pEvent->value;
        } else if(pEvent->value == 0) {
            // key release
            if (pEvent->code == KEY_VOLUMEDOWN) {
                hide_all_passphrase_chars = 1;
                return true;
            } else if (pEvent->code == KEY_POWER) {
                // Power
                boot_with_ramdisk();
            } else {
                // probably a typed character
                return handle_passphrase_key(pEvent->code, hard_keymap, hard_keybd.shift_state);
            }
        }
    } else if(pEvent->type == EV_ABS) {
        // touch screen
        if (pEvent->code == ABS_X) {
            partial_touch_x = pEvent->value;
        } else if (pEvent->code == ABS_Y) {
            partial_touch_y = pEvent->value;
        }
    } else if(pEvent->type == EV_SYN && pEvent->code == SYN_REPORT) {
        // touch screen - commit event data
        // transform touch coords since we've rotated the screen
        bool ret = on_touch(FB_WIDTH-1-(partial_touch_y / TOUCH_RESOLUTION),
                 (partial_touch_x / TOUCH_RESOLUTION), partial_touch_down);
#ifndef TOUCH_DEBUGGING_ENABLED
        return ret;
#else
        return true;
#endif
    }

    return false;
}

int find_softkey_code(int x, int y);

bool on_touch(int x, int y, int down) {
    static int down_code = 0;

#ifdef TOUCH_DEBUGGING_ENABLED
    debug_touch_x = x;
    debug_touch_y = y;
#endif

    // make keymap coords relative to keyboard, not whole screen
    y -= SOFTKEYBD_TOP;
    if (y < 0) {
        return false;
    }

    /**
     * I've currently got multiple up events coming in. Probably my fault due to
     * fast-and-loose (mis)handling of input events, but to hack around it, just ignore
     * events that seem to be coming at the wrong time.
     */
    if (down) {
        down_code = find_softkey_code(x, y);
    } else if (!down && down_code) {
        int up_code = find_softkey_code(x, y);
        if (down_code == up_code) {
            down_code = 0;
            // valid softkey press
            if (up_code == KEY_LEFTSHIFT) {
                if (soft_keybd.shift_state <= SHIFT_STATE_SHIFTED) { soft_keybd.shift_state = SHIFT_STATE_SHIFTED - soft_keybd.shift_state; }
                else { soft_keybd.shift_state = SHIFT_STATE_ALT - (soft_keybd.shift_state - SHIFT_STATE_NUMSYM); }
                return true;
            } else if (up_code == KEY_RIGHTSHIFT) {
                if (soft_keybd.shift_state <= SHIFT_STATE_SHIFTED) { soft_keybd.shift_state = SHIFT_STATE_NUMSYM; }
                else { soft_keybd.shift_state = SHIFT_STATE_NONE; }
                return true;
            } else {
                bool ret = handle_passphrase_key(up_code, soft_keymap, soft_keybd.shift_state);
                // always undo shift on soft keyboard. this may not match how real Android soft keyboard works
                soft_keybd.shift_state = SHIFT_STATE_NONE;
                return ret;
            }
        }
        down_code = 0;
    }

    return false;
}

void generate_keymappings();

int main(int argc, char **argv, __attribute__((unused))char **envp) {
    if (argc < 3) {
        printf("%s usage: ramdisk-mount-cmd passphrase-cmd-pieces...", argv[0]);
        exit(255);
    }

    setrlimit(RLIMIT_CORE, 0); // stop creation of core dumps that could have secrets

    // save configuration params
    cmd_mountramdisk = argv[1];
	cmd_passphrase_argv = &argv[2];

    // initialize keyboards
    hard_keybd.shift_state = SHIFT_STATE_NONE;
    soft_keybd.shift_state = SHIFT_STATE_NONE;
    generate_keymappings();

    ui_init();

    // set up constants
    gr_font_size(&CHAR_WIDTH, &CHAR_HEIGHT);
    FB_WIDTH = gr_fb_width();
    FB_HEIGHT = gr_fb_height();

    // show UI
    draw_screen();

    // wait forever to be exit()'d by input callbacks
    for (;;) {
        ev_wait(-1);
        ev_dispatch();
    }

    return 0;
}

int on_input_event(int fd, short revents, __attribute__((unused))void *data) {
    struct input_event ev;

    if(ev_get_input(fd, revents, &ev) != -1) {
        if (handle_input(&ev)) {
            draw_screen();
        }
    }

    return 0;
}

#define lengthof(a)  (sizeof(a)/sizeof(a[0]))

void generate_keymappings() {
    // hard keyboard
#define ADD_MAPPING(k, a, b) hard_keymap[0].code_to_ch[k] = a; hard_keymap[1].code_to_ch[k] = b;
    ADD_MAPPING(KEY_1, '1', '!');
    ADD_MAPPING(KEY_2, '2', '@');
    ADD_MAPPING(KEY_3, '3', '#');
    ADD_MAPPING(KEY_4, '4', '$');
    ADD_MAPPING(KEY_5, '5', '%');
    ADD_MAPPING(KEY_6, '6', '^');
    ADD_MAPPING(KEY_7, '7', '&');
    ADD_MAPPING(KEY_8, '8', '*');
    ADD_MAPPING(KEY_9, '9', '(');
    ADD_MAPPING(KEY_0, '0', ')');
    ADD_MAPPING(KEY_KP1, '1', '1');
    ADD_MAPPING(KEY_KP2, '2', '2');
    ADD_MAPPING(KEY_KP3, '3', '3');
    ADD_MAPPING(KEY_KP4, '4', '4');
    ADD_MAPPING(KEY_KP5, '5', '5');
    ADD_MAPPING(KEY_KP6, '6', '6');
    ADD_MAPPING(KEY_KP7, '7', '7');
    ADD_MAPPING(KEY_KP8, '8', '8');
    ADD_MAPPING(KEY_KP9, '9', '9');
    ADD_MAPPING(KEY_KP0, '0', '0');
    ADD_MAPPING(KEY_MINUS, '-', '_');
    ADD_MAPPING(KEY_EQUAL, '=', '+');
    ADD_MAPPING(KEY_TAB, '\t', '\t');
    ADD_MAPPING(KEY_Q, 'q', 'Q');
    ADD_MAPPING(KEY_W, 'w', 'W');
    ADD_MAPPING(KEY_E, 'e', 'E');
    ADD_MAPPING(KEY_R, 'r', 'R');
    ADD_MAPPING(KEY_T, 't', 'T');
    ADD_MAPPING(KEY_Y, 'y', 'Y');
    ADD_MAPPING(KEY_U, 'u', 'U');
    ADD_MAPPING(KEY_I, 'i', 'I');
    ADD_MAPPING(KEY_O, 'o', 'O');
    ADD_MAPPING(KEY_P, 'p', 'P');
    ADD_MAPPING(KEY_LEFTBRACE, '[', '{');
    ADD_MAPPING(KEY_RIGHTBRACE, ']', '}');
    ADD_MAPPING(KEY_A, 'a', 'A');
    ADD_MAPPING(KEY_S, 's', 'S');
    ADD_MAPPING(KEY_D, 'd', 'D');
    ADD_MAPPING(KEY_F, 'f', 'F');
    ADD_MAPPING(KEY_G, 'g', 'G');
    ADD_MAPPING(KEY_H, 'h', 'H');
    ADD_MAPPING(KEY_J, 'j', 'J');
    ADD_MAPPING(KEY_K, 'k', 'K');
    ADD_MAPPING(KEY_L, 'l', 'L');
    ADD_MAPPING(KEY_SEMICOLON, ';', ':');
    ADD_MAPPING(KEY_APOSTROPHE, '\'', '"');
    ADD_MAPPING(KEY_GRAVE, '`', '~');
    ADD_MAPPING(KEY_BACKSLASH, '\\', '|');
    ADD_MAPPING(KEY_Z, 'z', 'Z');
    ADD_MAPPING(KEY_X, 'x', 'X');
    ADD_MAPPING(KEY_C, 'c', 'C');
    ADD_MAPPING(KEY_V, 'v', 'V');
    ADD_MAPPING(KEY_B, 'b', 'B');
    ADD_MAPPING(KEY_N, 'n', 'N');
    ADD_MAPPING(KEY_M, 'm', 'M');
    ADD_MAPPING(KEY_COMMA, ',', '<');
    ADD_MAPPING(KEY_DOT, '.', '>');
    ADD_MAPPING(KEY_SLASH, '/', '?');
    ADD_MAPPING(KEY_KPASTERISK, '*', '*');
    ADD_MAPPING(KEY_SPACE, ' ', ' ');
    ADD_MAPPING(KEY_KPMINUS, '-', '-');
    ADD_MAPPING(KEY_KPPLUS, '+', '+');
    ADD_MAPPING(KEY_KPDOT, '.', '.');
    ADD_MAPPING(KEY_KPSLASH, '/', '/');
    ADD_MAPPING(KEY_KPEQUAL, '=', '=');
    ADD_MAPPING(KEY_KPCOMMA, ',', ',');
#undef ADD_MAPPING

    // soft keyboard
#define ADD_MAPPING(k, a, b, c, d) soft_keymap[0].code_to_ch[k] = a; soft_keymap[1].code_to_ch[k] = b; soft_keymap[2].code_to_ch[k] = c; soft_keymap[3].code_to_ch[k] = d;
    ADD_MAPPING(KEY_Q, 'q', 'Q', '1', '~');
    ADD_MAPPING(KEY_W, 'w', 'W', '2', '`');
    ADD_MAPPING(KEY_E, 'e', 'E', '3', '|');
    ADD_MAPPING(KEY_R, 'r', 'R', '4', 0);
    ADD_MAPPING(KEY_T, 't', 'T', '5', 0);
    ADD_MAPPING(KEY_Y, 'y', 'Y', '6', 0);
    ADD_MAPPING(KEY_U, 'u', 'U', '7', 0);
    ADD_MAPPING(KEY_I, 'i', 'I', '8', 0);
    ADD_MAPPING(KEY_O, 'o', 'O', '9', '{');
    ADD_MAPPING(KEY_P, 'p', 'P', '0', '}');
    ADD_MAPPING(KEY_A, 'a', 'A', '@', '\t');
    ADD_MAPPING(KEY_S, 's', 'S', '#', 0);
    ADD_MAPPING(KEY_D, 'd', 'D', '$', 0);
    ADD_MAPPING(KEY_F, 'f', 'F', '%', 0);
    ADD_MAPPING(KEY_G, 'g', 'G', '&', 0);
    ADD_MAPPING(KEY_H, 'h', 'H', '*', '^');
    ADD_MAPPING(KEY_J, 'j', 'J', '-', '_');
    ADD_MAPPING(KEY_K, 'k', 'K', '+', '=');
    ADD_MAPPING(KEY_L, 'l', 'L', '(', '[');
    ADD_MAPPING(KEY_SEMICOLON, 0, 0, ')', ']');
    ADD_MAPPING(KEY_Z, 'z', 'Z', '!', 0);
    ADD_MAPPING(KEY_X, 'x', 'X', '"', 0);
    ADD_MAPPING(KEY_C, 'c', 'C', '\'', 0);
    ADD_MAPPING(KEY_V, 'v', 'V', ':', 0);
    ADD_MAPPING(KEY_B, 'b', 'B', ';', '\\');
    ADD_MAPPING(KEY_N, 'n', 'N', '/', '<');
    ADD_MAPPING(KEY_M, 'm', 'M', '?', '>');
    ADD_MAPPING(KEY_COMMA, ',', ',', ',', 0);
    ADD_MAPPING(KEY_SPACE, ' ', ' ', ' ', ' ');
    ADD_MAPPING(KEY_DOT, '.', '.', '.', 0);
#undef ADD_MAPPING

    // soft key coordinates
    unsigned int i, x;
#define ADD_MAPPING(a, i, _y, _x, h, w, _code) assert(i < lengthof(a)); a[i].y = _y; a[i].x = _x; a[i].width = w; a[i].height = h; a[i].code = _code;
    for (i = 0, x = 4; i < lengthof(soft_keybd_top_row); x += 80, i++) { ADD_MAPPING(soft_keybd_top_row, i, 2, x, 53, 72, KEY_Q + i); }

    for (i = 0, x = 44; i < lengthof(soft_keybd_middle_row); x += 80, i++) { ADD_MAPPING(soft_keybd_middle_row, i, 70, x, 53, 72, KEY_A + i); }

    for (i = 0, x = 4; x < lengthof(soft_keybd_middle_row_wide); x += 80, i++) { ADD_MAPPING(soft_keybd_middle_row_wide, i, 70, x, 53, 72, KEY_A + i); }

    ADD_MAPPING(soft_keybd_bottom_row, 0, 138, 4, 53, 112, KEY_LEFTSHIFT);
    for (i = 1, x = 124; i < lengthof(soft_keybd_bottom_row); x += 80, i++) { ADD_MAPPING(soft_keybd_bottom_row, i, 138, x, 53, 72, KEY_Z + i - 1); }
    ADD_MAPPING(soft_keybd_bottom_row, i-1, 138, 684, 53, 112, KEY_BACKSPACE);

    ADD_MAPPING(soft_keybd_space_row, 0, 206, 4, 53, 112, KEY_RIGHTSHIFT);
    ADD_MAPPING(soft_keybd_space_row, 1, 206, 204, 53, 72, KEY_COMMA);
    ADD_MAPPING(soft_keybd_space_row, 2, 206, 284, 53, 232, KEY_SPACE);
    ADD_MAPPING(soft_keybd_space_row, 3, 206, 524, 53, 72, KEY_DOT);
    ADD_MAPPING(soft_keybd_space_row, 4, 206, 604, 53, 192, KEY_ENTER);
#undef ADD_MAPPING
}

int find_softkey_code(int x, int y) {
    unsigned int i;

#define SEARCH(arr) \
    for (i = 0; i < lengthof(arr); i++) { \
        if (x >= arr[i].x && y >= arr[i].y && x < (arr[i].x+arr[i].width) && y < (arr[i].y+arr[i].height)) { \
            return arr[i].code; \
        } \
        if (arr[i].x > x) { \
            break; /* skip the rest since keys are arranged left-ro-right */ \
        } \
    }

    SEARCH(soft_keybd_top_row);
    if (soft_keybd.shift_state <= SHIFT_STATE_SHIFTED) {
        SEARCH(soft_keybd_middle_row);
    } else {
        SEARCH(soft_keybd_middle_row_wide);
    }

    SEARCH(soft_keybd_bottom_row);
    SEARCH(soft_keybd_space_row);

    return 0;
#undef SEARCH
}

// http://www.dwheeler.com/secure-programs/Secure-Programs-HOWTO/protect-secrets.html
void *guaranteed_memset(void *v,int c,size_t n) { volatile char *p=v; while (n--) *p++=c; return v; }
