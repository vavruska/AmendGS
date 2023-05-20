/*	$OpenBSD: diffreg.c,v 1.93 2019/06/28 13:35:00 deraadt Exp $	*/

/*
 * Copyright (C) Caldera International Inc.  2001-2002.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code and documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed or owned by Caldera
 *	International, Inc.
 * 4. Neither the name of Caldera International, Inc. nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
 * INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)diffreg.c   8.1 (Berkeley) 6/6/93
 */

#include <types.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <resources.h>
#include <appleshare.h>
#include <orca.h>

//#include <unix.h>

#include "diff.h"
#include "util.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/*
 * diff - compare two files.
 */

/*
 *	Uses an algorithm due to Harold Stone, which finds
 *	a pair of longest identical subsequences in the two
 *	files.
 *
 *	The major goal is to generate the match vector J.
 *	J[i] is the index of the line in file1 corresponding
 *	to line i file0. J[i] = 0 if there is no
 *	such line in file1.
 *
 *	Lines are hashed so as to work in core. All potential
 *	matches are located by sorting the lines of each file
 *	on the hash (called ``value''). In particular, this
 *	collects the equivalence classes in file1 together.
 *	Subroutine equiv replaces the value of each line in
 *	file0 by the index of the first element of its
 *	matching equivalence in (the reordered) file1.
 *	To save space equiv squeezes file1 into a single
 *	array member in which the equivalence classes
 *	are simply concatenated, except that their first
 *	members are flagged by changing sign.
 *
 *	Next the indices that point into member are unsorted into
 *	array class according to the original order of file0.
 *
 *	The cleverness lies in routine stone. This marches
 *	through the lines of file0, developing a vector klist
 *	of "k-candidates". At step i a k-candidate is a matched
 *	pair of lines x,y (x in file0 y in file1) such that
 *	there is a common subsequence of length k
 *	between the first i lines of file0 and the first y
 *	lines of file1, but there is no such subsequence for
 *	any smaller y. x is the earliest possible mate to y
 *	that occurs in such a subsequence.
 *
 *	Whenever any of the members of the equivalence class of
 *	lines in file1 matable to a line in file0 has serial number
 *	less than the y of some k-candidate, that k-candidate
 *	with the smallest such y is replaced. The new
 *	k-candidate is chained (via pred) to the current
 *	k-1 candidate so that the actual subsequence can
 *	be recovered. When a member has serial number greater
 *	that the y of all k-candidates, the klist is extended.
 *	At the end, the longest subsequence is pulled out
 *	and placed in the array J by unravel
 *
 *	With J in hand, the matches there recorded are
 *	check'ed against reality to assure that no spurious
 *	matches have crept in due to hashing. If they have,
 *	they are broken, and "jackpot" is recorded--a harmless
 *	matter except that a true match for a spuriously
 *	mated line may now be unnecessarily reported as a change.
 *
 *	Much of the complexity of the program comes simply
 *	from trying to minimize core utilization and
 *	maximize the range of doable problems by dynamically
 *	allocating what is needed and reusing what is not.
 *	The core requirements for problems larger than somewhat
 *	are (in words) 2*length(file0) + length(file1) +
 *	3*(number of k-candidates installed),  typically about
 *	6n words for files of length n.
 */

struct cand {
    long	x;
    long	y;
    long	pred;
};

struct line {
    long	serial;
    long	value;
} *file[2];

/*
 * The following struct is used to record change information when
 * doing a "context" or "unified" diff.  (see routine "change" to
 * understand the highly mnemonic field names)
 */
struct context_vec {
    long	a;      /* start line in old file */
    long	b;      /* end line in old file */
    long	c;      /* start line in new file */
    long	d;      /* end line in new file */
};

static void	 output(StringPtr, word, StringPtr, word, long);
static void	 check(word, word, long);
static void	 range(long, long, char *);
static void	 uni_range(long, long);
static void	 dump_context_vec(word, word, long);
static void	 dump_unified_vec(word, word, long);
static void	 prepare(long, word, off_t, long);
static void	 prune(void);
static void	 equiv(struct line *, long, struct line *, long, long *);
static void	 unravel(long);
static void	 unsort(struct line *, long, long *);
static void	 change(StringPtr, word, StringPtr, word, long, long, long, long, long *);
static void	 sort(struct line *, long);
static void	 print_header(const StringPtr, const StringPtr);
static long	 ignoreline(char *);
static long	 asciifile(word);
static long	 fetch(long *, long, long, word, long, long, long);
static long	 newcand(long, long, long);
static long	 search(long *, long, long);
static long	 skipline(word);
static long	 isqrt(long);
static long	 stone(long *, long, long *, long *, long);
static long	 readhash(word, long);
static long	 files_differ(word, word, long);
static char* match_function(const long *, long, word);
static char* preadline(word, size_t, off_t);

