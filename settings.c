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
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <memory.h>
#include <resources.h>
#include <loader.h>
#include <gsos.h>
#include <control.h>
#include <event.h>

#include "AmendGS.h"
#include "settings.h"
//#include "tetab.h"
#include "util.h"

struct settings settings = { 0 };
extern word programID;

void settings_load(void) {
    Handle author;
    word resourceFileID = OpenResourceFile(readWriteEnable, NULL, LGetPathname2(programID, 1));

    author = LoadResource(rCString, STR_AUTHOR_ID);
    if (author) {
        memcpy(settings.author, *author, sizeof(settings.author));
        settings.author[sizeof(settings.author)] = '\0';
        ReleaseResource(-1, rCString, STR_AUTHOR_ID);
    } else {
        snprintf(settings.author, sizeof(settings.author), "");
    }

#if 0
    settings.tabwidth = (short)xGetStringAsLong(STR_TABWIDTH_ID);
    if (settings.tabwidth < 1 || settings.tabwidth > 20) {
        warn("Bogus tabwidth resource %d", settings.tabwidth);
        settings.tabwidth = 4;
    }

    TETabWidth = settings.tabwidth;
#endif
    CloseResourceFile(resourceFileID);
}

void settings_save(void) {
    Handle res;
    size_t l;
    word resourceFileID = OpenResourceFile(readWriteEnable, NULL, LGetPathname2(programID, 1));

    res = LoadResource(rCString, STR_AUTHOR_ID);
    if (res == NULL) {
        res = xNewHandle(16);
        AddResource(res,  attrNoPurge, rCString, STR_AUTHOR_ID);
        res = LoadResource(rCString, STR_AUTHOR_ID);
    }

    HLock(res);
    l = strlen(settings.author);
    //xSetHandleSize(res, l + 1);
    memcpy(*res, settings.author, l + 1);
    MarkResourceChange(true, rCString, STR_AUTHOR_ID);
    ReleaseResource(-1, rCString, STR_AUTHOR_ID);

#if 0
    res = xGetResource('STR ', STR_TABWIDTH_ID);
    HLock(res);
    snprintf(tmp, sizeof(tmp), "%d", settings.tabwidth);
    l = strlen(tmp);
    xSetHandleSize(res, l + 1);
    memcpy(*res, tmp, l + 1);
    CtoPstr(*res);
    ChangedResource(res);
    ReleaseResource(res);

    TETabWidth = settings.tabwidth;
#endif
    CloseResourceFile(resourceFileID);
}

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

void settings_edit(void) {
    WindowPtr win;
    EventRecord currentEvent;
    bool done = false;
    long id = 0;
    Str255 newAuthor;

    win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                     SETTINGS_WINDOW_ID, rWindParam1);

    SetLETextByID(win, SETTINGS_AUTHOR_TE_ID, (StringPtr)c2pstr(settings.author));

    ShowWindow(win);
    #if 0
    GetDItem(dlg, SETTINGS_TABWIDTH_ID, &itype, &ihandle, &irect);
    snprintf((char *)&txt, sizeof(txt), "%d", settings.tabwidth);
    CtoPstr(txt);
    SetIText(ihandle, txt);
    #endif

    currentEvent.wmTaskMask = 0x001FFFEFL;
    while (!done) {
        id = DoModalWindow(&currentEvent, NULL, (VoidProcPtr)NULL, NULL, 0x4028);
        switch (id) {
        case SETTINGS_ACCEPT_BUTTON_ID:
        case SETTINGS_CANCEL_BUTTON_ID:
            done = true;
            break;
        default:
            break;
        }
    }

    if (id == SETTINGS_ACCEPT_BUTTON_ID) {
        GetLETextByID(win, SETTINGS_AUTHOR_TE_ID, &newAuthor);
        memset(settings.author, 0, sizeof(settings.author));
        strncpy(settings.author, newAuthor.text, newAuthor.textLength);
        #if 0
        GetDItem(dlg, SETTINGS_TABWIDTH_ID, &itype, &ihandle, &irect);
        GetIText(ihandle, txt);
        PtoCstr(txt);
        settings.tabwidth = atoi((char *)&txt);
        #endif
        settings_save();
    }

    CloseWindow(win);
}
