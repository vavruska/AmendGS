/*
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

segment "browser";

#include <types.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <control.h>
#include <list.h>
#include <memory.h>
#include <menu.h>
#include <resources.h>
#include <event.h>
#include <textedit.h>
#include <window.h>
#include <qdaux.h>
#include <stdfile.h>
#include <gsos.h>
#include <orca.h>

#include "AmendGS.h"
#include "browser.h"
#include "bile.h"
#include "committer.h"
#include "diff.h"
#include "editor.h"
#include "focusable.h"
#include "patch.h"
#include "repo.h"
#include "settings.h"
//#include "tetab.h"
#include "util.h"
#include "visualize.h"

#define FILE_LIST_FONT geneva
#define FILE_LIST_FONT_SIZE 10
#define DIFF_BUTTON_FONT geneva
#define DIFF_BUTTON_FONT_SIZE 12

#define PADDING 10

extern word programID;
MemRecHndl fileListHndl = NULL;
MemRecHndl amendListHndl = NULL;

bool browser_close(struct focusable *focusable);
void browser_idle(struct focusable *focusable, EventRecord *event);
void browser_update_menu(struct browser *browser);
void browser_update(struct focusable *focusable, EventRecord *event);
void browser_show_amendment(struct browser *browser,
                            struct repo_amendment *amendment);
void browser_mouse_down(struct focusable *focusable, EventRecord *event);
bool browser_handle_menu(struct focusable *focusable, word menu,
                         word item);

void browser_add_files(struct browser *browser);
void browser_filter_amendments(struct browser *browser);
void browser_discard_changes(struct browser *browser);
void browser_edit_amendment(struct browser *browser);
void browser_visualize_amendment(struct browser *browser);

Pattern fill_pattern;

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

void browser_idle(struct focusable *focusable, EventRecord *event) {
    struct browser *browser = (struct browser *)focusable->cookie;

    switch (browser->state) {
    case BROWSER_STATE_IDLE:
        if (browser->need_refresh) {
            browser->need_refresh = false;
            browser->state = BROWSER_STATE_UPDATE_AMENDMENT_LIST;
        }
        break;
    case BROWSER_STATE_ADD_FILE:
        if (repo_add_file(browser->repo) == NULL) {
            browser->state = BROWSER_STATE_IDLE;
        } else {
            browser->state = BROWSER_STATE_UPDATE_FILE_LIST;
        }
        break;
    case BROWSER_STATE_UPDATE_FILE_LIST:
        browser_add_files(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_UPDATE_AMENDMENT_LIST:
        browser_filter_amendments(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_OPEN_COMMITTER:
        committer_init(browser);
        browser->state = BROWSER_STATE_WAITING_FOR_COMMITTER;
        break;
    case BROWSER_STATE_WAITING_FOR_COMMITTER:
        break;
    case BROWSER_STATE_DISCARD_CHANGES:
        browser_discard_changes(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_EXPORT_PATCH:
        browser_export_patch(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_APPLY_PATCH:
        browser_apply_patch(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_EDIT_AMENDMENT:
        browser_edit_amendment(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    case BROWSER_STATE_VISUALIZE_PATCH:
        browser_visualize_amendment(browser);
        browser->state = BROWSER_STATE_IDLE;
        break;
    }
}

struct browser*
browser_init(struct repo *repo) {
    static Str255 title; 
    struct browser *browser;
    struct focusable *focusable;

    browser = xmalloczero(sizeof(struct browser), "browser");
    browser->state = BROWSER_STATE_IDLE;
    browser->repo = repo;

    //GetIndPattern(&fill_pattern, sysPatListID, 22);

    browser->win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                              BROWSER_WINDOW_ID, rWindParam1);
    if (!browser->win) {
        err(1, "Can't create window");
    }
    SetPort(browser->win);

    sprintf(title.text, " %s: %s ", PROGRAM_NAME, p2cstr((char *) &repo->bile->filename));
    title.textLength = strlen(title.text);
    SetWTitle((Pointer) &title, browser->win);

    browser->file_list = GetCtlHandleFromID(browser->win, BROWSER_FILE_LIST_ID);
    browser->diff_te = (Handle)  GetCtlHandleFromID(browser->win, BROWSER_DIFF_TEXTEDIT_ID);
    browser->diff_button = GetCtlHandleFromID(browser->win, BROWSER_DIFF_BUTTON_ID);
    browser->amendment_list = GetCtlHandleFromID(browser->win, BROWSER_AMEND_LIST_ID);
    HiliteControl(255, browser->diff_button);

    ShowWindow(browser->win);

    browser_update_menu(browser);
    browser_add_files(browser);

    focusable = xmalloczero(sizeof(struct focusable), "focusable");
    focusable->cookie = browser;
    focusable->win = browser->win;
    focusable->idle = browser_idle;
    focusable->update = browser_update;
    focusable->mouse_down = browser_mouse_down;
    focusable->menu = browser_handle_menu;
    focusable->close = browser_close;
    focusable_add(focusable);

    progress(NULL);

    return browser;
}

bool
browser_close(struct focusable *focusable) {
    struct browser *browser = (struct browser *)focusable->cookie;

    if (browser->committer) browser_close_committer(browser);

    if (browser->repo) repo_close(browser->repo);

    CloseWindow(browser->win);

    xfree(&browser);

    menuDefaults();

    return true;
}

void browser_close_committer(struct browser *browser) {
    struct focusable *f;

    if (browser->committer) {
        f = focusable_find(browser->committer->win);
        if (f) {
            focusable_close(f);
        }
    }
}

#pragma toolparms 1
#pragma databank 1
pascal void DrawFileMember(RectPtr theRect, MemRecPtr MemberPtr,
                           CtlRecHndl listHandle) {
    struct repo_file *file = (struct repo_file *)MemberPtr->memPtr;

    EraseRect(theRect);
    MoveTo(theRect->h1 + 4, theRect->v2 - 2);
    if (file->flags & REPO_FILE_DELETED) { 
        DrawCString("~");
    }
    DrawString((Pointer) &file->filename);
    if (file->flags & REPO_FILE_DELETED) { 
        DrawCString("~");
    }

    if (MemberPtr->memFlag & memSelected) {
        InvertRect(theRect);
    }
}
#pragma databank 0
#pragma toolparms 0

void browser_add_files(struct browser *browser) {
    word i;
    static struct repo_file allFiles;

    if (fileListHndl) {
        DisposeHandle((Handle) fileListHndl);
    }
    fileListHndl = (MemRecHndl)NewHandle((browser->repo->nfiles + 1) * sizeof(MemRec),
                                              programID, attrFixed + attrLocked, NULL);
    
    browser_show_amendment(browser, NULL);

    (*fileListHndl)[0].memPtr = (pointer) &allFiles;
    strcpy((char *) allFiles.filename.text,"[All Files]");
    allFiles.filename.textLength = strlen(allFiles.filename.text);
    (*fileListHndl)[0].memFlag = (browser->repo->nfiles > 0) << 6;

    /* fill in files */
    for (i = 0; i < browser->repo->nfiles; i++) {
        (*fileListHndl)[i + 1].memPtr = (Pointer) browser->repo->files[i];
        (*fileListHndl)[i + 1].memFlag = 0;
    }

    NewList2((Pointer)&DrawFileMember, 1, (Ref)fileListHndl, refIsHandle, browser->repo->nfiles + 1, (Handle)browser->file_list);

    browser_filter_amendments(browser);
}

