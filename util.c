/*
 * Copyright (c) 2023 chris vavruska <chris@vavruska.com> (Apple //gs verison)
 * Copyright (c) 2020-2022 joshua stein <jcs@jcs.org>
 * Copyright (c) 1998, 2015 Todd C. Miller <millert@openbsd.org>
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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


#include <types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <memory.h>
#include <misctool.h>
#include <resources.h>
#include <window.h>
#include <orca.h>
#include <gsos.h>
#include <appleshare.h>
#include <qdaux.h>

#include "AmendGS.h"
#include "util.h"

segment "util";

/* ALRT resources */
#define ASK_ALERT_ID 130

#define ERROR_STRING_SIZE	1024
static char err_str[ERROR_STRING_SIZE];

extern word programID;
static CtlRecHndl progress_static = NULL;
static WindowPtr progress_dialog = NULL;

enum {
	STOP_ALERT,
	CAUTION_ALERT,
	NOTE_ALERT,
	APPICON_ALERT
};

/*
 * Define to audit each malloc and free and verify that a pointer isn't
 * double-freed.  The list of outstanding allocations can be checked by
 * looking through malloc_map.
 */
//#define MALLOC_DEBUG

#ifdef MALLOC_DEBUG
/*
 * List of allocations, updated at xmalloc() and xfree().  If an address
 * passed to xfree() isn't in the list, it indicates a double-free.
 */
#define MALLOC_MAP_CHUNK_SIZE	1024
struct malloc_map_e {
	unsigned long addr;
	unsigned long size;
	char note[MALLOC_NOTE_SIZE];
} *malloc_map = NULL;
unsigned long malloc_map_size = 0;
static bool malloc_map_compact = false;
#endif

void vwarn(word alert_func, const char *format, va_list ap);

/*
 * Util helper needed to be called at program startup, to pre-allocate
 * some things that we can't do during errors.
 */

void util_init(void)
{	
#ifdef MALLOC_DEBUG
	malloc_map_size = MALLOC_MAP_CHUNK_SIZE;
	malloc_map = (struct malloc_map_e *)NewPtr(malloc_map_size *
	  sizeof(struct malloc_map_e));
    if (malloc_map == NULL) {
		panic("NewPtr(%lu) failed", MALLOC_MAP_CHUNK_SIZE);
    }
	memset(malloc_map, 0, malloc_map_size);
#endif
}

/*
 * Memory functions
 */

#define MUL_NO_OVERFLOW ((size_t)1 << (sizeof(size_t) * 4))

void *xmalloc(size_t size, char *note)
{
	void *ptr;
#ifdef MALLOC_DEBUG
	struct malloc_map_e *new_malloc_map;
	word n, j;
#endif
	
	if (size == 0)
		panic("xmalloc: zero size");

	ptr = (void *) malloc(size);
    if (ptr == NULL) {
		panic("xmalloc(%lu) failed", size);
    }

#ifdef MALLOC_DEBUG
	if (malloc_map_compact) {
		for (n = 0; n < malloc_map_size; n++) {
            if (malloc_map[n].addr != 0) {
				continue;
            }
				
			for (j = n + 1; j < malloc_map_size; j++) {
                if (malloc_map[j].addr == 0) {
					continue;
                }
					
				malloc_map[n] = malloc_map[j];
				memset(&malloc_map[j], 0, sizeof(struct malloc_map_e));
				break;
			}
		}
		
		malloc_map_compact = false;
	}
	
	for (n = 0; n <= malloc_map_size; n++) {
		if (n == malloc_map_size) {
			malloc_map_size += MALLOC_MAP_CHUNK_SIZE;
			warn("xmalloc(%lu): out of malloc map entries, maybe a "
			  "memory leak, resizing to %ld", size, malloc_map_size);
			new_malloc_map = (struct malloc_map_e *)NewPtr(
			  malloc_map_size * sizeof(struct malloc_map_e));
            if (new_malloc_map == NULL) {
				panic("out of memory resizing malloc map");
            }
			memcpy(new_malloc_map, malloc_map,
			  (malloc_map_size - MALLOC_MAP_CHUNK_SIZE) *
			  sizeof(struct malloc_map_e));
			DisposePtr(malloc_map);
			malloc_map = new_malloc_map;
		}
		if (malloc_map[n].addr == 0) {
			malloc_map[n].addr = (unsigned long)ptr;
			malloc_map[n].size = size;
			strlcpy(malloc_map[n].note, note, sizeof(malloc_map[n].note));
			break;
		}
		n = n;
	}
#endif

	return ptr;
}