static long  *J;         /* will be overlaid on class */
static long  *class;     /* will be overlaid on file[0] */
static long  *klist;     /* will be overlaid on file[0] after class */
static long  *member;        /* will be overlaid on file[1] */
static long   clen;
static long   inifdef;       /* whether or not we are in a #ifdef block */
static long   len[2];
static long   pref, suff;    /* length of prefix and suffix */
static long   slen[2];
static long   anychange;
static long *ixnew;     /* will be overlaid on file[1] */
static long *ixold;     /* will be overlaid on klist */
static struct cand *clist;  /* merely a free storage pot for candidates */
static long   clistlen;      /* the length of clist */
static struct line *sfile[2];   /* shortened by pruning common prefix/suffix */
static u_char *chrtran;         /* translation table for case-folding */
static struct context_vec *context_vec_start;
static struct context_vec *context_vec_end;
static struct context_vec *context_vec_ptr;

#define FUNCTION_CONTEXT_SIZE	55
static char lastbuf[FUNCTION_CONTEXT_SIZE];
static long lastline;
static long lastmatchline;


/*
 * chrtran polongs to one of 2 translation tables: cup2low if folding upper to
 * lower case clow2low if not folding case
 */
u_char clow2low[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41,
    0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
    0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0,
    0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
    0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1,
    0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc,
    0xfd, 0xfe, 0xff
};

u_char cup2low[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x60, 0x61,
    0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
    0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
    0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0,
    0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
    0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1,
    0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc,
    0xfd, 0xfe, 0xff
};


long
diffreg(StringPtr file1, StringPtr file2, long flags) {
    word f1, f2, error;
    long i, rval;
    Str255 filename1, filename2;
    char *pos;

    pos = strrchr(file1->text, ':');
    if (pos) {
        strcpy(filename1.text, pos + 1);
    }
    pos = strrchr(file2->text, ':');
    if (pos) {
        strcpy(filename2.text, pos + 1);
    }
    filename1.textLength = strlen(filename1.text);
    filename2.textLength = strlen(filename2.text);


    f1 = f2 = 0;
    rval = D_SAME;
    anychange = 0;
    lastline = 0;
    lastmatchline = 0;
    context_vec_ptr = context_vec_start - 1;
    if (flags & D_IGNORECASE) {
        chrtran = cup2low;
    } else {
        chrtran = clow2low;
    }

    error = FOpen(0, file1, readEnable, &f1, NULL);
    if (error) {
        warn("failed to fopen %s", file1);
        status |= 2;
        goto closem;
    }
    error = FOpen(0, file2, readEnable, &f2, NULL);
    if (error) {
        warn("failed to fopen %s", file2);
        status |= 2;
        goto closem;
    }

    switch (files_differ(f1, f2, flags)) {
    case 0:
        goto closem;
    case 1:
        break;
    default:
        /* error */
        status |= 2;
        goto closem;
    }

    if ((flags & D_FORCEASCII) == 0 &&
        (!asciifile(f1) || !asciifile(f2))) {
        rval = D_BINARY;
        status |= 1;
        goto closem;
    }
    prepare(0, f1,  stb1.st_size, flags);
    prepare(1, f2, stb2.st_size, flags);

    prune();
    sort(sfile[0], slen[0]);
    sort(sfile[1], slen[1]);

    member = (long *)file[1];
    equiv(sfile[0], slen[0], sfile[1], slen[1], member);
    member = xreallocarray(member, slen[1] + 2, sizeof(*member));

    class = (long *)file[0];
    unsort(sfile[0], slen[0], class);
    class = xreallocarray(class, slen[0] + 2, sizeof(*class));

    klist = xcalloc(slen[0] + 2, sizeof(*klist), "diffreg klist");
    clen = 0;
    clistlen = 100;
    clist = xcalloc(clistlen, sizeof(*clist), "diffreg clist");
    i = stone(class, slen[0], member, klist, flags);
    xfree(&member);
    xfree(&class);

    J = xreallocarray(J, len[0] + 2, sizeof(*J));
    unravel(klist[i]);
    xfree(&clist);
    xfree(&klist);

    ixold = xreallocarray(ixold, len[0] + 2, sizeof(*ixold));
    ixnew = xreallocarray(ixnew, len[1] + 2, sizeof(*ixnew));
    check(f1, f2, flags);
    output(&filename1, f1, &filename2, f2, flags);
closem:
    if (anychange) {
        status |= 1;
        if (rval == D_SAME) {
            rval = D_DIFFER;
        }
    }
    if (f1 != 0) {
        FClose(f1);
    }
    if (f2 != 0) {
        FClose(f2);
    }

    return (rval);
}

/*
 * Check to see if the given files differ.
 * Returns 0 if they are the same, 1 if different, and -1 on error.
 * XXX - could use code from cmp(1) [faster]
 */
