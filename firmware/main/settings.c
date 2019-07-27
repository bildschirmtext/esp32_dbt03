#include "settings.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "nvs_flash.h"
#include "nvs.h"



#define FIRST_LINE (8)
#define VALUE_COLUMN (2)

static setting_t *settings=NULL;

setting_t *append_setting(setting_t *old, setting_t *new)
{
	if (new==NULL) return old;
	if (old==NULL) return new;
	if (old->next!=NULL) return append_setting(old->next, new);
	old->next=new;
	new->prev=old;
	new->num=old->num+1;
	return new;
}

char *copy_string(const char *src)
{
	if (src==NULL) return NULL;
	size_t len=strlen(src)+1;
	char *c=malloc(len);
	if (c==NULL) return NULL;
	memset(c,0,len);
	strncpy(c, src, len);
	return c;
}

setting_t *create_setting(const char *id, const char *label, const int value_len, const setting_type type, const int min, const int max)
{
	setting_t *new=malloc(sizeof(setting_t));
	if (new==NULL) return NULL;
	memset(new, 0, sizeof(setting_t));
	strncpy(new->id, id, S_MAX_KEY_LEN);
	new->label=copy_string(label);
	new->value_len=value_len;
	new->value=malloc(value_len+1);
	if (new->value!=NULL) memset(new->value, 0, value_len+1);
	new->type=type;
	new->min=min;
	new->max=max;
	new->num=0;
	return new;
}


void add_setting(const char *id, const char *label, const int value_len, const setting_type type, const int min, const int max)
{
	setting_t *new=create_setting(id, label, value_len, type, min, max);
	settings=append_setting(settings, new);
}

setting_t *first_setting(setting_t *s)
{
	if (s==NULL) return NULL;
	if (s->prev!=NULL) return first_setting(s->prev);
	return s;
}


void init_settings()
{
	add_setting("SSID",	"Netzwerkname",		33,	ST_TEXT,	0,	0);
	add_setting("PWD",	"WLAN-Passwort",	33,	ST_TEXT,	0,	0);
	add_setting("SRVIP",	"Server-IP",		33,	ST_IP,		0,	0);
	add_setting("SVRPORT",	"Server-Port",		6,	ST_INT,		1,	65534);
}

void print_svalue(const io_type_t *io, const setting_t *s)
{
	int end=0;
	int n;
	for (n=0; n<s->value_len; n++) {
		if (s->value[n]==0) end=1;
		if (end!=1) io->out(s->value[n]);
		else io->out(' ');
	}
}

void print_setting(const io_type_t *io, const setting_t *s)
{
	if (s==NULL) return;
	app_gotoxy(io, 0, s->num*2+FIRST_LINE);
	app_set_bg_colour(io, 8+1);
	app_write_string(io, s->label);	
	io->out(0x18); //Delete till end of line
	app_gotoxy(io, VALUE_COLUMN, s->num*2+FIRST_LINE+1);
	app_set_bg_colour(io, 0);
	print_svalue(io, s);
	app_set_bg_colour(io, 8+1);
	io->out(0x18); //Delete till end of line
}

void print_settings(const io_type_t *io, const setting_t *s)
{
	if (s==NULL) return;
	print_setting(io, s);
	print_settings(io, s->next);
}

int find_longest_label(const setting_t *s, const int len)
{
	if (s==NULL) return len;
	if (strlen(s->label)>len) return find_longest_label(s->next, strlen(s->label));
	return find_longest_label(s->next, len);
}


//Replaces the spaces in a setting with NULL characters
void clean_setting(setting_t *s)
{
	int n;
	for (n=s->value_len-1; n>=0; n--) {
		if (s->value[n]!=' ') break;
		s->value[n]=0;
	}
}