void xfree(void *ptrptr)
{
	unsigned long *addr = (unsigned long *)ptrptr;
	void *ptr = (void *)*addr;
#ifdef MALLOC_DEBUG
	unsigned long n;

	for (n = 0; n <= malloc_map_size; n++) {
        if (n == malloc_map_size) {
			panic("xfree(0x%lx): can't find in alloc map, likely "
              "double free()", *addr);
        }
		if (malloc_map[n].addr == *addr) {
			malloc_map[n].addr = 0;
			malloc_map[n].size = 0;
			malloc_map[n].note[0] = '\0';
			break;
		}
	}
#endif

	free(ptr);

	*addr = 0L;
}

void *xmalloczero(size_t size, char *note)
{
	void *ptr;
	
	ptr = xmalloc(size, note);
	memset(ptr, 0, size);

	return ptr;
}

void *xcalloc(size_t nmemb, size_t size, char *note)
{
	void *ptr;

    if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
        nmemb > 0 && SIZE_MAX / nmemb < size) {
		panic("xcalloc(%lu, %lu) overflow", nmemb, size);
    }
	ptr = xmalloczero(nmemb * size, note);
    if (ptr == NULL) {
		panic("xcalloc(%lu, %lu) failed", nmemb, size);
    }

	return ptr;
}

void *xrealloc(void *src, size_t size)
{
	void *ptr, *tsrc;
	char note[MALLOC_NOTE_SIZE] = "realloc from null";
	
#ifdef MALLOC_DEBUG
	if (src != NULL) {
		for (n = 0; n <= malloc_map_size; n++) {
			if (n == malloc_map_size) {
				panic("xrealloc(%lu): can't find in alloc map, likely "
				  "double free()", (unsigned long)src);
				return NULL;
			}
			if (malloc_map[n].addr == (unsigned long)src) {
				strlcpy(note, malloc_map[n].note, sizeof(note));
				break;
			}
		}
	}
#endif

	ptr = xmalloc(size, note);
	if (src != NULL) {
		memcpy(ptr, src, size);
		tsrc = src;
		xfree(&tsrc);
	}

	return ptr;
}

void *xreallocarray(void *optr, size_t nmemb, size_t size)
{
	if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
	  nmemb > 0 && SIZE_MAX / nmemb < size)
		panic("xreallocarray(%lu, %lu) failed", nmemb, size);
	return xrealloc(optr, size * nmemb);
}

char *xstrdup(const char *str, char *note)
{
	char *cp;
	size_t len;
	
	len = strlen(str);
	
	cp = xmalloc(len + 1, note);
	strlcpy(cp, str, len + 1);
	
	return cp;
}

char *xstrndup(const char *str, size_t maxlen, char *note)
{
	char *copy;
	const char *cp;
	size_t len;

	/* strnlen */
	for (cp = str; maxlen != 0 && *cp != '\0'; cp++, maxlen--)
		;

	len = (size_t)(cp - str);
	copy = xmalloc(len + 1, note);
	(void)memcpy(copy, str, len);
	copy[len] = '\0';

	return copy;
}


/*
 * String functions
 */

word getline(char *str, size_t len, char **ret)
{
	word i;
	
	for (i = 0; i < len; i++) {
		if (str[i] == '\r' || i == len - 1) {
			if (*ret == NULL)
				*ret = xmalloc(i + 1, "getline");
			memcpy(*ret, str, i + 1);
			(*ret)[i] = '\0';
			return i + 1;
		}
	}
	
	return 0;
}

