#ifndef WMBRIGHT_MMKEYS_H
#define WMBRIGHT_MMKEYS_H


/* Global Configuration */
extern struct multimedia_keys {
	KeyCode brightness_up;
	KeyCode brightness_down;
} mmkeys;

/* Grab the multimedia keys */
void mmkey_install(Display *display);

#endif	/* WMIX_MMKEYS_H */