word browser_is_all_files_selected(struct browser *browser) {

    if (browser->repo->nfiles == 0) {
        return 0;
    }

    //item 1 is the "all files" list entry
    if (NextMember2(0, (Handle) browser->file_list) == 1) {
        return 1;
    }

    return 0;
}

word browser_selected_file_ids(struct browser *browser, word **selected_files) {
    word nselected_files = 0, i;

    if (browser->repo->nfiles == 0) {
        *selected_files = NULL;
        return 0;
    }

    *selected_files = xcalloc(browser->repo->nfiles, sizeof(word),
                              "selected_files");

    if (browser_is_all_files_selected(browser)) {
        nselected_files = browser->repo->nfiles;
        for (i = 0; i < browser->repo->nfiles; i++) {
            (*selected_files)[i] = browser->repo->files[i]->id;
        }
    } else {
        i = 0;
        while (i = NextMember2(i, (Handle) browser->file_list)) {
            (*selected_files)[nselected_files] = browser->repo->files[i-2]->id;
            nselected_files++;
        }
    }
    return nselected_files;
}

void browser_filter_amendments(struct browser *browser) {
    struct repo_amendment *amendment;
    word i, j, k;
    bool add;
    word *selected_files = NULL;
    word nselected_files = 0;
    word aIdx = 0;
    extern MemRecHndl amendListHndl;

    browser_show_amendment(browser, NULL);

    /* fill in amendments for selected files */
    nselected_files = browser_selected_file_ids(browser, &selected_files);
    if (amendListHndl) {
        DisposeHandle((Handle) amendListHndl);
        amendListHndl = NULL;
    }

    if (nselected_files) {
        amendListHndl = (MemRecHndl)NewHandle(browser->repo->namendments * sizeof(MemRec),
                                                  programID, attrFixed + attrLocked, NULL);
        for (i = 0; i < browser->repo->namendments; i++) {
            add = false;
            amendment = browser->repo->amendments[i];
            for (j = 0; j < amendment->nfiles; j++) {
                for (k = 0; k < nselected_files; k++) {
                    if (selected_files[k] == amendment->file_ids[j]) {
                        add = true;
                        break;
                    }
                }
                if (add) {
                    break;
                }
            }

            if (add) {
                (*amendListHndl)[aIdx].memPtr = (Pointer) browser->repo->amendments[i];
                (*amendListHndl)[aIdx].memFlag = 0;

                aIdx++;
            }
        }

        NewList2((Pointer)&amendment_list_draw_cell, -1, (Ref)amendListHndl, refIsHandle, aIdx, (Handle)browser->amendment_list);
        xfree(&selected_files);
    }
}