const char *ordinal(word n)
{
	switch (n % 100) {
	case 11:
	case 12:
	case 13:
		return "th";
	default:
		switch (n % 10) {
		case 1:
			return "st";
		case 2:
			return "nd";
		case 3:
			return "rd";
		default:
			return "th";
		}
	}
}

size_t rtrim(char *str, char *chars)
{
	size_t len, rlen, n, j;
	
	rlen = len = strlen(str);
	
	for (n = len; n > 0; n--) {
		for (j = 0; chars[j] != '\0'; j++) {
			if (str[n - 1] == chars[j]) {
				rlen--;
				str[n - 1] = '\0';
				goto next_in_str;
			}
		}
		
		break;
next_in_str:
		continue;		
	}
	
	return rlen;
}

long strpos_quoted(char *str, char c)
{
	long pos;
	unsigned char quot = 0;
	
	for (pos = 0; str[pos] != '\0'; pos++) {
		if (quot) {
			if (str[pos] == '\\') {
				pos++;
				continue;
			}
			if (str[pos] == quot)
				quot = 0;
			continue;
		} else {
			if (str[pos] == '"') {
				quot = str[pos];
				continue;
			}
			if (str[pos] == c)
				return pos;
		}
	}
	
	return -1;
}

char *OSTypeToString(OSType type)
{
	static char ostype_s[5];

	ostype_s[0] = (unsigned char)((type >> 24) & 0xff);
	ostype_s[1] = (unsigned char)((type >> 16) & 0xff);
	ostype_s[2] = (unsigned char)((type >> 8) & 0xff);
	ostype_s[3] = (unsigned char)(type & 0xff);
	ostype_s[4] = 0;
	
	return ostype_s;
}

/*
 * BSD err(3) and warn(3) functions
 */
void vwarn(word alert_func, const char *format, va_list ap)
{
    extern char err_str[ERROR_STRING_SIZE];
    char alertStr[ERROR_STRING_SIZE];
    word icon;
    word size;
    int len = 0;
    char *t = (char *)ap[1];

    switch (alert_func) {
    case CAUTION_ALERT:
        icon = 4;
        break;
    case NOTE_ALERT:
        icon = 3;
        break;
    case APPICON_ALERT:
        //icon = GetResource('ICN#', APPICON_ICN_ID);
    default:
        icon = 2;
    }

    len = vsprintf(err_str, format, ap);
    if (len < 61) {
        size = 2;
    } else if (len < 111) {
        size = 3;
    } else if (len < 151) {
        size = 6;
    } else if (len < 176) {
        size = 4;
    } else if (len < 200) {
        size = 7;
    } else if (len < 250) {
        size = 8;
    } else {
        size = 9;
    }
    size++;
    size = size > 9 ? 9 : size;
    sprintf(alertStr, "%c%c/%s/^#0", 48 + size, 48 + icon, err_str);
    AlertWindow(awCString + awButtonLayout, 0, (Ref)alertStr);
    t = err_str;
}

void ExitToShell(void) {
    QuitRecGS quit = { 1, 0 };
    QuitGS(&quit);
}

void panic(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(STOP_ALERT, format, ap);
	va_end(ap);
	
	ExitToShell();
}

void err(word ret, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(STOP_ALERT, format, ap);
	va_end(ap);
	
	ExitToShell();
}

void warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(CAUTION_ALERT, format, ap);
	va_end(ap);
}

void warnx(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(CAUTION_ALERT, format, ap);
	va_end(ap);
}

void note(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(NOTE_ALERT, format, ap);
	va_end(ap);
}

void appicon_note(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(APPICON_ALERT, format, ap);
	va_end(ap);
}

word ask(const char *format, ...)
{
    char alertStr[ERROR_STRING_SIZE];
	word hit = 0;
	va_list ap;
    word icon = 3;

    va_start(ap, format);
    vsnprintf(err_str, ERROR_STRING_SIZE, format, ap);
    va_end(ap);
    sprintf(alertStr, "6%d/%s/^#2/#3", icon, err_str);
    hit = AlertWindow(awCString + awButtonLayout, 0, (Ref)alertStr);

	return (hit == 1);
}

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

