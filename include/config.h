#ifndef WMBRIGHT_CONFIG_H
#define WMBRIGHT_CONFIG_H

#define EXCLUDE_MAX_COUNT 100

/* Global Configuration */
extern struct _Config {
	char        *file;				/* full path to config file name */
	char        *display_name;		/* X Display to connect to */
	char        *mixer_device;		/* device file to use for controlling Mixer volumes */

	int api;                        /* Sound API (0 = ALSA, 1 = OSS) */
	unsigned int verbose    : 1;	/* be Verbose when starting */
	unsigned int osd        : 1;	/* show OSD? */
	unsigned int mousewheel : 1;	/* mousewheel enabled? */
	unsigned int scrolltext : 1;	/* scroll channel names? */
	unsigned int mmkeys     : 1;	/* grab multimedia keys for volume control */

	unsigned int wheel_button_up;	/* up button */
	unsigned int wheel_button_down;	/* down button */

	float        scrollstep;		/* scroll mouse step adjustment */
	char        *osd_color;			/* osd color */
	char        *osd_monitor_name;		/* monitor name to display osd on */
	int          osd_monitor_number;	/* monitor number to display osd on */

	char        *exclude_channel[EXCLUDE_MAX_COUNT + 1];	/* Devices to exclude from GUI's list */
} config;

/* Default color for OSD */
extern const char default_osd_color[];

/* Current version of WMBright */
#define VERSION "0.1"

/* Sets the default values in the config */
void config_init(void);

/* Release memory associated with configuration (this concern only stuff needed during startup) */
void config_release(void);

/* Sets configuration from command line */
void parse_cli_options(int argc, char **argv);

/* Read configuration from file */
void config_read(void);

/* Set some default values based on configuration choices */
void config_set_defaults();

#endif	/* WMBRIGHT_CONFIG_H */
