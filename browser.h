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

#ifndef __BROWSER_H__
#define __BROWSER_H__

#include <stdlib.h>

#include "committer.h"
#include "repo.h"

enum {
	BROWSER_STATE_IDLE,
	BROWSER_STATE_ADD_FILE,
	BROWSER_STATE_UPDATE_FILE_LIST,
	BROWSER_STATE_UPDATE_AMENDMENT_LIST,
	BROWSER_STATE_OPEN_COMMITTER,
	BROWSER_STATE_WAITING_FOR_COMMITTER,
	BROWSER_STATE_REMOVE_FILE,
	BROWSER_STATE_DISCARD_CHANGES,
	BROWSER_STATE_EXPORT_PATCH,
	BROWSER_STATE_APPLY_PATCH,
	BROWSER_STATE_EDIT_AMENDMENT,
    BROWSER_STATE_VISUALIZE_PATCH
};

struct browser {
	word state;
	WindowPtr win;
	struct repo *repo;
    CtlRecHndl file_list;
    CtlRecHndl amendment_list;
    Handle diff_te;
    CtlRecHndl diff_button;
	struct committer *committer;
	bool need_refresh;
};

struct browser *browser_init(struct repo *repo);
void browser_update_titlebar(struct browser *browser);
void browser_show_amendment(struct browser *browser,
  struct repo_amendment *amendment);
void browser_close_committer(struct browser *browser);
void browser_export_patch(struct browser *browser);
word browser_is_all_files_selected(struct browser *browser);
word browser_selected_file_ids(struct browser *browser,
  word **selected_files);
void browser_apply_patch(struct browser *browser);
  
#endif