void progress(char *format, ...)
{
	static char progress_s[255];
	va_list argptr;

	if (format == NULL) {
		if (progress_dialog != NULL) {
            CloseWindow(progress_dialog);
			progress_dialog = NULL;
            InitCursor();
		}
		return;
	}
		
	va_start(argptr, format);
	vsnprintf(progress_s, 256, format, argptr);
	va_end(argptr);
	//c2pstr(progress_s);
	
	if (progress_dialog == NULL) {
        progress_dialog = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                                  PROGRESS_WINDOW_ID, rWindParam1);
        progress_static = GetCtlHandleFromID(progress_dialog, PROGRESS_STATIC_TEXT_ID);
        (*progress_static)->ctlData = (long)&progress_s;
        (*progress_static)->ctlMoreFlags = 0x1000;
        WaitCursor();
    }
	ShowWindow(progress_dialog);
    (*progress_static)->ctlValue = strlen(progress_s);
    DrawOneCtl(progress_static);
}


/*
 * General Mac-specific non-GUI functions
 */

static unsigned long _xorshift_state = 0;
unsigned long xorshift32(void)
{
	unsigned long x = _xorshift_state;
	if (x == 0)
		x = GetTick();
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return _xorshift_state = x;
}

 
Handle xNewHandle(size_t size)
{
	Handle h;
	
    if (size == 0) {
		panic("Zero xNewHandle size");
    }

    h = NewHandle(size, programID, 0, NULL);

    if (h == NULL) {
		panic("Failed to NewHandle(%lu)", size);
    }
	
	return h;
}


Handle xGetResource(ResType type, long id) {
	Handle h;
	
	h = LoadResource(type, id);
    
    if (h == NULL) {
		panic("Failed to find resource %d", id);
    }
	
	return h;
}

Handle xGetString(long id)
{
	Handle h;
	
	h = xGetResource(rCString, id);
    if (h == NULL) {
		panic("Failed to find STR resource %d", id);
    }
	
	return h;
}

char *xGetStringAsChar(long id)
{
	Handle h = NULL;
	char *out = NULL;
	size_t l;
	
	h = xGetString(id);
    if (!toolerror() && h) {
        HLock(h);
        l = (*h)[0];
        out = xmalloc(l + 1, "xGetStringAsChar");
        memcpy((void *)out, (void *)(*h + 1), l);
        out[l] = '\0';
        ReleaseResource(0, rPString, (long) h);
    }
	
	return out;
}

long xGetStringAsLong(long id)
{
	char *c;
	long r;
	
	c = xGetStringAsChar(id);
	r = atol(c);
	xfree(&c);
	return r;
}

void xSetHandleSize(Handle h, Size s)
{
	SetHandleSize(s, h);
    if (toolerror()) {
		panic("Failed to SetHandleSize to %ld", s);
    }
}

word getpath(word vRefNum, StringPtr fileName, GSString255Ptr ret, bool include_file) {
    ResultBuf255 prefix = { 255, { 0 }};
    RefInfoRecGS refRec = { 3, vRefNum, 0, &prefix };
    char *pos;
    word error;

    memset(ret, 0, sizeof(GSString255));
    GetRefInfoGS(&refRec);
    error = toolerror();
    if (!error) {
        pos = strrchr(prefix.bufString.text, ':');
        if (pos) {
            pos++;
            *pos = 0;
            prefix.bufString.length = strlen(prefix.bufString.text);
        }
        memcpy(ret, &prefix.bufString, prefix.bufString.length + sizeof(word));
        if (include_file) {
            strncat(ret->text, fileName->text, fileName->textLength);
            ret->length = strlen(ret->text);
        }
    }
    
    return error;
}

word FSDelete(GSString255Ptr path) {
    NameRecGS destroyRec = { 1, path };
    DestroyGS(&destroyRec);
    return toolerror();
}

word FDelete(StringPtr path) {
    GSString255 gpath;
    s2gstr(gpath, (*path));
    return FSDelete(&gpath);
}

word FRename(GSString255Ptr source, GSString255Ptr dest) {
    ChangePathRecGS changeRec = { 2, source, dest };
    ChangePathGS(&changeRec);

    return toolerror();
}

