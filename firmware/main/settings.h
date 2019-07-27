#pragma once

#include "applications.h"

#define S_MAX_KEY_LEN (16)

typedef enum {ST_SEP, ST_TEXT, ST_INT, ST_IP} setting_type;

typedef struct setting_s {
	struct setting_s *next;
	struct setting_s *prev;
	char id[S_MAX_KEY_LEN]; //KEY as used in the NVS library
	char *label; //Label as displayed on the screen
	char *value; //Value
	int value_len; //maximum length of value
	setting_type type; //Type of the setting
	int min; //Minimum value if type=ST_INT
	int max; //Maximum value if type=ST_INT
	int num; //Number of setting (0-based)
} setting_t;


void settings_app(const io_type_t *io);
void settings_init();
char *get_setting(const char *id);