// Edits one setting
// returns -2 on error
// 1 if we shall go to the next field
// -1 if we shall go back
int edit_setting(const io_type_t *io, setting_t *s)
{
	if (io==NULL) return -2;
	if (s==NULL) return -2;
	io->out(0x1a); //Make the terminal talk
	io->out(0x11); //Turn on cursor
	int line=FIRST_LINE+s->num*2+1;
	app_gotoxy(io, VALUE_COLUMN, line);
	app_set_bg_colour(io, 0);
	int position=0;
	while (1==1) {
		app_gotoxy(io, VALUE_COLUMN+position, line);
		if (s->value[position]!=0) {
			io->out(s->value[position]); //Print character under cursor
			io->out(0x08); //Go one step back
		}
		int c=io->in(1);
		if (c<0) return -2;
		if (c==0x08) position=position-1; //left
		if (c==0x09) position=position+1; //right
		if (c==0x0a) return 1; //down
		if (c==0x0b) return -1; //up
		if (c==0x1c) return 1; //# Terminator
		if (position<0) return -1; //to far to the left, go up
		if (position>=s->value_len) return 1; //to far to the right, go down
		int accept=0;
		if ( (c>='0') && (c<='9') ) accept=1;
		if ( (s->type==ST_IP) && (c==0x13) ) {accept=1; c='.';}; //Use * for . in IP-Addresses
		if ( (s->type==ST_TEXT) && (c>=0x20) && (c<0x80) ) accept=1;
		if (accept==1) {
			s->value[position]=c;
			io->out(c);
			position=position+1;
		}
	}
	return 0; //We should never get here
}

//Remove spaces at the end of the settings value
void trim_setting(setting_t *s)
{
	int len=strlen(s->value);
	if (len==0) return;
	while (s->value[len-1]==' ') {
		s->value[len-1]=0;
		len=len-1;
		if (len<=1) return;
	}
}

void edit_settings(const io_type_t *io, setting_t *s)
{
	if (s==NULL) return;
	int res=edit_setting(io, s);
	trim_setting(s);
	if (res==-2) return; //Error, exit the configuration
	if (res==1) return edit_settings(io, s->next);
	if (res==-1) return edit_settings(io, s->prev);
}

void read_setting_from_nvs(nvs_handle_t handle, setting_t *s)
{
	if (s==NULL) return;
	printf("Reading Setting %s=", s->id);
	size_t len=s->value_len;
	nvs_get_str(handle, s->id, s->value, &len);
	printf("%s\n", s->value);
	return read_setting_from_nvs(handle, s->next);
}


void read_settings_from_nvs(setting_t *first)
{
	printf("Opening Non-Volatile Storage (NVS) handle... ");
	nvs_handle_t my_handle;
	int err = nvs_open("storage", NVS_READWRITE, &my_handle);
	printf("err=%d\n", err);
	read_setting_from_nvs(my_handle, first);
	nvs_close(my_handle);
}


void write_setting_to_nvs(nvs_handle_t handle, setting_t *s)
{
	if (s==NULL) return;
	printf("Writing Setting %s=%s\n", s->id, s->value);
	nvs_set_str(handle, s->id, s->value);
	return write_setting_to_nvs(handle, s->next);
}


void write_settings_to_nvs(setting_t *first)
{
	printf("Opening Non-Volatile Storage (NVS) handle... ");
	nvs_handle_t my_handle;
	int err = nvs_open("storage", NVS_READWRITE, &my_handle);
	printf("err=%d\n", err);
	write_setting_to_nvs(my_handle, first);
	nvs_commit(my_handle);
	nvs_close(my_handle);
}

void settings_app(const io_type_t *io)
{
	app_init_screen(io);
	app_set_screen_colour(io, 8+1);
	app_gotoxy(io, 0,4);
	app_set_line_colour(io, 1);
	app_write_string(io, "\x8d Einstellungen:\x8c");
	setting_t *f=first_setting(settings);
	print_settings(io, f);
	edit_settings(io, f);
	write_settings_to_nvs(f);
}

char *get_setting_(const char *id, setting_t *s)
{
	printf("get_setting_ s=%p\n", s);
	if (s==NULL) return NULL;
	printf("get_setting_ id=%s s->id=%s\n", id, s->id);
	if (strcmp(id,s->id)==0) return s->value;
	return get_setting_(id, s->next);
}

char *get_setting(const char *id)
{
	setting_t *f=first_setting(settings);
	return get_setting_(id, f);
}

void settings_init()
{
	init_settings();
	setting_t *f=first_setting(settings);
	read_settings_from_nvs(f);
}