word FStat(GSString255Ptr path, FileInfoRecPtrGS sb) {
    static ResultBuf255 optionList = { 255, { 0 } };

    sb->pCount = 11;
    sb->pathname = path;
    sb->optionList = &optionList;
    GetFileInfoGS(sb);
    
    return toolerror();
}

word copy_file(GSString255Ptr source, GSString255Ptr dest, bool overwrite) {
    word error, source_ref, dest_ref;
    FileInfoRecGS infoRec;
    CreateRecGS createRec = {7, dest, 0};
    OpenRecGS sOpenRec = { 4, 0, source, readEnable, dataForkNum };
    OpenRecGS dOpenRec = { 4, 0, dest, writeEnable, dataForkNum };
    RefNumRecGS closeRec = { 1 };

	/* copy data fork */
    error = FStat(source, &infoRec);
    if (error) {
		return error;
    }
	
    createRec.access = infoRec.access;
    createRec.fileType = infoRec.fileType;
    createRec.auxType = infoRec.auxType;
    createRec.storageType = infoRec.storageType;
    createRec.eof = infoRec.eof;
    createRec.resourceEOF = infoRec.resourceEOF;
    CreateGS(&createRec);
    error = toolerror();
	if (error == dupPathname && overwrite) {
        error = FSDelete(dest);
        if (error) {
			return error;
        }
        CreateGS(&createRec);
        error = toolerror();
	}
    if (error) {
		return error;
    }
	
    OpenGS(&sOpenRec);
    error = toolerror();
    if (error) {
		return error;
    }
    source_ref = sOpenRec.refNum;
	
    OpenGS(&dOpenRec);
    error = toolerror();
	if (error) {
        closeRec.refNum = source_ref;
        CloseGS(&closeRec);
		return error;
	}
    dest_ref = dOpenRec.refNum;

	error = copy_file_contents(source_ref, dest_ref);
	
    closeRec.refNum = source_ref;
    CloseGS(&closeRec);
    closeRec.refNum = dest_ref;
    CloseGS(&closeRec);
	
    if (error) {
		return error;
    }
	
	/*
	 * Copy resource fork, open source as shared read/write in case it's
	 * an open resource file.
	 */
    if (infoRec.resourceEOF) {
    }
    sOpenRec.resourceNumber =  resourceForkNum;
    OpenGS(&sOpenRec);
    error = toolerror();
	if (error) {
		/* no resource fork */
		return 0;
	}
    source_ref = sOpenRec.refNum;
	
    dOpenRec.resourceNumber = resourceForkNum;
    OpenGS(&dOpenRec);
    error = toolerror();

	if (error) {
        closeRec.refNum = source_ref;
        CloseGS(&closeRec);
		return error;
	}
	
	error = copy_file_contents(source_ref, dest_ref);
	
    closeRec.refNum = source_ref;
    CloseGS(&closeRec);
    closeRec.refNum = dest_ref;
    CloseGS(&closeRec);
	
    return error;
}

word FSeek(word vRefNum, long displacement) {
    SetPositionRecGS posRec = { 3, vRefNum, startPlus, displacement };
    SetMarkGS(&posRec);

    return toolerror();
}

word FCreate(word vRefNum, StringPtr filename, word fileType, long auxType, longword preAlloc) {
    GSString255 path;
    CreateRecGS createRec = { 6, &path, 0x00E3, fileType, auxType, 1, preAlloc};
    if (vRefNum) {
        getpath(vRefNum, filename, &path, true);
    } else {
        strncpy(path.text, filename->text, filename->textLength);
        path.length = filename->textLength;
    }

    CreateGS(&createRec);

    return toolerror();
}

word FOpen(word vRefNum, StringPtr filename, word access, word *frefnum, longword *eof) {
    GSString255 path;
    OpenRecGS openRec = { 12, 0, &path, access, dataForkNum };

    if (vRefNum) {
        getpath(vRefNum, filename, &path, true);
    } else {
        strncpy(path.text, filename->text, filename->textLength);
        path.length = filename->textLength;
    }

    OpenGS(&openRec);

    if (toolerror() == 0) {
        if (frefnum) {
            *frefnum = openRec.refNum;
        }
        if (eof) {
            *eof = openRec.eof;
        }
    }
    return toolerror();
}

