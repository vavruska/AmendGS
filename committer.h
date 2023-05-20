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

#ifndef __COMMITTER_H__
#define __COMMITTER_H__

#include "browser.h"
#include "util.h"

#define WAIT_DLOG_ID 128

enum {
	COMMITTER_STATE_IDLE,
	COMMITTER_STATE_DO_DIFF,
	COMMITTER_STATE_DO_COMMIT
};

struct committer {
	struct browser *browser;
	WindowPtr win;
	word state;
	TERecordHndl log_te;
    TERecordHndl diff_te;
	unsigned long diff_te_len;
	CtlRecHndl commit_static;
	CtlRecHndl commit_button;
    CtlRecHndl visualize_button;
	word ndiffed_files;
	struct diffed_file *diffed_files;
	bool allow_commit;
	word diff_adds;
	word diff_subs;
	bool diff_too_big;
#define DIFF_LINE_SIZE 512
	Handle diff_line;
	size_t diff_line_pos;
#define DIFF_CHUNK_SIZE (1024 * 4)
	Handle diff_chunk;
	size_t diff_chunk_pos;
	TERecordHndl last_te;
};

void committer_init(struct browser *browser);
void committer_generate_diff(struct committer *committer);
pascal void amendment_list_draw_cell(RectPtr theRect, MemRecPtr MemberPtr,
                         CtlRecHndl listHandle);

size_t diff_output(const char *format, ...);

#endif