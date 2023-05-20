/*
 * Copyright (c) 2023 chris vavruska <chris@vavruska.com> (Apple //gs verison)
 * Copyright (c) 2022 joshua stein <jcs@jcs.org>
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
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <window.h>
#include <memory.h>
#include <resources.h>
#include <list.h>
#include <control.h>
#include <event.h>
#include <menu.h>
#include <misctool.h>
#include <gsos.h>

#include "AmendGS.h"
#include "browser.h"
#include "editor.h"
#include "focusable.h"
#include "repo.h"
#include "bile.h"
//#include "tetab.h"
#include "util.h"

bool editor_close(struct focusable *focusable);
void editor_idle(struct focusable *focusable, EventRecord *event);
void editor_update(struct focusable *focusable, EventRecord *event);
void editor_suspend(struct focusable *focusable);
void editor_resume(struct focusable *focusable);
void editor_key_down(struct focusable *focusable, EventRecord *event);
void editor_mouse_down(struct focusable *focusable, EventRecord *event);
bool editor_handle_menu(struct focusable *focusable, short menu,
                        short item);

void editor_update_menu(struct editor *editor);
void editor_save(struct editor *editor);

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

void editor_init(struct browser *browser, struct repo_amendment *amendment) {
    static Str255 title;
    struct editor *editor;
    struct focusable *focusable;
    char date[32];
    TimeRec ttm;

    editor = xmalloczero(sizeof(struct editor), "editor");
    if (!editor) {
        err(1, "Can't create editor window");
        return;
    }
    editor->browser = browser;
    editor->amendment = amendment;

    editor->win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                             EDITOR_WINDOW_ID, rWindParam1);
    //SetPort(editor->win);

    snprintf(title.text, sizeof(title.text), "%s: %s: Edit Amendment %d",
             PROGRAM_NAME, (browser->repo ? p2cstr((char *) &browser->repo->bile->filename) : "No repo open"),
             amendment->id);
    title.textLength = strlen(title.text);
    SetWTitle((char *)&title, editor->win);

    editor->author_le = GetCtlHandleFromID(editor->win, EDITOR_AUTHOR_LE_ID);
    editor->date_le = GetCtlHandleFromID(editor->win, EDITOR_DATE_LE_ID);
    editor->log_te = (TERecordHndl)GetCtlHandleFromID(editor->win, EDITOR_LOG_TEXTEDIT_ID);
    editor->save_button = GetCtlHandleFromID(editor->win, EDITOR_SAVE_BUTTON_ID);

    
    /* author */
    SetLETextByID(editor->win, EDITOR_AUTHOR_LE_ID, (StringPtr) c2pstr(amendment->author));

    /* date */
    ConvSeconds(secs2TimeRec, amendment->date, (Pointer) &ttm);
    
    snprintf(date, sizeof(date), "%04d-%02d-%02d %02d:%02d:%02d",
             (int) ttm.year + 1900, ttm.month + 1, ttm.day,
             ttm.hour, ttm.minute, ttm.second);
    SetLETextByID(editor->win, EDITOR_DATE_LE_ID, (StringPtr) c2pstr(date));

    /* log message */
    HLock(amendment->log);
    TEInsert(0x0005, (Ref)*(amendment->log), amendment->log_len, 0, 0, (Handle) editor->log_te);
    HUnlock(amendment->log);

    MakeThisCtlTarget((CtlRecHndl)editor->log_te);

    ShowWindow(editor->win);

    editor->last_te = editor->log_te;

    focusable = xmalloczero(sizeof(struct focusable), "editor focusable");
    focusable->cookie = editor;
    focusable->win = editor->win;
    focusable->modal = true;
    focusable->idle = editor_idle;
    focusable->update = editor_update;
    focusable->mouse_down = editor_mouse_down;
    focusable->key_down = editor_key_down;
    focusable->menu = editor_handle_menu;
    focusable->close = editor_close;
    focusable_add(focusable);
}

bool editor_close(struct focusable *focusable) {
    struct editor *editor = (struct editor *)focusable->cookie;

    CloseWindow(editor->win);

    xfree(&editor);

    return true;
}

void editor_idle(struct focusable *focusable, EventRecord *event) {
    struct editor *editor = (struct editor *)focusable->cookie;

    TEIdle((Handle)editor->last_te);
}

void editor_update(struct focusable *focusable, EventRecord *event) {
    short what = -1;

    if (event != NULL) {
        what = event->what;
    }

    switch (what) {
    case -1:
    case updateEvt:
        break;
    }
}

void editor_suspend(struct focusable *focusable) {
    struct editor *editor = (struct editor *)focusable->cookie;
    TEDeactivate((Handle) editor->log_te);
}

void editor_resume(struct focusable *focusable) {
    struct editor *editor = (struct editor *)focusable->cookie;
    TEActivate((Handle) editor->log_te);
}

void editor_key_down(struct focusable *focusable, EventRecord *event) {
    struct editor *editor = (struct editor *)focusable->cookie;
    editor_update_menu(editor);
}

