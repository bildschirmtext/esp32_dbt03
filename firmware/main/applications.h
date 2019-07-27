#pragma once

#include "interfaces.h"


int application(io_type_t *io);


void app_gotoxy(const io_type_t *io, const int x, const int y);
void app_write_string(const io_type_t *io, const char *s);
void app_status_string(const io_type_t *io, const char *s);
void app_init_screen(const io_type_t *io);

void app_set_screen_colour(const io_type_t *io, const int colour);
void app_set_palette(const io_type_t *io, const int palette);
void app_set_bg_colour(const io_type_t *io, const int colour);
void app_set_line_colour(const io_type_t *io, const int colour);


void terminal_task(void *pvParameters);
