/**
 * \file hook/x11.c
 * \brief libX11 wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup hook
 *  \{
 * \defgroup x11 libX11 wrapper
 *  \{
 */

#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>

#include "lib.h"

struct x11_private_s {
	glc_t *glc;

	void *libX11_handle;
	int (*XNextEvent)(Display *, XEvent *);
	int (*XPeekEvent)(Display *, XEvent *);
	int (*XWindowEvent)(Display *, Window, long, XEvent *);
	int (*XMaskEvent)(Display *, long, XEvent *);
	Bool (*XCheckWindowEvent)(Display *, Window, long, XEvent *);
	Bool (*XCheckMaskEvent)(Display *, long, XEvent *);
	Bool (*XCheckTypedEvent)(Display *, long, XEvent *);
	Bool (*XCheckTypedWindowEvent)(Display *, Window, int, XEvent *);

	int (*XIfEvent)(Display *, XEvent *, Bool (*)(), XPointer);
	Bool (*XCheckIfEvent)(Display *, XEvent *, Bool (*)(), XPointer);
	int (*XPeekIfEvent)(Display *, XEvent *, Bool (*)(), XPointer);

	void *libXxf86vm_handle;
	Bool (*XF86VidModeSetGamma)(Display *, int, XF86VidModeGamma *);

	/* capture start/stop */
	unsigned int capture_key_mask;
	KeySym capture_key;

	/* start capturing to different file */
	unsigned int reload_key_mask;
	KeySym reload_key;

	Time last_event_time;
};

#define X11_KEY_CTRL               1
#define X11_KEY_ALT                2
#define X11_KEY_SHIFT              4

__PRIVATE struct x11_private_s x11;
__PRIVATE void get_real_x11();
__PRIVATE void x11_event(Display *dpy, XEvent *event);
__PRIVATE int x11_parse_key(const char *str, KeySym *key, unsigned int *mask);
__PRIVATE int x11_match_key(Display *dpy, XEvent *event, KeySym key, unsigned int mask);

int x11_init(glc_t *glc)
{
	x11.glc = glc;

	get_real_x11();

	if (getenv("GLC_HOTKEY")) {
		if (x11_parse_key(getenv("GLC_HOTKEY"), &x11.capture_key, &x11.capture_key_mask)) {
			glc_log(x11.glc, GLC_WARNING, "x11",
				 "invalid hotkey '%s'", getenv("GLC_HOTKEY"));
			glc_log(x11.glc, GLC_WARNING, "x11",
				 "using default <Shift>F8\n");
			x11.capture_key_mask = X11_KEY_SHIFT;
			x11.capture_key = XK_F8;
		}
	} else {
		x11.capture_key_mask = X11_KEY_SHIFT;
		x11.capture_key = XK_F8;
	}

	if (getenv("GLC_RELOAD_HOTKEY")) {
		if (x11_parse_key(getenv("GLC_RELOAD_HOTKEY"), &x11.reload_key, &x11.reload_key_mask)) {
			glc_log(x11.glc, GLC_WARNING, "x11",
				 "invalid reload hotkey '%s'", getenv("GLC_HOTKEY"));
			glc_log(x11.glc, GLC_WARNING, "x11",
				 "using default <Shift>F9\n");
			x11.reload_key_mask = X11_KEY_SHIFT;
			x11.reload_key = XK_F9;
		}
	} else {
		x11.reload_key_mask = X11_KEY_SHIFT;
		x11.reload_key = XK_F9;
	}

	return 0;
}

int x11_parse_key(const char *str, KeySym *key, unsigned int *mask)
{
	/** \todo better parsing */
	int c, s;
	*mask = 0;
	c = s = 0;
	while (str[c] != '\0') {
		if (str[c] == '<')
			s = c;
		else if (str[c] == '>') {
			if (!strncmp(&str[s], "<Ctrl>", c - s))
				*mask |= X11_KEY_CTRL;
			else if (!strncmp(&str[s], "<Shift>", c - s))
				*mask |= X11_KEY_SHIFT;
			else
				return EINVAL;
			s = c + 1;
		}
		c++;
	}

	*key = XStringToKeysym(&str[s]);

	if (!(*key))
		return EINVAL;
	return 0;
}