void editor_mouse_down(struct focusable *focusable, EventRecord *event){
    struct editor *editor = (struct editor *)focusable->cookie;
    CtlRecHndl control;
    word part;

    part = FindControl(&control, event->where.h, event->where.v, editor->win);
    if (part && control == editor->save_button) {
        editor_save(editor);
    }
}

void editor_update_menu(struct editor *editor) {
    if ((*(editor->last_te))->selectionStart == (*(editor->last_te))->selectionEnd) {
        DisableMItem(EDIT_MENU_CUT_ID);
        DisableMItem(EDIT_MENU_COPY_ID);
    } else {
        EnableMItem(EDIT_MENU_CUT_ID);
        EnableMItem(EDIT_MENU_COPY_ID);
    }
    if ((*(editor->last_te))->textLength > 0) {
        EnableMItem(EDIT_MENU_SELECT_ALL_ID);
    } else {
        DisableMItem(EDIT_MENU_SELECT_ALL_ID);
    }
    EnableMItem(EDIT_MENU_PASTE_ID);

    DisableMItem(REPO_MENU_ADD_FILE_ID);
    DisableMItem(REPO_MENU_DISCARD_CHANGES_ID);
    DisableMItem(REPO_MENU_APPLY_PATCH_ID);

    DisableMItem(AMENDMENT_MENU_EDIT_ID);
    DisableMItem(AMENDMENT_MENU_EXPORT_ID);
    DisableMItem(AMENDMENT_MENU_VISUALIZE_ID);
}

bool editor_handle_menu(struct focusable *focusable, short menu, short item) {
    struct editor *editor = (struct editor *)focusable->cookie;

    switch (menu) {
    case EDIT_MENU_ID:
        switch (item) {
        case EDIT_MENU_CUT_ID:
            TECut((Handle)editor->last_te);
            editor_update_menu(editor);
            return true;
        case EDIT_MENU_COPY_ID:
            TECopy((Handle)editor->last_te);
            editor_update_menu(editor);
            return true;
        case EDIT_MENU_PASTE_ID:
            TEPaste((Handle)editor->last_te);
            editor_update_menu(editor);
            return true;
        case EDIT_MENU_SELECT_ALL_ID:
            TESetSelection((Pointer)0, (Pointer)-1, (Handle)editor->last_te);
            editor_update_menu(editor);
            return true;
        }
        break;
    }

    return false;
}

void editor_save(struct editor *editor) {
    TimeRec ttm;
    size_t len, size;
    time_t ts;
    short ret, yy, mm, dd, hh, min, ss, count = 0;
    char *data;
    Str255 date, author;
    TEInfoRec infoRec;


    GetLETextByID(editor->win, EDITOR_AUTHOR_LE_ID, &author);
    #if 0
    if (author.textLength == 0) {
        warn("Author field cannot be blank");
        return;
    #endif

    GetLETextByID(editor->win, EDITOR_DATE_LE_ID, &date);
    if (date.textLength == 0) {
        warn("Date field cannot be blank");
        return;
    }

    date.text[date.textLength] = '\0';

    ret = sscanf(date.text, "%d-%d-%d %d:%d:%d%n", &yy, &mm, &dd, &hh, &min,
                 &ss, &count);
    if (ret != 6 || count < 11) {
        warn("Date must be in YYYY-MM-DD HH:MM:SS format");
        return;
    }

    ttm.year = yy - 1900;
    ttm.month = mm - 1;
    ttm.day = dd;
    ttm.hour = hh;
    ttm.minute = min;
    ttm.second = ss;
    ts = ConvSeconds(TimeRec2Secs, 0, (Pointer) &ttm);

    TEGetTextInfo((Pointer)&infoRec, 1, (Handle)editor->log_te);
    len = infoRec.charCount;
    if (len == 0) {
        warn("Log cannot be blank");
        return;
    }

    editor->amendment->date = ts;

    len = sizeof(editor->amendment->author) - 1;
    if (author.textLength < len) {
        len = author.textLength;
    }
    memcpy(editor->amendment->author, author.text,
           len);
    editor->amendment->author[len] = '\0';

    editor->amendment->log_len = infoRec.charCount;
    if (editor->amendment->log) {
        DisposeHandle(editor->amendment->log);
    }

    TEGetText(0x1D, (Ref)&editor->amendment->log, 0L, refIsNewHandle, (Ref)NULL, (Handle)editor->log_te);

    progress("Storing updated amendment metadata...");

    repo_marshall_amendment(editor->amendment, &data, &len);

    size = bile_write(editor->browser->repo->bile, REPO_AMENDMENT_RTYPE,
                      editor->amendment->id, data, len);
    if (size != len) {
        panic("Failed storing amendment in repo file: %d",
              bile_error(editor->browser->repo->bile));
    }

    editor->browser->need_refresh = true;
    focusable_close(focusable_find(editor->win));
    progress(NULL);
}


