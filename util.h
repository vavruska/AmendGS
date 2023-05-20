/*
 * Copyright (c) 2023 chris vavruska <chris@vavruska.com> (Apple //gs verison)
 * Copyright (c) 2020-2022 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <stdbool.h>
#include <window.h>
#include <textedit.h>
#include <quickdraw.h>
#include <control.h>
#include <dialog.h> 
#ifndef SIZE_MAX
#define SIZE_MAX ULONG_MAX
#endif

#ifdef __ORCAC__
#define OSType long
#define Size size_t
#endif 
#define nitems(what) (sizeof((what)) / sizeof((what)[0]))
#define member_size(type, member) sizeof(((type *)0)->member)

#define MALLOC_NOTE_SIZE 32

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define BOUND(a, min, max) ((a) > (max) ? (max) : ((a) < (min) ? (min) : (a)))

#define EXPAND_TO_FIT(var, var_size, used_size, add, grow_amount) { \
	if ((used_size) + (add) >= (var_size)) { \
		while ((used_size) + (add) >= (var_size)) { \
			(var_size) += (grow_amount); \
		} \
		(var) = xrealloc((var), (var_size)); \
	} \
}

#define CHARS_TO_LONG(a,b,c,d) (unsigned long)(\
  ((unsigned long)((unsigned char)(a)) << 24) | \
  ((unsigned long)((unsigned char)(b)) << 16) | \
  ((unsigned long)((unsigned char)(c)) << 8) | \
  (unsigned long)((unsigned char)(d)) )
#define CHARS_TO_word(a,b) (unsigned word)(\
  ((unsigned word)((unsigned char)(a)) << 8) | \
  (unsigned word)((unsigned char)(b)) )

#define SCROLLBAR_WIDTH 16

/* GetMBarHeight() is not very useful */
#define MENUBAR_HEIGHT 20

#define TICKS_PER_SEC 60L

#define MAX_TEXTEDIT_SIZE 32767L

#ifndef bool
typedef Boolean bool;
#endif
typedef signed long off_t;
typedef signed long ssize_t;
typedef unsigned char u_char;
typedef unsigned long u_int;
typedef unsigned char u_int8_t;
typedef unsigned int u_int16_t;
typedef unsigned long u_int32_t;

#define BYTE_ORDER BIG_ENDIAN

typedef struct {
	word push[2], rts;
	void *addr;
} tCodeStub;

struct stat {
	word	st_mode;
	ssize_t	st_size;
	time_t	st_ctime;
	time_t	st_mtime;
	unsigned char st_flags;
};

void util_init(void);

void * xmalloc(size_t, char *note);
void xfree(void *ptrptr);
void xfree_verify(void);
void * xmalloczero(size_t, char *note);
void * xcalloc(size_t, size_t, char *note);
void * xrealloc(void *src, size_t size);
void * xreallocarray(void *, size_t, size_t);
char * xstrdup(const char *, char *note);
char * xstrndup(const char *str, size_t maxlen, char *note);

word getline(char *str, size_t len, char **ret);
const char * ordinal(word n);
size_t rtrim(char *str, char *chars);
long strpos_quoted(char *str, char c);
char * OSTypeToString(OSType type);

unsigned long xorshift32(void);

void panic(const char *format, ...);
void err(word ret, const char *format, ...);
void warnx(const char *format, ...);
void warn(const char *format, ...);
void note(const char *format, ...);
void appicon_note(const char *format, ...);
word ask(const char *format, ...);
#define ASK_YES 1
#define ASK_NO  2
void about(char *program_name);
void progress(char *format, ...);
void window_rect(WindowPtr win, Rect *ret);
void center_in_screen(word width, word height, bool titlebar, Rect *b);
Point centered_sfget_dialog(void);
Point centered_sfput_dialog(void);

Handle xNewHandle(size_t size);
Handle xGetResource(ResType type, long id);
Handle xGetString(long id);
char * xGetStringAsChar(long id);
long xGetStringAsLong(long id);
void xSetHandleSize(Handle h, Size s);

word getpath(word vRefNum, StringPtr fileName, GSString255Ptr ret, bool include_file);
word FSDelete(GSString255Ptr path);
word FDelete(StringPtr path);
word FRename(GSString255Ptr source, GSString255Ptr dest);
word FStat(GSString255Ptr path, FileInfoRecPtrGS sb);
word FSeek(word vRefNum, long displacement);
word FCreate(word vRefNum, StringPtr filename, word fileType, long auxType, longword prealloc);
word FOpen(word vRefNum, StringPtr filename, word access, word *frefnum, longword *eof);
word FClose(word fRefNum);
word FWrite(word fRefNum, void *buf, longword *count);
word FRead(word fRefNum, void *buf, longword *count);
word FSetEOF(word fRefNum, longword displacement);
word FGetEOF(word fRefNum, longword *eof);
word FFlush(word fRefNum);
word FRewind(word fRefNum);
int FGetc(word fRefNum); 
long FGetMark(word fRefNum);
word copy_file(GSString255Ptr source, GSString255Ptr dest, bool overwrite);
word copy_file_contents(word source_ref, word dest_ref);
size_t FSReadLine(word frefnum, char *buf, size_t buflen);
word FGetFSTId(word refNum);

word FontHeight(word font_id, word size);
void PasswordDialogFieldFinish(void);
pascal void NullCaretHook(void);
void HideMenuBar(void);
void RestoreHiddenMenuBar(void);

size_t strlcat(char *dst, const char *src, size_t dsize);
size_t strlcpy(char *dst, const char *src, size_t dsize);
char * strndup(const char *str, size_t maxlen);
char * strsep(char **stringp, const char *delim);
char* timeString(time_t tim);


#endif