static long
files_differ(word f1, word f2, long flags) {
    char buf1[BUFSIZ], buf2[BUFSIZ];
    size_t i, j;
    word e1 = 0, e2 = 0;

    if ((flags & (D_EMPTY1 | D_EMPTY2)) || stb1.st_size != stb2.st_size) {
        return (1);
    }
    for (;;) {
        i = BUFSIZ;
        e1 = FRead(f1, buf1, &i); 
        j = BUFSIZ;
        e2 = FRead(f2, buf2, &j);
        if ((!i && e1) || (!j && e2)) {
            if (e1 != eofEncountered && e2 != eofEncountered) {
                return (-1);
            }
        }
        if (i != j) {
            return (1);
        }
        if (i == 0) {
            return (0);
        }
        if (memcmp(buf1, buf2, i) != 0) {
            return (1);
        }
    }
}

static void prepare(long i, word fd, off_t filesize, long flags) {
    struct line *p;
    long j, h;
    size_t sz;

    FRewind(fd);

    sz = (filesize <= SIZE_MAX ? filesize : SIZE_MAX) / 25;
    if (sz < 100) sz = 100;

    p = xcalloc(sz + 3, sizeof(*p), "diff prepare");
    for (j = 0; (h = readhash(fd, flags));) {
        if (j == sz) {
            sz = sz * 3 / 2;
            p = xreallocarray(p, sz + 3, sizeof(*p));
        }
        p[++j].value = h;
    }
    len[i] = j;
    file[i] = p;
}

static void prune(void) {
    long i, j;

    for (pref = 0; pref < len[0] && pref < len[1] &&
         file[0][pref + 1].value == file[1][pref + 1].value;
         pref++)
    ;
    for (suff = 0; suff < len[0] - pref && suff < len[1] - pref &&
         file[0][len[0] - suff].value == file[1][len[1] - suff].value;
         suff++)
    ;
    for (j = 0; j < 2; j++) {
        sfile[j] = file[j] + pref;
        slen[j] = len[j] - pref - suff;
        for (i = 0; i <= slen[j]; i++) {
            sfile[j][i].serial = i;
        }
    }
}

static void equiv(struct line *a, long n, struct line *b, long m, long *c) {
    long i, j;

    i = j = 1;
    while (i <= n && j <= m) {
        if (a[i].value < b[j].value) {
            a[i++].value = 0;
        } else if (a[i].value == b[j].value) {
            a[i++].value = j;
        } else {
            j++;
        }
    }
    while (i <= n) {
        a[i++].value = 0;
    }
    b[m + 1].value = 0;
    j = 0;
    while (++j <= m) {
        c[j] = -b[j].serial;
        while (b[j + 1].value == b[j].value) {
            j++;
            c[j] = b[j].serial;
        }
    }
    c[j] = -1;
}

/* Code taken from ping.c */
static long isqrt(long n) {
    long y, x = 1;

    if (n == 0) return (0);

    do { /* newton was a stinker */
        y = x;
        x = n / x;
        x += y;
        x /= 2;
    } while ((x - y) > 1 || (x - y) < -1);

    return (x);
}

static long stone(long *a, long n, long *b, long *c, long flags) {
    long i, k, y, j, l;
    long oldc, tc, oldl, sq;
    unsigned long numtries, bound;

    if (flags & D_MINIMAL) {
        bound = ULONG_MAX;
    } else {
        sq = isqrt(n);
        bound = MAXIMUM(256, sq);
    }

    k = 0;
    c[0] = newcand(0, 0, 0);
    for (i = 1; i <= n; i++) {
        j = a[i];
        if (j == 0) {
            continue;
        }
        y = -b[j];
        oldl = 0;
        oldc = c[0];
        numtries = 0;
        do {
            if (y <= clist[oldc].y) {
                continue;
            }
            l = search(c, k, y);
            if (l != oldl + 1) {
                oldc = c[l - 1];
            }
            if (l <= k) {
                if (clist[c[l]].y <= y) continue;
                tc = c[l];
                c[l] = newcand(i, y, oldc);
                oldc = tc;
                oldl = l;
                numtries++;
            } else {
                c[l] = newcand(i, y, oldc);
                k++;
                break;
            }
        } while ((y = b[++j]) > 0 && numtries < bound);
    }
    return (k);
}

static long newcand(long x, long y, long pred) {
    struct cand *q;

    if (clen == clistlen) {
        clistlen = clistlen * 11 / 10;
        clist = xreallocarray(clist, clistlen, sizeof(*clist));
    }
    q = clist + clen;
    q->x = x;
    q->y = y;
    q->pred = pred;
    return (clen++);
}

static long search(long *c, long k, long y) {
    long i, j, l = 0, t;

    if (clist[c[k]].y < y) { /* quick look for typical case */
        return (k + 1);
    }
    i = 0;
    j = k + 1;
    for (;;) {
        l = (i + j) / 2;
        if (l <= i) {
            break;
        }
        t = clist[c[l]].y;
        if (t > y) j = l;
        else if (t < y) {
            i = l;
        } else {
            return (l);
        }
    }
    return (l + 1);
}