word FWrite(word fRefNum, void *buf, longword *count) {
    IORecGS writeRec = { 4, fRefNum, (Pointer) buf, *count, 0 };

    WriteGS(&writeRec);
    *count = writeRec.transferCount;

    return toolerror();
}

word FRead(word fRefNum, void *buf, longword *count) {
    IORecGS readRec = { 4, fRefNum, (Pointer) buf, *count, 0 };

    ReadGS(&readRec);
    *count = readRec.transferCount;

    return toolerror();
}

word FClose(word fRefNum) {
    RefNumRecGS closeRec = { 1, fRefNum };

    CloseGS(&closeRec);

    return toolerror();
}

word FSetEOF(word fRefNum, longword displacement) {
    SetPositionRecGS eofRec = { 3, fRefNum, 0, displacement };

    SetEOFGS(&eofRec);

    return toolerror();
}

word FGetEOF(word fRefNum, longword *eof) {
    EOFRecGS eofRec = { 2, fRefNum, 0 };

    GetEOFGS(&eofRec);
    if (eof) {
        *eof = eofRec.eof;
    }

    return toolerror();
}

word FFlush(word fRefNum) {
    RefNumRecGS flushRec = { 1, fRefNum };

    FlushGS(&flushRec);

    return toolerror();
}

word FRewind(word fRefNum) {
    return FSeek(fRefNum, 0);
}

int FGetc(word fRefNum) {
    char c = 0;
    IORecGS readRec = { 4, fRefNum, &c, 1, 0 };
    ReadGS(&readRec);
    if (toolerror()) {
        return -1;
    }
    return c;
}

long FGetMark(word fRefNum) {
    PositionRecGS markRec = { 2, fRefNum, 0 };

    GetMarkGS(&markRec);

    return markRec.position;
}

word copy_file_contents(word source_ref, word dest_ref)
{
	char *buf;
	word error;
    long count, source_size;
    EOFRecGS eofRec = { 2, source_ref };
    IORecGS ioRec = { 4, 0, 0, 0, 0 };

    GetEOFGS(&eofRec);
    source_size = eofRec.eof;

    error = FSeek(source_ref, 0);
    if (error) {
		return error;
    }
    eofRec.refNum = dest_ref;
    error = FSeek(dest_ref, 0);
    if (error) {
		return error;
    }
		
	buf = xmalloc(1024, "copy_file_contents");
	
	while (source_size > 0) {
		count = 1024;
        if (count > source_size) {
			count = source_size;
        }
        ioRec.refNum = source_ref;
        ioRec.dataBuffer = buf;
        ioRec.requestCount = count;
        ReadGS(&ioRec);
        error = toolerror();
        if (error && error != eofEncountered) {
			break;
        }
		source_size -= count;
        ioRec.refNum = dest_ref;
        WriteGS(&ioRec);
        error = toolerror();
		if (error && error != eofEncountered)
			break;
	}
	
	xfree(&buf);
	
    if (error && error != eofEncountered) {
		return error;
    }
	
	return 0;
}

/* read a \r-terminated line or the final non-line bytes of an open file */
size_t FSReadLine(word frefnum, char *buf, size_t buflen)
{
    size_t pos, fsize, total_read = 0;
	char tbuf = 0;
	word error;
    EOFRecGS eofRec = { 2, frefnum };
    PositionRecGS markRec = { 2, frefnum, 0 };
    IORecGS ioRec = { 4, frefnum, &tbuf, 1, 0 };

    GetMarkGS(&markRec);
    pos = markRec.position;
    GetEOFGS(&eofRec);
    fsize = eofRec.eof;
	
	
	for (; pos <= fsize; pos++) {
        if (total_read > buflen) {
			return -1;
        }
        ReadGS(&ioRec);
        error = toolerror();
        if (error) {
			return -1;
        }
		
        if (tbuf == '\r') {
			return total_read;
        }

		buf[total_read++] = tbuf;
	}
	
	/* nothing found until the end of the file */
	return total_read;
}