void browser_show_amendment(struct browser *browser,
                       struct repo_amendment *amendment) {

    if (amendment == NULL) {
        long start = 0, end = -1;
        TERecordHndl teRec = (TERecordHndl)browser->diff_te;

        (*teRec)->textFlags &= ~fReadOnly;
        TESetSelection((Pointer) start, (Pointer) end, (Handle) teRec);
        TEClear((Handle)teRec);
        (*teRec)->textFlags |= fReadOnly;
    } else {
        WaitCursor();
        repo_show_diff_text(browser->repo, amendment, browser->diff_te);
        InitCursor();
    }

    browser_update_menu(browser);
}

void browser_discard_changes(struct browser *browser) {
    Str255 buf;
    struct repo_file *file;
    word *selected = NULL;
    word nselected = 0, i, error;
    SFReplyRec reply;

    nselected = browser_selected_file_ids(browser, &selected);
    for (i = 0; i < nselected; i++) {
        file = repo_file_with_id(browser->repo, selected[i]);

        snprintf(buf.text, sizeof(buf.text), "Save %s:", p2cstr((char *) &file->filename));
        buf.textLength = strlen(buf.text);

        SFPutFile(0x15, 0x15, (Pointer) &buf, NULL, 254, &reply);
        if (!reply.good) {
            break;
        }

        error = repo_checkout_file(browser->repo, file, (StringPtr) &reply.fullPathname);
        if (error) {
            break;
        }
    }
}

void
browser_export_patch(struct browser *browser) {
    struct repo_amendment *amendment;
    word *selected_files = NULL;
    word nselected = 0;
    word match=0,i,j,k;
    word listid = 1;
    word selected = 0;

    nselected = browser_selected_file_ids(browser, &selected_files);
    if (nselected == 0) {
        /* can't select nothing, do nothing*/
        return;
    } else {
        browser_show_amendment(browser, NULL);
        while (selected = NextMember2(selected, (handle) browser->amendment_list)) {
            for (i = 0; i < browser->repo->namendments; i++) {
                match = 0;
                amendment = browser->repo->amendments[i];
                for (j = 0; j < amendment->nfiles; j++) {
                    for (k = 0; k < nselected; k++) {
                        if (selected_files[k] == amendment->file_ids[j]) {
                            if (selected == listid) {
                                match = 1;
                                break;
                            } else {
                                listid++;
                            }
                        }
                    }
                    if (match) {
                        break;
                    }
                }
                if (match) {
                    break;
                }
            }
        }
        if (selected_files) {
            xfree(&selected_files);
        }
    }
    if (match) {
        SFReplyRec reply;

        SFPutFile(0x15, 0x15, (Pointer) &"\pSave patch as:", NULL, 254, &reply);
        if (!reply.good) {
            return;
        }
        repo_export_patch(browser->repo, amendment, (StringPtr) reply.fullPathname);
    }
}