int x11_close()
{
	return 0;
}

int x11_match_key(Display *dpy, XEvent *event, KeySym key, unsigned int mask)
{
	if (event->xkey.keycode != XKeysymToKeycode(dpy, key))
		return 0;

	if ((mask & X11_KEY_CTRL) && (!(event->xkey.state & ControlMask)))
		return 0;

	if ((mask & X11_KEY_SHIFT) && (!(event->xkey.state & ShiftMask)))
		return 0;

	return 1;
}

void x11_event(Display *dpy, XEvent *event)
{
	if (!event)
		return;

	if (event->type == KeyPress) {
		if (event->xkey.time == x11.last_event_time)
			return; /* handle duplicates */
	
		if (x11_match_key(dpy, event, x11.capture_key, x11.capture_key_mask)) {
			if (lib.flags & LIB_CAPTURING) /* stop */
				stop_capture();
			else /* start */
				start_capture();
		} else if (x11_match_key(dpy, event, x11.reload_key, x11.reload_key_mask)) {
			if (lib.flags & LIB_CAPTURING) /* just stop */
				stop_capture();
			else { /* reload and start */
				increment_capture();
				reload_stream();
				start_capture();
			}
		}

		x11.last_event_time = event->xkey.time;
	}
}

void get_real_x11()
{
	if (!lib.dlopen)
		get_real_dlsym();

	x11.libX11_handle = lib.dlopen("libX11.so.6", RTLD_LAZY);
	if (!x11.libX11_handle)
		goto err;

	x11.XNextEvent =
	  (int (*)(Display *, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XNextEvent");
	if (!x11.XNextEvent)
		goto err;
	x11.XPeekEvent =
	  (int (*)(Display *, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XPeekEvent");
	if (!x11.XPeekEvent)
		goto err;
	x11.XWindowEvent =
	  (int (*)(Display *, Window, long, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XWindowEvent");
	if (!x11.XWindowEvent)
		goto err;
	x11.XMaskEvent =
	  (int (*)(Display *, long, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XMaskEvent");
	if (!x11.XMaskEvent)
		goto err;
	x11.XCheckWindowEvent =
	  (Bool (*)(Display *, Window, long, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XCheckWindowEvent");
	if (!x11.XCheckWindowEvent)
		goto err;
	x11.XCheckMaskEvent =
	  (Bool (*)(Display *, long, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XCheckMaskEvent");
	if (!x11.XCheckMaskEvent)
		goto err;
	x11.XCheckTypedEvent =
	  (Bool (*)(Display *, long, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XCheckTypedEvent");
	if (!x11.XCheckTypedEvent)
		goto err;
	x11.XCheckTypedWindowEvent =
	  (Bool (*)(Display *, Window, int, XEvent *))
	    lib.dlsym(x11.libX11_handle, "XCheckTypedWindowEvent");
	if (!x11.XCheckTypedWindowEvent)
		goto err;
	x11.XIfEvent =
	  (int (*)(Display *, XEvent *, Bool (*)(), XPointer))
	    lib.dlsym(x11.libX11_handle, "XIfEvent");
	if (!x11.XIfEvent)
		goto err;
	x11.XCheckIfEvent =
	  (Bool (*)(Display *, XEvent *, Bool (*)(), XPointer))
	    lib.dlsym(x11.libX11_handle, "XCheckIfEvent");
	if (!x11.XCheckIfEvent)
		goto err;
	x11.XPeekIfEvent =
	  (int (*)(Display *, XEvent *, Bool (*)(), XPointer))
	    lib.dlsym(x11.libX11_handle, "XPeekIfEvent");
	if (!x11.XPeekIfEvent)
		goto err;

	x11.XF86VidModeSetGamma = NULL;
	x11.libXxf86vm_handle = lib.dlopen("libXxf86vm.so.1", RTLD_LAZY);
	if (x11.libXxf86vm_handle) {
		x11.XF86VidModeSetGamma =
		  (Bool (*)(Display *, int, XF86VidModeGamma *))
		    lib.dlsym(x11.libX11_handle, "XF86VidModeGamma");
	}

	return;
err:
	fprintf(stderr, "(glc) can't get real X11\n");
	exit(1);
}

__PUBLIC int XNextEvent(Display *display, XEvent *event_return)
{
	return __x11_XNextEvent(display, event_return);
}

int __x11_XNextEvent(Display *display, XEvent *event_return)
{
	INIT_GLC
	int ret = x11.XNextEvent(display, event_return);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC int XPeekEvent(Display *display, XEvent *event_return)
{
	return __x11_XPeekEvent(display, event_return);
}

int __x11_XPeekEvent(Display *display, XEvent *event_return)
{
	INIT_GLC
	int ret = x11.XPeekEvent(display, event_return);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC int XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
	return __x11_XWindowEvent(display, w, event_mask, event_return);
}

int __x11_XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
	INIT_GLC
	int ret = x11.XWindowEvent(display, w, event_mask, event_return);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XCheckWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
	return __x11_XCheckWindowEvent(display, w, event_mask, event_return);
}

Bool __x11_XCheckWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
	INIT_GLC
	Bool ret = x11.XCheckWindowEvent(display, w, event_mask, event_return);
	if (ret)
		x11_event(display, event_return);
	return ret;
}

__PUBLIC int XMaskEvent(Display *display, long event_mask, XEvent *event_return)
{
	return __x11_XMaskEvent(display, event_mask, event_return);
}

int __x11_XMaskEvent(Display *display, long event_mask, XEvent *event_return)
{
	INIT_GLC
	int ret = x11.XMaskEvent(display, event_mask, event_return);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event_return)
{
	return __x11_XCheckMaskEvent(display, event_mask, event_return);
}

Bool __x11_XCheckMaskEvent(Display *display, long event_mask, XEvent *event_return)
{
	INIT_GLC
	Bool ret = x11.XCheckMaskEvent(display, event_mask, event_return);
	if (ret)
		x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XCheckTypedEvent(Display *display, int event_type, XEvent *event_return)
{
	return __x11_XCheckTypedEvent(display, event_type, event_return);
}

Bool __x11_XCheckTypedEvent(Display *display, int event_type, XEvent *event_return)
{
	INIT_GLC
	Bool ret = x11.XCheckTypedEvent(display, event_type, event_return);
	if (ret)
		x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XCheckTypedWindowEvent(Display *display, Window w, int event_type, XEvent *event_return)
{
	return __x11_XCheckTypedWindowEvent(display, w, event_type, event_return);
}

Bool __x11_XCheckTypedWindowEvent(Display *display, Window w, int event_type, XEvent *event_return)
{
	INIT_GLC
	Bool ret = x11.XCheckTypedWindowEvent(display, w, event_type, event_return);
	if (ret)
		x11_event(display, event_return);
	return ret;
}

__PUBLIC int XIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	return __x11_XIfEvent(display, event_return, predicate, arg);
}

int __x11_XIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	INIT_GLC
	int ret = x11.XIfEvent(display, event_return, predicate, arg);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XCheckIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	return __x11_XCheckIfEvent(display, event_return, predicate, arg);
}

Bool __x11_XCheckIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	INIT_GLC
	Bool ret = x11.XCheckIfEvent(display, event_return, predicate, arg);
	if (ret)
		x11_event(display, event_return);
	return ret;
}

__PUBLIC int XPeekIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	return __x11_XPeekIfEvent(display, event_return, predicate, arg);
}

int __x11_XPeekIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg)
{
	INIT_GLC
	int ret = x11.XPeekIfEvent(display, event_return, predicate, arg);
	x11_event(display, event_return);
	return ret;
}

__PUBLIC Bool XF86VidModeSetGamma(Display *display, int screen, XF86VidModeGamma *Gamma)
{
	return __x11_XF86VidModeSetGamma(display, screen, Gamma);
}

Bool __x11_XF86VidModeSetGamma(Display *display, int screen, XF86VidModeGamma *Gamma)
{
	INIT_GLC
	if (x11.XF86VidModeSetGamma == NULL)
		return False; /* might not be present */

	Bool ret = x11.XF86VidModeSetGamma(display, screen, Gamma);
	opengl_refresh_color_correction();

	return ret;
}

/**  \} */
/**  \} */