static void unravel(long p) {
    struct cand *q;
    long i;

    for (i = 0; i <= len[0]; i++) {
        J[i] = i <= pref ? i : i > len[0] - suff ? i + len[1] - len[0] : 0;
    }
    for (q = clist + p; q->y != 0; q = clist + q->pred) {
        J[q->x + pref] = q->y + pref;
    }
}

/*
 * Check does double duty:
 *  1.	ferret out any fortuitous correspondences due
 *	to confounding by hashing (which result in "jackpot")
 *  2.  collect random access indexes to the two files
 */
static void check(word f1, word f2, long flags) {
    long i, j, jackpot, c, d;
    long ctold, ctnew;

    FRewind(f1);
    FRewind(f2);
    j = 1;
    ixold[0] = ixnew[0] = 0;
    jackpot = 0;
    ctold = ctnew = 0;
    for (i = 1; i <= len[0]; i++) {
        if (J[i] == 0) {
            ixold[i] = ctold += skipline(f1);
            continue;
        }
        while (j < J[i]) {
            ixnew[j] = ctnew += skipline(f2);
            j++;
        }
        if (flags & (D_FOLDBLANKS | D_IGNOREBLANKS | D_IGNORECASE)) {
            for (;;) {
                c = FGetc(f1);
                d = FGetc(f2);
                /*
                 * GNU diff ignores a missing newline
                 * in one file for -b or -w.
                 */
                if (flags & (D_FOLDBLANKS | D_IGNOREBLANKS)) {
                    if (c == EOF && d == '\r') {
                        ctnew++;
                        break;
                    } else if (c == '\r' && d == EOF) {
                        ctold++;
                        break;
                    }
                }
                ctold++;
                ctnew++;
                if ((flags & D_FOLDBLANKS) && isspace(c) &&
                    isspace(d)) {
                    do {
                        if (c == '\r') break;
                        ctold++;
                    } while (isspace(c = FGetc(f1)));
                    do {
                        if (d == '\r') break;
                        ctnew++;
                    } while (isspace(d = FGetc(f2)));
                } else if ((flags & D_IGNOREBLANKS)) {
                    while (isspace(c) && c != '\r') {
                        c = FGetc(f1);
                        ctold++;
                    }
                    while (isspace(d) && d != '\r') {
                        d = FGetc(f2);
                        ctnew++;
                    }
                }
                if (chrtran[c] != chrtran[d]) {
                    jackpot++;
                    J[i] = 0;
                    if (c != '\r' && c != EOF) ctold += skipline(f1);
                    if (d != '\r' && c != EOF) ctnew += skipline(f2);
                    break;
                }
                if (c == '\r' || c == EOF) {
                    break;
                }
            }
        } else {
            for (;;) {
                ctold++;
                ctnew++;
                if ((c = FGetc(f1)) != (d = FGetc(f2))) {
                    /* jackpot++; */
                    J[i] = 0;
                    if (c != '\r' && c != EOF) {
                        ctold += skipline(f1);
                    }
                    if (d != '\r' && c != EOF) {
                        ctnew += skipline(f2);
                    }
                    break;
                }
                if (c == '\r' || c == EOF) {
                    break;
                }
            }
        }
        ixold[i] = ctold;
        ixnew[j] = ctnew;
        j++;
    }
    for (; j <= len[1]; j++) {
        ixnew[j] = ctnew += skipline(f2);
    }
    /*
     * if (jackpot)
     *	fprintf(stderr, "jackpot\n");
     */
}

/* shellsort CACM #201 */
static void sort(struct line *a, long n) {
    struct line *ai, *aim, w;
    long j, m = 0, k;

    if (n == 0) {
        return;
    }
    for (j = 1; j <= n; j *= 2) m = 2 * j - 1;
    for (m /= 2; m != 0; m /= 2) {
        k = n - m;
        for (j = 1; j <= k; j++) {
            for (ai = &a[j]; ai > a; ai -= m) {
                aim = &ai[m];
                if (aim < ai) {
                    break;  /* wraparound */
                }
                if (aim->value > ai[0].value ||
                    (aim->value == ai[0].value &&
                     aim->serial > ai[0].serial)) {
                     break;
                }
                w.value = ai[0].value;
                ai[0].value = aim->value;
                aim->value = w.value;
                w.serial = ai[0].serial;
                ai[0].serial = aim->serial;
                aim->serial = w.serial;
            }
        }
    }
}

static void unsort(struct line *f, long l, long *b) {
    long *a, i;

    a = xcalloc(l + 1, sizeof(*a), "diff unsort");
    for (i = 1; i <= l; i++) {
        a[f[i].serial] = f[i].value;
    }
    for (i = 1; i <= l; i++) {
        b[i] = a[i];
    }
    xfree(&a);
}

static long skipline(word f) {
    long i, c;

    for (i = 1; (c = FGetc(f)) != '\r' && c != EOF; i++) {
        continue;
    }
    return (i);
}

