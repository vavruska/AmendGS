#line 1 "/host/amendGS/focusable.c"
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

#include <types.h>
#include <string.h>
#include <stdbool.h>
#include <window.h>
#include <memory.h>
#include <resources.h>
#include <gsos.h>

#include "focusable.h"
#include "util.h"

struct focusable **focusables = NULL;
word nfocusables = 0;

struct focusable* focusable_find(WindowPtr win) {
    word n;

    for (n = 0; n < nfocusables; n++) {
        if (focusables[n]->win == win) return focusables[n];
    }

    return NULL;
}

struct focusable* focusable_focused(void) {
    if (nfocusables && focusables[0]->visible) return focusables[0];

    return NULL;
}

void focusable_add(struct focusable *focusable) {
    word n;

    nfocusables++;
    focusables = xreallocarray(focusables, sizeof(struct focusable *),
                               nfocusables);

    if (nfocusables > 1) for (n = nfocusables - 1; n > 0; n--) focusables[n] = focusables[n - 1];

    focusables[0] = focusable;

    focusable_show(focusable);
}

bool focusable_show(struct focusable *focusable) {
    struct focusable *last, *tmp;
    word n;

    if (nfocusables > 1 && focusables[0] != focusable) {
        last = focusables[0];

        if (last->modal)
            /* other focusables cannot steal focus from modals */
            return false;

        focusables[0] = focusable;
        for (n = 1; n < nfocusables; n++) {
            tmp = focusables[n];
            focusables[n] = last;
            last = tmp;
            if (last == focusable) break;
        }
    }

    if (!focusable->visible) {
        focusable->visible = true;
        ShowWindow(focusable->win);
    }

    SelectWindow(focusable->win);
    SetPort(focusable->win);

    return true;
}

bool focusable_close(struct focusable *focusable) {
    word n;

    if (focusable->close) {
        if (!focusable->close(focusable)) {
            return false;
        }
    } else CloseWindow(focusable->win);

    for (n = 0; n < nfocusables; n++) {
        if (focusables[n] == focusable) {
            for (; n < nfocusables - 1; n++) focusables[n] = focusables[n + 1];
            break;
        }
    }

    nfocusables--;
    if (nfocusables) focusables = xreallocarray(focusables, sizeof(Ptr), nfocusables);
    else xfree(&focusables);

    if (nfocusables && focusables[0]->visible) focusable_show(focusables[0]);

    return true;
}

void focusable_hide(struct focusable *focusable) {
    word n;

    HideWindow(focusable->win);
    focusable->visible = false;

    for (n = 0; n < nfocusables; n++) {
        if (focusables[n] == focusable) {
            for (; n < nfocusables - 1; n++) focusables[n] = focusables[n + 1];
            break;
        }
    }
    focusables[nfocusables - 1] = focusable;
}

bool focusables_quit(void) {
    word tnfocusables = nfocusables;
    word n;
    struct focusable **tfocusables;
    bool quit = true;

    if (nfocusables) {
        /*
         * nfocusables and focusables array will probably be
         * modified as each focusable quits
         */
        tfocusables = xcalloc(sizeof(Ptr), tnfocusables, "tfocusables");
        memcpy(tfocusables, focusables, sizeof(Ptr) * tnfocusables);

        for (n = 0; n < tnfocusables; n++) {
            if (tfocusables[n] && !focusable_close(tfocusables[n])) {
                quit = false;
                break;
            }
        }

        xfree(&tfocusables);
    }

    return quit;
}
