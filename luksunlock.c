#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>

#include "minui/minui.h"

#define CHAR_WIDTH		10
#define CHAR_HEIGHT		18

#define CHAR_START		0x20
#define CHAR_END		0x7E

struct {
	unsigned char key;
	int xpos;
	int ypos;
	int selected;
} keys[CHAR_END - CHAR_START];

struct input_event keyqueue[2048];

char passphrase[1024];

pthread_mutex_t keymutex;
unsigned int sp = 0;

gr_surface background;
int res, current = 0;

char *cmd_cryptsetup;
char *cmd_mount;
char *dev_sdcard;
char *dev_userdata;
char *mapname_sdcard;
char *mapname_userdata;
int userdata_is_whyaffs = 0;

struct {
	int shift_state;
} keybd;

static struct {
	unsigned char ch;
	unsigned char shift_ch;
} keymapping[255];

char *escape_input(char *str) {
	size_t i, j = 0;
	char *new = malloc(sizeof(char) * (strlen(str) * 2 + 1));

	for(i = 0; i < strlen(str); i++) {
		if(!(((str[i] >= 'A') && (str[i] <= 'Z')) ||
		((str[i] >= 'a') && (str[i] <= 'z')) ||
		((str[i] >= '0') && (str[i] <= '9')) )) {
			new[j] = '\\';
			j++;
		}
		new[j] = str[i];
		j++;
	}
	new[j] = '\0';

	return new;
}

void draw_keygrid() {
	size_t i;
	char keybuf[2];

	for(i = 0; i < (CHAR_END - CHAR_START); i++) {
		sprintf(keybuf, "%c", keys[i].key);

		if(keys[i].selected == 1) {
			gr_color(255, 0, 0, 255);
			gr_fill(keys[i].xpos, keys[i].ypos - CHAR_HEIGHT, keys[i].xpos + CHAR_WIDTH, keys[i].ypos);
			gr_color(255, 255, 255, 255);
		}
		else
			gr_color(0, 0, 0, 255);

		gr_text(keys[i].xpos, keys[i].ypos, keybuf);
	}
}

static void *input_thread() {
	int rel_sum = 0;

	for(;;) {
		struct  input_event ev;

		do {
			ev_get(&ev, 0);

			switch(ev.type) {
				case EV_SYN:
					continue;
				case EV_REL:
					rel_sum += ev.value;
					break;

				default:
					rel_sum = 0;
			}

			if(rel_sum > 4 || rel_sum < -4)
				break;

		} while(ev.type != EV_KEY || ev.code > KEY_MAX);

		rel_sum = 0;

		// Add the key to the fifo
		pthread_mutex_lock(&keymutex);
		if(sp < (sizeof(keyqueue) / sizeof(struct input_event)))
			sp++;

		keyqueue[sp] = ev;
		pthread_mutex_unlock(&keymutex);
	}

	return 0;
}

void ui_init(void) {
	gr_init();
	ev_init();

	// Generate bitmap from /system/res/padlock.png ( you can change the path in minui/resources.c)
	res_create_surface("padlock", &background);
}

void draw_screen() {
	// This probably only looks good in HTC Wildfire resolution
	int bgwidth, bgheight, bgxpos, bgypos, i, cols;

	gr_color(255, 255, 255, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());

	bgwidth = gr_get_width(background);
	bgheight = gr_get_height(background);
	bgxpos = (gr_fb_width() - gr_get_width(background)) / 2;
	bgypos = (gr_fb_height() - gr_get_height(background)) / 2;

	gr_blit(background, 0, 0, bgwidth, bgheight, bgxpos, bgypos);

	gr_color(0, 0, 0, 255);
	gr_text(0, CHAR_HEIGHT, "Enter unlock phrase: ");

	cols = gr_fb_width() / CHAR_WIDTH;

	for(i = 0; i < (int) strlen(passphrase); i++) 
		gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, "*");

	for(; i < cols - 1; i++)
		gr_text(i * CHAR_WIDTH, CHAR_HEIGHT * 2, "_");

	gr_text(0, gr_fb_height() - (CHAR_HEIGHT * 2), "Press Volup to unlock");
	gr_text(0, gr_fb_height() - CHAR_HEIGHT, "Press Voldown to erase");
	gr_text(0, gr_fb_height(), "Press Power to boot with blank ramdisk");

	draw_keygrid();
	gr_flip();
}

void generate_keygrid() {
	int xpos, ypos;
	char key;
	int i;

	xpos = 0;
	ypos = CHAR_HEIGHT * 4;

	for(i = 0, key = CHAR_START; key < CHAR_END; key++, i++, xpos += (CHAR_WIDTH * 3)) {
		if(xpos >= gr_fb_width() - CHAR_WIDTH) {
			ypos += CHAR_HEIGHT;

			xpos = 0;
		}

		keys[i].key = key;
		keys[i].xpos = xpos;
		keys[i].ypos = ypos;
		keys[i].selected = 0;
	}

	keys[current].selected = 1;
}

void write_centered_text(char *text, int line_offset) {
	gr_text((gr_fb_width() / 2) - ((strlen(text) / 2) * CHAR_WIDTH), (gr_fb_height() / 2) + line_offset * CHAR_HEIGHT, text);
}

void write_modal_status_text(char *text) {
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_color(255, 255, 255, 255);

	write_centered_text(text, 0);
	gr_flip();
}