static void output(StringPtr file1, word f1, StringPtr file2, word f2, long flags) {
    long m, i0, i1, j0, j1;

    FRewind(f1);
    FRewind(f2);
    m = len[0];
    J[0] = 0;
    J[m + 1] = len[1] + 1;
    if (diff_format != D_EDIT) {
        for (i0 = 1; i0 <= m; i0 = i1 + 1) {
            while (i0 <= m && J[i0] == J[i0 - 1] + 1) {
                i0++;
            }
            j0 = J[i0 - 1] + 1;
            i1 = i0 - 1;
            while (i1 < m && J[i1 + 1] == 0) {
                i1++;
            }
            j1 = J[i1 + 1] - 1;
            J[i1] = j1;
            change(file1, f1, file2, f2, i0, i1, j0, j1, &flags);
        }
    } else {
        for (i0 = m; i0 >= 1; i0 = i1 - 1) {
            while (i0 >= 1 && J[i0] == J[i0 + 1] - 1 && J[i0] != 0) {
                i0--;
            }
            j0 = J[i0 + 1] - 1;
            i1 = i0 + 1;
            while (i1 > 1 && J[i1 - 1] == 0) {
                i1--;
            }
            j1 = J[i1 - 1] + 1;
            J[i1] = j1;
            change(file1, f1, file2, f2, i1, i0, j1, j0, &flags);
        }
    }
    if (m == 0) {
        change(file1, f1, file2, f2, 1, 0, 1, len[1], &flags);
    }
    if (diff_format == D_IFDEF) {
        for (;;) {
#define	c i0
            if ((c = FGetc(f1)) == EOF) {
                return;
            }
            diff_output("%c", (int) c);
        }
#undef c
    }
    if (anychange != 0) {
        if (diff_format == D_CONTEXT) {
            dump_context_vec(f1, f2, flags);
        } else if (diff_format == D_UNIFIED) {
            dump_unified_vec(f1, f2, flags);
        }
    }
}

static void range(long a, long b, char *separator) {
    diff_output("%ld", a > b ? b : a);
    if (a < b) {
        diff_output("%s%ld", separator, b);
    }
}

static void uni_range(long a, long b) {
    if (a < b) {
        diff_output("%ld,%ld", a, b - a + 1);
    } else if (a == b) {
        diff_output("%ld", b);
    } else {
        diff_output("%ld,0", b);
    }
}

static char *preadline(word fd, size_t rlen, off_t off) {
    char *line;
    size_t nr;
    off_t pos;
    word error;


    line = xmalloc(rlen + 1, "diff preadline");
    pos = FGetMark(fd);
    FSeek(fd, off);
    nr = rlen;
    error = FRead(fd, line, &nr);
    if (error) {
        err(2, "preadline");
    }

    FSeek(fd, pos);
    if (nr > 0 && line[nr - 1] == '\r') {
        nr--;
    }
    line[nr] = '\0';
    return (line);
}

static long ignoreline(char *line) {
#if 0
    long ret;

    ret = regexec(&ignore_re, line, 0, NULL, 0);
    xfree(&line);
    return (ret == 0);  /* if it matched, it should be ignored. */
#endif
    return 0;
}

/*
 * Indicate that there is a difference between lines a and b of the from file
 * to get to lines c to d of the to file.  If a is greater then b then there
 * are no lines in the from file involved and this means that there were
 * lines appended (beginning at b).  If c is greater than d then there are
 * lines missing from the to file.
 */
