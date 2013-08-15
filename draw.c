/* See LICENSE file for copyright and license details. */
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include "draw.h"

#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#define DEFAULTFN  "fixed"

static Bool loadfont(DC *dc, const char *fontstr);

void
drawrect(DC *dc, int x, int y, unsigned int w, unsigned int h, Bool fill, unsigned long color) {
	XSetForeground(dc->dpy, dc->gc, color);
	if(fill)
		XFillRectangle(dc->dpy, dc->canvas, dc->gc, dc->x + x, dc->y + y, w, h);
	else
		XDrawRectangle(dc->dpy, dc->canvas, dc->gc, dc->x + x, dc->y + y, w-1, h-1);
}

int
utf8toxchar2b(const char *intext, int inlen, XChar2b *outtext, int outlen)
{
	int j, k;
	for(j =0, k=0; j < inlen && k < outlen; j ++) {
		unsigned char c = intext[j];
		if (c < 128) {
			outtext[k].byte1 = 0;
			outtext[k].byte2 = c;
			k++;
		}
		else if (c < 0xC0) {
			/* we're inside a character we don't know */
			continue;
		}
		else
			switch(c & 0xF0) {
				case 0xC0:
				case 0xD0: /* two bytes 5+6 = 11 bits */
					if (inlen < j + 1)
						return k;
					outtext[k].byte1 = (c & 0x1C) >> 2;
					j++;
					outtext[k].byte2 = ((c & 0x3) << 6) + (intext[j] & 0x3F);
					k++;
					break;
				case 0xE0: /* three bytes 4+6+6 = 16 bits */
					if (inlen < j + 2)
						return k;
					j++;
					outtext[k].byte1 = ((c & 0xF) << 4) + ((intext[j] & 0x3C) >> 2);
					c = intext[j];
					j++;
					outtext[k].byte2 = ((c & 0x3) << 6) + (intext[j] & 0x3F);
					k++;
					break;
				case 0xFF:
					/* the character uses more than 16 bits */
					break;
			}
	}
	return k;
}

int
drawstring2b(Display *display, Drawable d, GC gc, int x, int y, const char *string, int length)
{
	size_t len = strlen(string);
	XChar2b charbuf[len * sizeof(*string)];
	size_t buflen = utf8toxchar2b(string, length, charbuf, len);
	return XDrawString16(display, d, gc, x, y, charbuf, buflen);
}

void
drawtext(DC *dc, const char *text, unsigned long col[ColLast]) {
	char buf[BUFSIZ];
	size_t mn, n = strlen(text);

	/* shorten text if necessary */
	for(mn = MIN(n, sizeof buf); textnw(dc, text, mn) + dc->font.height/2 > dc->w; mn--)
		if(mn == 0)
			return;
	memcpy(buf, text, mn);
	if(mn < n)
		for(n = MAX(mn-3, 0); n < mn; buf[n++] = '.');

	drawrect(dc, 0, 0, dc->w, dc->h, True, BG(dc, col));
	drawtextn(dc, buf, mn, col);
}

void
drawtextn(DC *dc, const char *text, size_t n, unsigned long col[ColLast]) {
	int x = dc->x + dc->font.height/2;
	int y = dc->y + dc->font.ascent+1;

	XSetForeground(dc->dpy, dc->gc, FG(dc, col));
	if(dc->font.set)
		XmbDrawString(dc->dpy, dc->canvas, dc->font.set, dc->gc, x, y, text, n);
	else {
		XSetFont(dc->dpy, dc->gc, dc->font.xfont->fid);
		drawstring2b(dc->dpy, dc->canvas, dc->gc, x, y, text, n);
	}
}

void
eprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if(fmt[0] != '\0' && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	}
	exit(EXIT_FAILURE);
}

void
freedc(DC *dc) {
	if(dc->font.set)
		XFreeFontSet(dc->dpy, dc->font.set);
	if(dc->font.xfont)
		XFreeFont(dc->dpy, dc->font.xfont);
	if(dc->canvas)
		XFreePixmap(dc->dpy, dc->canvas);
	XFreeGC(dc->dpy, dc->gc);
	XCloseDisplay(dc->dpy);
	free(dc);
}

unsigned long
getcolor(DC *dc, const char *colstr) {
	Colormap cmap = DefaultColormap(dc->dpy, DefaultScreen(dc->dpy));
	XColor color;

	if(!XAllocNamedColor(dc->dpy, cmap, colstr, &color, &color))
		eprintf("cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

DC *
initdc(void) {
	DC *dc;

	if(!XSupportsLocale())
		fputs("no locale support\n", stderr);
	if(!(dc = calloc(1, sizeof *dc)))
		eprintf("cannot malloc %u bytes:", sizeof *dc);
	if(!(dc->dpy = XOpenDisplay(NULL)))
		eprintf("cannot open display\n");

	dc->gc = XCreateGC(dc->dpy, DefaultRootWindow(dc->dpy), 0, NULL);
	XSetLineAttributes(dc->dpy, dc->gc, 1, LineSolid, CapButt, JoinMiter);
	return dc;
}

void
initfont(DC *dc, const char *fontstr) {
	if(!loadfont(dc, fontstr ? fontstr : DEFAULTFN)) {
		if(fontstr != NULL)
			fprintf(stderr, "cannot load font '%s'\n", fontstr);
		if(fontstr == NULL || !loadfont(dc, DEFAULTFN))
			eprintf("cannot load font '%s'\n", DEFAULTFN);
	}
	dc->font.height = dc->font.ascent + dc->font.descent;
}

Bool
loadfont(DC *dc, const char *fontstr) {
	char *def, **missing, **names;
	int i, n;
	XFontStruct **xfonts;

	if(!*fontstr)
		return False;
	if((dc->font.set = XCreateFontSet(dc->dpy, fontstr, &missing, &n, &def))) {
		n = XFontsOfFontSet(dc->font.set, &xfonts, &names);
		for(i = 0; i < n; i++) {
			dc->font.ascent  = MAX(dc->font.ascent,  xfonts[i]->ascent);
			dc->font.descent = MAX(dc->font.descent, xfonts[i]->descent);
			dc->font.width   = MAX(dc->font.width,   xfonts[i]->max_bounds.width);
		}
	}
	else if((dc->font.xfont = XLoadQueryFont(dc->dpy, fontstr))) {
		dc->font.ascent  = dc->font.xfont->ascent;
		dc->font.descent = dc->font.xfont->descent;
		dc->font.width   = dc->font.xfont->max_bounds.width;
	}
	if(missing)
		XFreeStringList(missing);
	return dc->font.set || dc->font.xfont;
}

void
mapdc(DC *dc, Window win, unsigned int w, unsigned int h) {
	XCopyArea(dc->dpy, dc->canvas, win, dc->gc, 0, 0, w, h, 0, 0);
}

void
resizedc(DC *dc, unsigned int w, unsigned int h) {
	if(dc->canvas)
		XFreePixmap(dc->dpy, dc->canvas);

	dc->w = w;
	dc->h = h;
	dc->canvas = XCreatePixmap(dc->dpy, DefaultRootWindow(dc->dpy), w, h,
	                           DefaultDepth(dc->dpy, DefaultScreen(dc->dpy)));
}

int
textnw(DC *dc, const char *text, size_t len) {
	if(dc->font.set) {
		XRectangle r;

		XmbTextExtents(dc->font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc->font.xfont, text, len);
}

int
textw(DC *dc, const char *text) {
	return textnw(dc, text, strlen(text)) + dc->font.height;
}
