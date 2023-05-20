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

#include <types.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <memory.h>
#include <resources.h>
#include <control.h>
#include <list.h>
#include <gsos.h>
#include <qdaux.h>

#include "browser.h"
#include "repo.h"
#include "util.h"
#include "committer.h"

segment "browser";

#pragma toolparms 1
#pragma databank 1
pascal void amendment_list_draw_cell(RectPtr theRect, MemRecPtr MemberPtr,
                         CtlRecHndl listHandle) {
    char tmp[50];
    struct repo_amendment *amendment = (struct repo_amendment *)MemberPtr->memPtr;
    word len;

    if (amendment == NULL) {
        /* XXX: why does this happen? */
        return;
    }

    EraseRect(theRect);

    MoveTo(theRect->h1 + 4, theRect->v1 + 8);

    HLock(amendment->log);
    //draw the text up until the first newline
    for (len = 1; len < amendment->log_len; len++) {
        if ((*(amendment->log))[len - 1] == '\r') {
            break;
        }
    }
    DrawText(*(amendment->log), len);
    HUnlock(amendment->log);

    MoveTo(theRect->h1 + 4, theRect->v1 + 18);
    DrawCString(timeString(amendment->date) + 4);

    MoveTo(theRect->h1 + 160, theRect->v1 + 18);
    DrawStringWidth(dswCString, (Ref) amendment->author, 135);

    snprintf(tmp, sizeof(tmp), "%d (+), %d (-)",
             amendment->adds, amendment->subs);
    MoveTo(theRect->h1 + 300, theRect->v1 + 18);
    DrawStringWidth(dswCString, (Ref) tmp, 90);

    if (MemberPtr->memFlag & memSelected) {
        InvertRect(theRect);
    }
}
#pragma databank 0
#pragma toolparms 1