static void change(StringPtr file1, word f1, StringPtr file2, word f2, long a, long b, long c, long d,
       long *pflags) {
    static size_t max_context = 64;
    long i;

restart:
    if (diff_format != D_IFDEF && a > b && c > d) {
        return;
    }
    if (ignore_pats != NULL) {
        char *line;
        /*
         * All lines in the change, insert, or delete must
         * match an ignore pattern for the change to be
         * ignored.
         */
        if (a <= b) {       /* Changes and deletes. */
            for (i = a; i <= b; i++) {
                line = preadline(f1,
                                 ixold[i] - ixold[i - 1], ixold[i - 1]);
                if (!ignoreline(line)) {
                    goto proceed;
                }
            }
        }
        if (a > b || c <= d) {  /* Changes and inserts. */
            for (i = c; i <= d; i++) {
                line = preadline(f2,
                                 ixnew[i] - ixnew[i - 1], ixnew[i - 1]);
                if (!ignoreline(line)) {
                    goto proceed;
                }
            }
        }
        return;
    }
proceed:
    if (*pflags & D_HEADER) {
        diff_output("%s %s\r", file1->text, file2->text);
        *pflags &= ~D_HEADER;
    }
    if (diff_format == D_CONTEXT || diff_format == D_UNIFIED) {
        /*
         * Allocate change records as needed.
         */
        if (context_vec_ptr == context_vec_end - 1) {
            ptrdiff_t offset = context_vec_ptr - context_vec_start;
            max_context <<= 1;
            context_vec_start = xreallocarray(context_vec_start,
                                              max_context, sizeof(*context_vec_start));
            context_vec_end = context_vec_start + max_context;
            context_vec_ptr = context_vec_start + offset;
        }
        if (anychange == 0) {
            /*
             * Prlong the context/unidiff header first time through.
             */
            print_header(file1, file2);
            anychange = 1;
        } else if (a > context_vec_ptr->b + (2 * diff_context) + 1 &&
                   c > context_vec_ptr->d + (2 * diff_context) + 1) {
            /*
             * If this change is more than 'diff_context' lines from the
             * previous change, dump the record and reset it.
             */
            if (diff_format == D_CONTEXT) {
                dump_context_vec(f1, f2, *pflags);
            } else {
                dump_unified_vec(f1, f2, *pflags);
            }
        }
        context_vec_ptr++;
        context_vec_ptr->a = a;
        context_vec_ptr->b = b;
        context_vec_ptr->c = c;
        context_vec_ptr->d = d;
        return;
    }
    if (anychange == 0) {
        anychange = 1;
    }
    switch (diff_format) {
    case D_BRIEF:
        return;
    case D_NORMAL:
    case D_EDIT:
        range(a, b, ",");
        diff_output("%c",(int) (a > b ? 'a' : c > d ? 'd' : 'c'));
        if (diff_format == D_NORMAL) {
            range(c, d, ",");
        }
        diff_output("\r");
        break;
    case D_REVERSE:
        diff_output("%c", (int) (a > b ? 'a' : c > d ? 'd' : 'c'));
        range(a, b, " ");
        diff_output("\r");
        break;
    case D_NREVERSE:
        if (a > b) {
            diff_output("a%ld %ld\r", b, d - c + 1);
        } else {
            diff_output("d%ld %ld\r", a, b - a + 1);
            if (!(c > d))
                /* add changed lines */
                diff_output("a%ld %ld\r", b, d - c + 1);
        }
        break;
    }
    if (diff_format == D_NORMAL || diff_format == D_IFDEF) {
        fetch(ixold, a, b, f1, '<', 1, *pflags);
        if (a <= b && c <= d && diff_format == D_NORMAL) {
            diff_output("---\r");
        }
    }
    i = fetch(ixnew, c, d, f2, diff_format == D_NORMAL ? '>' : '\0', 0, *pflags);
    if (i != 0 && diff_format == D_EDIT) {
        /*
         * A non-zero return value for D_EDIT indicates that the
         * last line prlonged was a bare dot (".") that has been
         * escaped as ".." to prevent ed(1) from mislongerpreting
         * it.  We have to add a substitute command to change this
         * back and restart where we left off.
         */
        diff_output(".\r");
        diff_output("%ls/.//\r", a + i - 1);
        b = a + i - 1;
        a = b + 1;
        c += i;
        goto restart;
    }
    if ((diff_format == D_EDIT || diff_format == D_REVERSE) && c <= d) {
        diff_output(".\r");
    }
    if (inifdef) {
        diff_output("#endif /* %s */\r", ifdefname);
        inifdef = 0;
    }
}

static long fetch(long *f, long a, long b, word lb, long ch, long oldfile, long flags) {
    long i, j, c, lastc, col; 
    long nc;

    /*
     * When doing #ifdef's, copy down to current line
     * if this is the first file, so that stuff makes it to output.
     */
    if (diff_format == D_IFDEF && oldfile) {
        long curpos = FGetMark(lb);
        /* prlong through if append (a>b), else to (nb: 0 vs 1 orig) */
        nc = f[a > b ? b : a - 1] - curpos;
        for (i = 0; i < nc; i++) {
            diff_output("%c", (int) FGetc(lb));
        }
    }
    if (a > b) {
        return (0);
    }
    if (diff_format == D_IFDEF) {
        if (inifdef) {
            diff_output("#else /* %s%s */\r",
                        oldfile == 1 ? "!" : "", ifdefname);
        } else {
            if (oldfile) {
                diff_output("#ifndef %s\r", ifdefname);
            } else {
                diff_output("#ifdef %s\r", ifdefname);
            }
        }
        inifdef = 1 + oldfile;
    }
    for (i = a; i <= b; i++) {
        FSeek(lb, f[i - 1]);
        nc = f[i] - f[i - 1];
        if (diff_format != D_IFDEF && ch != '\0') {
            diff_output("%c", (int) ch);
            if (diff_format != D_UNIFIED) {
                diff_output(" ");
            }
        }
        col = 0;
        for (j = 0, lastc = '\0'; j < nc; j++, lastc = c) {
            if ((c = FGetc(lb)) == EOF) {
                if (diff_format == D_EDIT || diff_format == D_REVERSE ||
                    diff_format == D_NREVERSE) {
                    warnx("No newline at end of file");
                } else {
                    diff_output("\r");
                }
                return (0);
            }
            if (c == '\n') {
                c = '\n';
            }
            if (c == '\t' && (flags & D_EXPANDTABS)) {
                do {
                    diff_output(" ");
                } while (++col & 7);
            } else {
                if (diff_format == D_EDIT && j == 1 && c == '\r'
                    && lastc == '.') {
                    /*
                     * Don't prlong a bare "." line
                     * since that will confuse ed(1).
                     * Prlong ".." instead and return,
                     * giving the caller an offset
                     * from which to restart.
                     */
                    diff_output(".\r");
                    return (i - a + 1);
                }
                diff_output("%c", (int) c);
                col++;
            }
        }
    }
    return (0);
}