int check_file_exists(char *name) {
	struct stat buf;

	return stat(name, &buf) != -1 || errno != ENOENT;
}

int unlock() {
	char buffer[2048];

	write_modal_status_text("Unlocking...");

	// sdcard
	snprintf(buffer, sizeof(buffer), "echo %s | %s luksOpen %s %s", escape_input(passphrase), cmd_cryptsetup, dev_sdcard, mapname_sdcard);
	system(buffer);

	snprintf(buffer, sizeof(buffer), "/dev/mapper/%s", mapname_sdcard);
	if(!check_file_exists(buffer)) {
		return 0;
	}

	// userdata
	if (!userdata_is_whyaffs) {
		snprintf(buffer, sizeof(buffer), "echo %s | %s luksOpen %s %s", escape_input(passphrase), cmd_cryptsetup, dev_userdata, mapname_userdata);
		system(buffer);

		snprintf(buffer, sizeof(buffer), "/dev/mapper/%s", mapname_userdata);
		if(!check_file_exists(buffer)) {
			return 0;
		}
	} else {
		snprintf(buffer, sizeof(buffer), "%s -t yaffs2 -o nosuid,nodev,relatime,unlock_encrypted=%s %s %s", cmd_mount, escape_input(passphrase), dev_userdata, mapname_userdata);
		if (system(buffer) != 0) {
			return 0;
		}
	}

	return 1;
}

void boot_with_ramdisk() {
	char buffer[512];

	write_modal_status_text("Booting with ramdisk...");

	snprintf(buffer, sizeof(buffer), "%s -t tmpfs -o nosuid,nodev tmpfs /data", cmd_mount);
	system(buffer);

	exit(0);
}

void handle_input(struct input_event event) {
	int cols;

	cols = gr_fb_width() / (CHAR_WIDTH * 3);

	// Joystick
	if(event.type == EV_REL && event.value != 0) {
		keys[current].selected = 0;

		// down or up
		if(event.code == REL_Y) {
			if(event.value > 0) {
				if(current + cols < (CHAR_END - CHAR_START)) {
					current += cols;
				}
			} else {
				if(current - cols > 0) {
					current -= cols;
				}
			}
		} else if(event.code == REL_X) {
			// left or right
			if(event.value > 0) {
				if(current < (CHAR_END - CHAR_START) - 1) {
					current++;
				}
			} else {
				if(current > 0) {
					current--;
				}
			}
		}

		keys[current].selected = 1;
	} else if(event.type == EV_KEY) {
		if(event.code == BTN_MOUSE) {
			// Pressed joystick
			snprintf(passphrase, sizeof(passphrase), "%s%c", passphrase, keys[current].key);
		} else if(event.code == KEY_VOLUMEDOWN || event.code == KEY_BACKSPACE) {
			// erase
			passphrase[strlen(passphrase) - 1] = '\0';
		} else if(event.code == KEY_VOLUMEUP || event.code == KEY_ENTER || event.code == KEY_KPENTER) {
			// enter
			if (unlock()) {
				write_centered_text("Success!", 1);
				gr_flip();
				exit(0);
			} else {
				write_centered_text("Failed!", 1);
				gr_flip();

				sleep(2);
				passphrase[0] = '\0';
			}

		} else if (event.code == KEY_POWER) {
			// Power
			boot_with_ramdisk();
		} else {
			// maybe a keyboard key, map to char
			if (keymapping[event.code].ch) {
				snprintf(passphrase, sizeof(passphrase), "%s%c", passphrase, !keybd.shift_state ? keymapping[event.code].ch : keymapping[event.code].shift_ch);
			}
		}
	}

	draw_screen();
}

void generate_keymapping();

int main(int argc, char **argv, char **envp) {
	struct input_event event;
	pthread_t t;
	unsigned int i;

	// initialize keyboard
	keybd.shift_state = 0;
	generate_keymapping();

	ui_init();

	if (argc != 7 && argc != 8) {
		printf("%s not called correctly", argv[0]);
		exit(255);
	}

	// save configuration params
	cmd_cryptsetup = argv[1];
	cmd_mount = argv[2];
	dev_sdcard = argv[3];
	mapname_sdcard = argv[4];
	dev_userdata = argv[5];
	mapname_userdata = argv[6];
	if (argc == 8 && 0 == strcasecmp("whisperyaffs", argv[7])) {
		userdata_is_whyaffs = 1;
	}

	// show UI
	generate_keygrid();
	draw_screen();

	pthread_create(&t, NULL, input_thread, NULL);
	pthread_mutex_init(&keymutex, NULL);

	for(;;) {
		pthread_mutex_lock(&keymutex);

		if(sp > 0) {
			for(i = 0; i < sp; i++)
				keyqueue[i] = keyqueue[i + 1];

			event = keyqueue[0];
			sp--;

			pthread_mutex_unlock(&keymutex);
		} else {
			pthread_mutex_unlock(&keymutex);
			continue;
		}

		switch(event.type) {
			case(EV_KEY):
				if(event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT) {
					keybd.shift_state = event.value;
				} else if(event.value == 0) { // release
					handle_input(event);
				}
				break;
			case(EV_REL):
				handle_input(event);
				break;
			default:
				break;
		}
	}

	return 0;
}

void generate_keymapping() {
#define ADD_MAPPING(x, y, z) keymapping[x].ch = y; keymapping[x].shift_ch = z
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
}