void browser_apply_patch(struct browser *browser) {
    SFReplyRec reply;

    SFGetFile(0x15, 0x15, (Pointer) &"\pSelect Patch File:", NULL, NULL, &reply);

    if (!reply.good) {
        return;
    }

    patch_process(browser->repo, (StringPtr) &reply.fullPathname);
}

void browser_edit_amendment(struct browser *browser) {
    struct repo_amendment *amendment;
    word *selected_files = NULL;
    word nselected = 0;
    word match,i,j,k;
    word listid = 1;
    word selected = 0;

    nselected = browser_selected_file_ids(browser, &selected_files);
    if (nselected == 0) {
        /* can't select nothing, do nothing*/
        return;
    } else {
        browser_show_amendment(browser, NULL);
        while (selected = NextMember2(selected, (handle) browser->amendment_list)) {
            for (i = 0; i < browser->repo->namendments; i++) {
                match = 0;
                amendment = browser->repo->amendments[i];
                for (j = 0; j < amendment->nfiles; j++) {
                    for (k = 0; k < nselected; k++) {
                        if (selected_files[k] == amendment->file_ids[j]) {
                            if (selected == listid) {
                                editor_init(browser, amendment);
                                match = 1;
                                break;
                            } else {
                                listid++;
                            }
                        }
                    }
                    if (match) {
                        break;
                    }
                }
                if (match) {
                    break;
                }
            }
        }
        if (selected_files) {
            xfree(&selected_files);
        }
    }
}

void browser_visualize_amendment(struct browser *browser) {
    word nselected = 0, i, j, k, selected, amend_files;
    word *selected_files = NULL;
    struct repo_file **repo_files;
    struct repo_amendment *amendment;
    extern MemRecHndl amendListHndl;

    nselected = browser_selected_file_ids(browser, &selected_files);

    selected = NextMember2(0, (handle)browser->amendment_list);
    amendment = (struct repo_amendment *)(*amendListHndl)[selected-1].memPtr;

    repo_files = xcalloc(amendment->nfiles, sizeof(struct repo_file *), 
                         "browser_visualize_amendment");
    amend_files = 0;
    for (k = 0; k < browser->repo->nfiles; k++) {
        for (i = 0; i < amendment->nfiles; i++) {
            if (browser->repo->files[k]->id == amendment->file_ids[i]) {
                for (j = 0; j < nselected; j++) {
                    if (selected_files[j] == amendment->file_ids[i]) {
                        repo_files[amend_files++] = browser->repo->files[k];
                        break;
                    }
                }
            }
        }
    }

    visualize_amendment(browser, amendment, amend_files, repo_files);

    xfree(&selected_files);
    xfree(&repo_files);
}

void browser_update_menu(struct browser *browser) {
    TERecordPtr diff = *((TERecordHndl) browser->diff_te);
    WindowPtr port = GetPort();

    SetPort(browser->win);

    HLock(browser->diff_te);

    menuDefaults();

    DisableMItem(EDIT_MENU_CUT_ID);

    if (diff->selectionStart == diff->selectionEnd) {
        DisableMItem(EDIT_MENU_COPY_ID);
    } else {
        EnableMItem(EDIT_MENU_COPY_ID);
    }

    DisableMItem(EDIT_MENU_PASTE_ID);

    if (diff->textLength == 0) {
        DisableMItem(EDIT_MENU_SELECT_ALL_ID);
    } else {
        EnableMItem(EDIT_MENU_SELECT_ALL_ID);
    }

    HUnlock(browser->diff_te);

    if (NextMember2(0, (Handle) browser->file_list) == 0) {
        HiliteControl(255, browser->diff_button);
    } else {
        HiliteControl(0, browser->diff_button);
    }

    if (!browser->committer) {
        EnableMItem(FILE_MENU_CLOSE_ID);
        EnableMItem(REPO_MENU_ADD_FILE_ID);
        EnableMItem(REPO_MENU_DISCARD_CHANGES_ID);
        EnableMItem(REPO_MENU_APPLY_PATCH_ID);
    }

    if (NextMember2(0, (Handle) browser->amendment_list)) {
        EnableMItem(AMENDMENT_MENU_EDIT_ID);
        EnableMItem(AMENDMENT_MENU_EXPORT_ID);
        EnableMItem(AMENDMENT_MENU_VISUALIZE_ID);
    } else {
        DisableMItem(AMENDMENT_MENU_EDIT_ID);
        DisableMItem(AMENDMENT_MENU_EXPORT_ID);
        DisableMItem(AMENDMENT_MENU_VISUALIZE_ID);
    }
    SetPort(port);
}