/*
 * Hash function taken from Robert Sedgewick, Algorithms in C, 3d ed., p 578.
 */
static long readhash(word f, long flags) {
    long i, t, space;
    long sum;

    sum = 1;
    space = 0;
    if ((flags & (D_FOLDBLANKS | D_IGNOREBLANKS)) == 0) {
        if (flags & D_IGNORECASE) {
            for (i = 0; (t = FGetc(f)) != '\r'; i++) {
                if (t == EOF) {
                    if (i == 0) {
                        return (0);
                    }
                    break;
                }
                sum = sum * 127 + chrtran[t];
            }
        } else {
            for (i = 0; (t = FGetc(f)) != '\r'; i++) {
                if (t == EOF) {
                    if (i == 0) {
                        return (0);
                    }
                    break;
                }
                sum = sum * 127 + t;
            }
        } 
    } else {
        for (i = 0;;) {
            switch (t = FGetc(f)) {
            case '\t':
            case '\n':
            case '\v':
            case '\f':
            case ' ':
                space++;
                continue;
            default:
                if (space && (flags & D_IGNOREBLANKS) == 0) {
                    i++;
                    space = 0;
                }
                sum = sum * 127 + chrtran[t];
                i++;
                continue;
            case EOF:
                if (i == 0) {
                    return (0);
                }
                /* FALLTHROUGH */
            case '\r':
                break;
            }
            break;
        }
    }
    /*
     * There is a remote possibility that we end up with a zero sum.
     * Zero is used as an EOF marker, so return 1 instead.
     */
    return (sum == 0 ? 1 : sum);
}

static long asciifile(word f) {
    unsigned char buf[BUFSIZ];
    size_t cnt;

    if (!f) {
        return (1);
    }

    FSeek(f, 0);
    cnt = BUFSIZ;
    FRead(f, buf, &cnt);
    return (memchr(buf, '\0', cnt) == NULL);
}

#define begins_with(s, pre) (strncmp(s, pre, sizeof(pre)-1) == 0)

static char *match_function(const long *f, long pos, word fp) {
    unsigned char buf[FUNCTION_CONTEXT_SIZE];
    size_t nc;
    long last = lastline;
    char *state = NULL;

    lastline = pos;
    while (pos > last) {
        FSeek(fp, f[pos - 1]);
        nc = f[pos] - f[pos - 1];
        if (nc >= sizeof(buf)) {
            nc = sizeof(buf) - 1;
        }
        FRead(fp, buf, &nc);
        if (nc > 0) {
            buf[nc] = '\0';
            buf[strcspn((const char *)buf, "\r")] = '\0';
            if (isalpha(buf[0]) || buf[0] == '_' || buf[0] == '$') {
                if (begins_with((const char *)buf, "private:")) {
                    if (!state) {
                        state = " (private)";
                    }
                } else if (begins_with((const char *)buf, "protected:")) {
                    if (!state) {
                        state = " (protected)";
                    }
                } else if (begins_with((const char *)buf, "public:")) {
                    if (!state) {
                        state = " (public)";
                    }
                } else {
                    strncpy(lastbuf, (const char *)buf, sizeof lastbuf);
                    if (state) strncat(lastbuf, (const char *)state, sizeof lastbuf);
                    lastmatchline = pos;
                    return lastbuf;
                }
            }
        }
        pos--;
    }
    return (lastmatchline > 0) ? lastbuf : NULL;
}

