/*
 * Copyright (c) 2023 chris vavruska <chris@vavruska.com> (Apple //gs verison)
 * Copyright (c) 2021 joshua stein <jcs@jcs.org>
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

#ifndef __REPO_H__
#define __REPO_H__

#include <time.h>
//#include "bile.h"
#include <memory.h>
#include <textedit.h>

#define AMEND_CREATOR		'AMND'

#define REPO_TYPE			0x0052

#define REPO_FILE_RTYPE		0x454C4946L
#define REPO_AMENDMENT_RTYPE 0x444E4D41L
#define REPO_DIFF_RTYPE		0x46464944L
#define REPO_TEXT_RTYPE	    0x54584554L
#define REPO_VERS_RTYPE		0x53524556L
//#define REPO_TMPL_RTYPE     0xDEAD

#define DIFF_FILE_TYPE		0x04
#define DIFF_AUX_TYPE       'AMND'

#define REPO_DIFF_TOO_BIG	"\r[ Diff too large to view, %lu bytes not shown ]"

#define REPO_CUR_VERS		3

struct repo_file {
	word id;
	Str255 filename;
	long type;
	unsigned long auxType;
	unsigned long ctime;
	unsigned long mtime;
	unsigned char flags;
#define REPO_FILE_DELETED			(1 << 0)
};

struct repo_file_attrs {
	long type;
    unsigned long auxType;
	unsigned long ctime;
	unsigned long mtime;
};

struct diffed_file {
	struct repo_file *file;
	word flags;
#define DIFFED_FILE_TEXT		(1 << 0)
#define DIFFED_FILE_METADATA	(1 << 1)
};

struct repo_amendment {
	word id;
	time_t date;
	char author[32];
	word nfiles;
	word *file_ids;
	word adds;
	word subs;
	word log_len;
	Handle log;
};

struct repo {
	struct bile *bile;
	word nfiles;
	struct repo_file **files;
	word next_file_id;
	word namendments;
	struct repo_amendment **amendments;
	word next_amendment_id;
};

struct repo *repo_open(const StringPtr file);
struct repo *repo_create(void);
void repo_close(struct repo *repo);
struct repo_amendment *repo_parse_amendment(unsigned long id, unsigned char *data,
  size_t size);
struct repo_file * repo_parse_file(unsigned long id, unsigned char *data,
  size_t size);
struct repo_file *repo_file_with_id(struct repo *repo, word id);
void repo_show_diff_text(struct repo *repo, struct repo_amendment *amendment,
  Handle te);
struct repo_file *repo_add_file(struct repo *repo);
void repo_file_mark_for_deletion(struct repo *repo, struct repo_file *file);
word repo_diff_file(struct repo *repo, struct repo_file *file);
word repo_file_changed(struct repo *repo, struct repo_file *file);
word repo_checkout_file(struct repo *repo, struct repo_file *file,
  Str255 *filename);
void repo_export_patch(struct repo *repo, struct repo_amendment *amendment,
  StringPtr filename);
void repo_amend(struct repo *repo, struct diffed_file *diffed_files,
  word nfiles, word adds, word subs, char *author, Handle log,
  word loglen, Handle diff, unsigned long difflen);
void repo_marshall_amendment(struct repo_amendment *amendment,
  char **retdata, unsigned long *retlen);
void repo_backup(struct repo *repo);

#endif