void browser_update(struct focusable *focusable, EventRecord *event) {
    struct browser *browser = (struct browser *)focusable->cookie;
    word what = -1;

    if (event != NULL) {
        what = event->what;
    }

    switch (what) {
    default:
        browser_update_menu(browser);

        break;
    }
}

void browser_mouse_down(struct focusable *focusable, EventRecord *event) {
    CtlRecHndl control;
    struct browser *browser = (struct browser *)focusable->cookie;
    word i = 0, j, k;
    struct repo_amendment *amendment = NULL;
    word *selected_files = NULL;
    word nselected = 0;
    word part, match;
    word listid = 1;
    word selected = 0;

    part = FindControl(&control, event->where.h, event->where.v, browser->win);
    if ((part) && (control == ((CtlRecHndl) browser->diff_button))) {
        browser->state = BROWSER_STATE_OPEN_COMMITTER;
    }

    if (event->wmTaskData4 == BROWSER_FILE_LIST_ID) {
        browser_update_menu(browser);
        browser_filter_amendments(browser);
    } else if (event->wmTaskData4 == BROWSER_AMEND_LIST_ID) {
        nselected = browser_selected_file_ids(browser, &selected_files);
        if (nselected == 0) {
            /* can't select nothing, select 'all files' */
            if (nselected != browser->repo->nfiles) {
                browser_filter_amendments(browser);
            }
        } else {
            WaitCursor();
            browser_show_amendment(browser, NULL);
            while (selected = NextMember2(selected, (handle) browser->amendment_list)) {
                for (i = 0; i < browser->repo->namendments; i++) {
                    match = 0;
                    amendment = browser->repo->amendments[i];
                    for (j = 0; j < amendment->nfiles; j++) {
                        for (k = 0; k < nselected; k++) {
                            if (selected_files[k] == amendment->file_ids[j]) {
                                if (selected == listid) {
                                    browser_show_amendment(browser, amendment);
                                    match = 1;
                                    break;
                                } else {
                                    listid++;
                                }
                            }
                        }
                        if (match) {
                            break;
                        }
                    }
                    if (match) {
                        break;
                    }
                }
            }
            InitCursor();
        }
        if (selected_files) {
            xfree(&selected_files);
        }
        TEScroll(teScrollAbsTop, 0, 0, browser->diff_te);
    }
}

bool browser_handle_menu(struct focusable *focusable, word menu, word item) {
    struct browser *browser = (struct browser *)focusable->cookie;

    switch (menu) {
    case EDIT_MENU_ID:
        switch (item) {
        case EDIT_MENU_COPY_ID:
            TECopy(browser->diff_te);
            return true;
        case EDIT_MENU_SELECT_ALL_ID:
            TESetSelection((Pointer)0, (Pointer)(1024 * 32L), browser->diff_te);
            return true;
        }
        break;
    case REPO_MENU_ID:
        switch (item) {
        case REPO_MENU_ADD_FILE_ID:
            browser->state = BROWSER_STATE_ADD_FILE;
            return true;
        case REPO_MENU_DISCARD_CHANGES_ID:
            browser->state = BROWSER_STATE_DISCARD_CHANGES;
            return true;
        case REPO_MENU_APPLY_PATCH_ID:
            browser->state = BROWSER_STATE_APPLY_PATCH;
            return true;
        }
        break;
    case AMENDMENT_MENU_ID:
        switch (item) {
        case AMENDMENT_MENU_EDIT_ID:
            browser->state = BROWSER_STATE_EDIT_AMENDMENT;
            return true;
        case AMENDMENT_MENU_EXPORT_ID:
            browser->state = BROWSER_STATE_EXPORT_PATCH;
            return true;
        case AMENDMENT_MENU_VISUALIZE_ID:
            browser->state = BROWSER_STATE_VISUALIZE_PATCH;
            return true;
        }
        break;
    }

    return false;
}
