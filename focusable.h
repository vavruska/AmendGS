/*
 * Copyright (c) 2023 chris vavruska <chris@vavruska.com> (Apple //gs verison)
 * Copyright (c) 2021-2022 joshua stein <jcs@jcs.org>
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

#ifndef __FOCUSABLE_H__
#define __FOCUSABLE_H__

#include "util.h"

struct focusable {
	WindowPtr win;
	bool visible;
	void *cookie;
	bool modal;
	word (*wait_type)(struct focusable *focusable);
	void (*idle)(struct focusable *focusable, EventRecord *event);
	void (*update)(struct focusable *focusable, EventRecord *event);
	void (*key_down)(struct focusable *focusable, EventRecord *event);
	void (*mouse_down)(struct focusable *focusable, EventRecord *event);
	bool (*menu)(struct focusable *focusable, word menu, word item);
	void (*suspend)(struct focusable *focusable, EventRecord *event);
	void (*resume)(struct focusable *focusable, EventRecord *event);
	bool (*close)(struct focusable *focusable);
	void (*atexit)(struct focusable *focusable);
};
extern struct focusable **focusables;
extern word nfocusables;

struct focusable * focusable_find(WindowPtr win);
struct focusable * focusable_focused(void);
void focusable_add(struct focusable *focusable);
bool focusable_show(struct focusable *focusable);
bool focusable_close(struct focusable *focusable);
void focusable_hide(struct focusable *focusable);
bool focusables_quit(void);

#endif