/* dump accumulated "context" diff changes */
static void dump_context_vec(word f1, word f2, long flags) {
    struct context_vec *cvp = context_vec_start;
    long lowa, upb, lowc, upd, do_output;
    long a, b, c, d;
    char ch, *f;

    if (context_vec_start > context_vec_ptr) {
        return;
    }

    b = d = 0;      /* gcc */
    lowa = MAXIMUM(1, cvp->a - diff_context);
    upb = MINIMUM(len[0], context_vec_ptr->b + diff_context);
    lowc = MAXIMUM(1, cvp->c - diff_context);
    upd = MINIMUM(len[1], context_vec_ptr->d + diff_context);

    diff_output("***************");
    if ((flags & D_PROTOTYPE)) {
        f = match_function(ixold, lowa - 1, f1);
        if (f != NULL) {
            diff_output(" %s", f);
        }
    }
    diff_output("\r*** ");
    range(lowa, upb, ",");
    diff_output(" ****\r");

    /*
     * Output changes to the "old" file.  The first loop suppresses
     * output if there were no changes to the "old" file (we'll see
     * the "old" lines as context in the "new" list).
     */
    do_output = 0;
    for (; cvp <= context_vec_ptr; cvp++) {
        if (cvp->a <= cvp->b) {
            cvp = context_vec_start;
            do_output++;
            break;
        }
    }
    if (do_output) {
        while (cvp <= context_vec_ptr) {
            a = cvp->a;
            b = cvp->b;
            c = cvp->c;
            d = cvp->d;

            if (a <= b && c <= d) {
                ch = 'c';
            } else {
                ch = (a <= b) ? 'd' : 'a';
            }

            if (ch == 'a') {
                fetch(ixold, lowa, b, f1, ' ', 0, flags);
            } else {
                fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
                fetch(ixold, a, b, f1,
                      ch == 'c' ? '!' : '-', 0, flags);
            }
            lowa = b + 1;
            cvp++;
        }
        fetch(ixold, b + 1, upb, f1, ' ', 0, flags);
    }
    /* output changes to the "new" file */
    diff_output("--- ");
    range(lowc, upd, ",");
    diff_output(" ----\r");

    do_output = 0;
    for (cvp = context_vec_start; cvp <= context_vec_ptr; cvp++) {
        if (cvp->c <= cvp->d) {
            cvp = context_vec_start;
            do_output++;
            break;
        }
    }
    if (do_output) {
        while (cvp <= context_vec_ptr) {
            a = cvp->a;
            b = cvp->b;
            c = cvp->c;
            d = cvp->d;

            if (a <= b && c <= d) {
                ch = 'c';
            } else {
                ch = (a <= b) ? 'd' : 'a';
            }

            if (ch == 'd') {
                fetch(ixnew, lowc, d, f2, ' ', 0, flags);
            } else {
                fetch(ixnew, lowc, c - 1, f2, ' ', 0, flags);
                fetch(ixnew, c, d, f2,
                      ch == 'c' ? '!' : '+', 0, flags);
            }
            lowc = d + 1;
            cvp++;
        }
        fetch(ixnew, d + 1, upd, f2, ' ', 0, flags);
    }
    context_vec_ptr = context_vec_start - 1;
}

/* dump accumulated "unified" diff changes */
static void dump_unified_vec(word f1, word f2, long flags) {
    struct context_vec *cvp = context_vec_start;
    long lowa, upb, lowc, upd;
    long a, b, c, d;
    char ch, *f;

    if (context_vec_start > context_vec_ptr) {
        return;
    }

    d = 0;      /* gcc */
    lowa = MAXIMUM(1, cvp->a - diff_context);
    upb = MINIMUM(len[0], context_vec_ptr->b + diff_context);
    lowc = MAXIMUM(1, cvp->c - diff_context);
    upd = MINIMUM(len[1], context_vec_ptr->d + diff_context);

    diff_output("@@ -");
    uni_range(lowa, upb);
    diff_output(" +");
    uni_range(lowc, upd);
    diff_output(" @@");
    if ((flags & D_PROTOTYPE)) {
        f = match_function(ixold, lowa - 1, f1);
        if (f != NULL) {
            diff_output(" %s", f);
        }
    }
    diff_output("\r");

    /*
     * Output changes in "unified" diff format--the old and new lines
     * are prlonged together.
     */
    for (; cvp <= context_vec_ptr; cvp++) {
        a = cvp->a;
        b = cvp->b;
        c = cvp->c;
        d = cvp->d;

        /*
         * c: both new and old changes
         * d: only changes in the old file
         * a: only changes in the new file
         */
        if (a <= b && c <= d) {
            ch = 'c';
        } else {
            ch = (a <= b) ? 'd' : 'a';
        }

        switch (ch) {
        case 'c':
            fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
            fetch(ixold, a, b, f1, '-', 0, flags);
            fetch(ixnew, c, d, f2, '+', 0, flags);
            break;
        case 'd':
            fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
            fetch(ixold, a, b, f1, '-', 0, flags);
            break;
        case 'a':
            fetch(ixnew, lowc, c - 1, f2, ' ', 0, flags);
            fetch(ixnew, c, d, f2, '+', 0, flags);
            break;
        }
        lowa = b + 1;
        lowc = d + 1;
    }
    fetch(ixnew, d + 1, upd, f2, ' ', 0, flags);

    context_vec_ptr = context_vec_start - 1;
}

static void print_header(const StringPtr file1, const StringPtr file2) {
    if (label[0] != NULL) {
        diff_output("%s %s\r", diff_format == D_CONTEXT ? "***" : "---",
                                      label[0]);
    } else {
        diff_output("%s %s\t%sblah ", diff_format == D_CONTEXT ? "***" : "---",
                     file1->text, ctime(&stb1.st_mtime));
    }
    if (label[1] != NULL) {
        diff_output("%s %s\r", diff_format == D_CONTEXT ? "---" : "+++",
                                      label[1]);
    } else {
        diff_output("%s %s\t%s", diff_format == D_CONTEXT ? "---" : "+++",
                     file2->text, ctime(&stb2.st_mtime));
    }
}