word FGetFSTId(word refNum) {
    ResultBuf255 volume = { 255, { 0 }};
    RefInfoRecGS refRec = { 3, refNum, 0, &volume };
    char *pos;
    int sysID = 0;
    VolumeRecGS volumeRec = {0};
    DInfoRecGS dInfoRec;
    ResultBuf32 devName = { 32, { 0 }};
    ResultBuf255 volName = { 255, { 0 }};
    word devCount = 0;

    GetRefInfoGS(&refRec);
    pos = strchr(volume.bufString.text, ':');
    if (!pos) {
        return 0;
    }

    *pos = 0;
    do {
        dInfoRec.pCount = 2;
        dInfoRec.devNum = devCount++;
        dInfoRec.devName = &devName;
        DInfoGS(&dInfoRec);
        if (toolerror() == 0) {
            memset(&volName, 0, sizeof(ResultBuf255));
            volumeRec.pCount = 5;
            volumeRec.devName = &(devName.bufString);
            volumeRec.volName = &volName;
            VolumeGS(&volumeRec);
            if (toolerror() == 0 &&
                strcmp(volume.bufString.text, volName.bufString.text) == 0) {
                sysID = volumeRec.fileSysID;
                break;
            }
        } else {
            break;
        }
    } while (toolerror() != paramRangeErr);

    return sysID;
}

/*
 * Appends src to string dst of size dsize (unlike strncat, dsize is the
 * full size of dst, not space left).  At most dsize-1 characters
 * will be copied.  Always NUL terminates (unless dsize <= strlen(dst)).
 * Returns strlen(src) + MIN(dsize, strlen(initial dst)).
 * If retval >= dsize, truncation occurred.
 */
size_t strlcat(char *dst, const char *src, size_t dsize)
{
	const char *odst = dst;
	const char *osrc = src;
	size_t n = dsize;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end. */
    while (n-- != 0 && *dst != '\0') {
		dst++;
    }
	dlen = dst - odst;
	n = dsize - dlen;

    if (n-- == 0) {
		return(dlen + strlen(src));
    }
	while (*src != '\0') {
		if (n != 0) {
			*dst++ = *src;
			n--;
		}
		src++;
	}
	*dst = '\0';

	return(dlen + (src - osrc));	/* count does not include NUL */
}

/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t strlcpy(char *dst, const char *src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0') {
				break;
            }
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';		/* NUL-terminate dst */
		while (*src++)
			;
	}

	return(src - osrc - 1);	/* count does not include NUL */
}

char *strndup(const char *str, size_t maxlen)
{
	char *copy;
	const char *cp;
	size_t len;

	/* strnlen */
	for (cp = str; maxlen != 0 && *cp != '\0'; cp++, maxlen--)
		;

	len = (size_t)(cp - str);
	copy = malloc(len + 1);
	if (copy != NULL) {
		(void)memcpy(copy, str, len);
		copy[len] = '\0';
	}

	return copy;
}

/*
 * Get next token from string *stringp, where tokens are possibly-empty
 * strings separated by characters from delim.  
 *
 * Writes NULs into the string at *stringp to end tokens.
 * delim need not remain constant from call to call.
 * On return, *stringp points past the last NUL written (if there might
 * be further tokens), or is NULL (if there are definitely no more tokens).
 *
 * If *stringp is NULL, strsep returns NULL.
 */
char *strsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
                if (c == 0) {
					s = NULL;
                } else {
					s[-1] = 0;
                }
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
	return (NULL);
}

char* timeString(time_t tim) {
    TimeRec tm;
    static char timeStr[32];
    static char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    ConvSeconds(secs2TimeRec, tim, (Pointer) &tm);
    memset(timeStr, 0, sizeof(timeStr));

    sprintf(timeStr, "%s %s %d %02d:%02d:%02d %d",
            days[tm.weekDay], months[tm.month], tm.day, tm.hour, tm.minute,
            tm.second, 1900+(int)tm.year);

    return timeStr;
}

