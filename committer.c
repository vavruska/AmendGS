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


segment "commit";

#include <types.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <memory.h>
#include <resources.h>
#include <control.h>
#include <window.h>
#include <menu.h>
#include <textedit.h>
#include <orca.h>
#include <qdaux.h>
#include <list.h>
#include <textedit.h>
#include <event.h>
#include <gsos.h>

#include "AmendGS.h"
#include "browser.h"
#include "committer.h"
#include "bile.h"
#include "diff.h"
#include "focusable.h"
#include "repo.h"
#include "settings.h"
#include "visualize.h"
//#include "tetab.h"
#include "util.h"

/* needed by diffreg */
struct stat stb1, stb2;
long diff_format, diff_context, status = 0;
char *ifdefname, *diffargs, *label[2], *ignore_pats;

struct committer *committer_diffing = NULL;

bool committer_close(struct focusable *focusable);
void committer_idle(struct focusable *focusable, EventRecord *event);
void committer_update(struct focusable *focusable, EventRecord *event);
void committer_suspend(struct focusable *focusable);
void committer_resume(struct focusable *focusable);
void committer_key_down(struct focusable *focusable, EventRecord *event);
void committer_mouse_down(struct focusable *focusable, EventRecord *event);
bool committer_handle_menu(struct focusable *focusable, word menu,
                           word item);

void committer_generate_diff(struct committer *committer);
void committer_update_menu(struct committer *committer);
void committer_commit(struct committer *committer);
void diff_append_line(char *str, size_t len, bool flush);
void diff_chunk_write(void);
void diff_finish(void);

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

void committer_init(struct browser *browser) {
    static Str255 title;
    struct committer *committer;
    struct focusable *focusable;

    committer = xmalloczero(sizeof(struct committer), "committer_init");
    committer->browser = browser;
    browser->committer = committer;

    committer->win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                                COMMIT_WINDOW_ID, rWindParam1);
    if (!committer->win) {
        err(1, "Can't create window");
    }
    SetPort(committer->win);

    snprintf(title.text, sizeof(title.text), "%s: %s: diff",
             PROGRAM_NAME, (browser->repo ? p2cstr((char *) &browser->repo->bile->filename) : "No repo open"));
    title.textLength = strlen(title.text);
    SetWTitle((char *)&title, committer->win);

    committer->log_te = (TERecordHndl)GetCtlHandleFromID(committer->win, COMMIT_COMMIT_TEXTEDIT_ID);
    committer->diff_te = (TERecordHndl)GetCtlHandleFromID(committer->win, COMMIT_DIFF_TEXTEDIT_ID);
    committer->commit_button = GetCtlHandleFromID(committer->win, COMMIT_COMMIT_BUTTON_ID);
    committer->commit_static = GetCtlHandleFromID(committer->win, COMMIT_INFO_STATIC_ID);
    committer->visualize_button = GetCtlHandleFromID(committer->win, COMMIT_VISUALIZE_BUTTON_ID);
    committer->last_te = committer->log_te;

    (*committer->commit_static)->ctlMoreFlags = 0x1000;
    (*committer->commit_static)->ctlValue = 0;
    HiliteControl(255, committer->commit_button);

    MakeThisCtlTarget((CtlRecHndl)committer->log_te);

    ShowWindow(committer->win);

    focusable = xmalloczero(sizeof(struct focusable), "committer focusable");
    focusable->cookie = committer;
    focusable->win = committer->win;
    focusable->modal = true;
    focusable->idle = committer_idle;
    focusable->update = committer_update;
    focusable->mouse_down = committer_mouse_down;
    focusable->key_down = committer_key_down;
    focusable->menu = committer_handle_menu;
    focusable->close = committer_close;
    focusable_add(focusable);

    committer->state = COMMITTER_STATE_DO_DIFF;
}

bool committer_close(struct focusable *focusable) {
    struct committer *committer = (struct committer *)focusable->cookie;

    committer->browser->committer = NULL;

    if (committer->diff_line != NULL) {
        DisposeHandle(committer->diff_line);
        committer->diff_line = NULL;
    }

    if (committer->diffed_files != NULL) {
        xfree(&committer->diffed_files);
    }

    CloseWindow(committer->win);

    xfree(&committer);

    return true;
}

void committer_idle(struct focusable *focusable, EventRecord *event) {
    struct committer *committer = (struct committer *)focusable->cookie;

    switch (committer->state) {
    case COMMITTER_STATE_IDLE:
        if (committer->last_te == committer->log_te) {
            TEIdle((Handle) committer->log_te);
        }
        break;
    case COMMITTER_STATE_DO_DIFF:
        committer_generate_diff(committer);
        committer->state = COMMITTER_STATE_IDLE;
        break;
    case COMMITTER_STATE_DO_COMMIT:
        break;
    }
}

void committer_update(struct focusable *focusable, EventRecord *event) {
    word what = -1;
    struct committer *committer = (struct committer *)focusable->cookie;

    if (event != NULL) {
        what = event->what;
    }

    switch (what) {
    case -1:
        break;
    case updateEvt:
        committer_update_menu(committer);
        break;
    }
}

void committer_suspend(struct focusable *focusable) {
    struct committer *committer = (struct committer *)focusable->cookie;

    TEDeactivate((Handle) committer->log_te);
    TEDeactivate((Handle) committer->diff_te);
}

void committer_resume(struct focusable *focusable) {
    struct committer *committer = (struct committer *)focusable->cookie;

    TEActivate((Handle) committer->log_te);
    TEActivate((Handle) committer->diff_te);
}

void committer_key_down(struct focusable *focusable, EventRecord *event) {
    struct committer *committer = (struct committer *)focusable->cookie;
    committer_update_menu(committer);
}

void committer_mouse_down(struct focusable *focusable, EventRecord *event) {
    struct committer *committer = (struct committer *)focusable->cookie;
    CtlRecHndl control;
    word part;
    part = FindControl(&control, event->where.h, event->where.v, committer->win);
    if (part && control == committer->commit_button) {
        committer_commit(committer);
    }
    if (part && control == (CtlRecHndl) committer->log_te) {
        if ((*committer->log_te)->textLength) {
            HiliteControl(0, committer->commit_button);
        } else {
            HiliteControl(255, committer->commit_button);
        }
    }
    if (part && control == (CtlRecHndl)committer->visualize_button) {
        visualize_commit(committer);
    }

    
}

void committer_update_menu(struct committer *committer) {
    TEInfoRec infoRec;
    HLock((Handle)committer->diff_te);
    HLock((Handle)committer->log_te);

    if (committer->last_te == committer->diff_te) {
        DisableMItem(EDIT_MENU_CUT_ID);
        if ((*(committer->diff_te))->selectionStart ==
                (*(committer->diff_te))->selectionEnd) {
            DisableMItem(EDIT_MENU_COPY_ID);
        } else {
            EnableMItem(EDIT_MENU_COPY_ID);
        }
        if ((*(committer->diff_te))->textLength > 0) {
            EnableMItem(EDIT_MENU_SELECT_ALL_ID);
        } else {
            DisableMItem(EDIT_MENU_SELECT_ALL_ID);
        }
        DisableMItem(EDIT_MENU_PASTE_ID);
    } else if (committer->last_te == committer->log_te) {
        if ((*(committer->log_te))->selectionStart ==
                (*(committer->log_te))->selectionEnd) {
            DisableMItem(EDIT_MENU_CUT_ID);
            DisableMItem(EDIT_MENU_COPY_ID);
        } else {
            EnableMItem(EDIT_MENU_CUT_ID);
            EnableMItem(EDIT_MENU_COPY_ID);
        }
        if ((*(committer->log_te))->textLength > 0) {
            EnableMItem(EDIT_MENU_SELECT_ALL_ID);
        } else {
            DisableMItem(EDIT_MENU_SELECT_ALL_ID);
        }
        EnableMItem(EDIT_MENU_PASTE_ID);
    }

    DisableMItem(REPO_MENU_ADD_FILE_ID);
    DisableMItem(REPO_MENU_DISCARD_CHANGES_ID);
    DisableMItem(REPO_MENU_APPLY_PATCH_ID);

    DisableMItem(AMENDMENT_MENU_EDIT_ID);
    DisableMItem(AMENDMENT_MENU_EXPORT_ID);
    DisableMItem(AMENDMENT_MENU_VISUALIZE_ID);

    HUnlock((Handle)committer->log_te);
    HUnlock((Handle)committer->diff_te);

    //GetMenuFlag(repo_memi)
    //SetMenuFlag(repo_menu, 0);
    TEGetTextInfo((Pointer) &infoRec, 1, (Handle) committer->log_te);
    if (infoRec.charCount > 0 && committer->allow_commit) {
        HiliteControl(255, committer->commit_button);
    } else {
        HiliteControl(0, committer->commit_button);
    }
}

void committer_generate_diff(struct committer *committer) {
    struct repo_file *file;
    word i, all_files;
    word *selected_files = NULL;
    word nselected_files = 0;
    WaitCursor();
    static Str255 buf;

    nselected_files = browser_selected_file_ids(committer->browser, &selected_files);

    /* default to unified diffs (should this be a setting?) */
    diff_format = D_UNIFIED;
    diff_context = 3;

    committer->diff_adds = 0;
    committer->diff_subs = 0;
    committer->ndiffed_files = 0;
    committer->allow_commit = false;
    committer->diffed_files = xcalloc(sizeof(struct diffed_file),
                                      nselected_files, "committer diffed_files");
    committer->diff_too_big = false;

    HLock((Handle)committer->diff_te);

    all_files = browser_is_all_files_selected(committer->browser);

    committer_diffing = committer;
    for (i = 0; i < nselected_files; i++) {
        file = repo_file_with_id(committer->browser->repo,
                                 selected_files[i]);
        if (file == NULL) {
            err(1, "Failed to find file in repo with id %d",
                              selected_files[i]);
            continue;
        }

        if (all_files && !repo_file_changed(committer->browser->repo,
                                            file)) {
            progress("Skipping unchanged %s...", file->filename);
            continue;
        }

        committer->diffed_files[committer->ndiffed_files].file = file;
        committer->diffed_files[committer->ndiffed_files].flags =
            DIFFED_FILE_METADATA;


        progress("Diffing %s...", file->filename.text);
        if (repo_diff_file(committer->browser->repo, file)) {
            committer->diffed_files[committer->ndiffed_files].flags |=
                DIFFED_FILE_TEXT;
            committer->allow_commit = true;
        }
        diff_finish();
        committer->ndiffed_files++;
        if (committer->diff_too_big) {
            break;
        }
    }

    committer_diffing = NULL;

    HUnlock((Handle)committer->diff_te);

    progress(NULL);

    if (committer->diff_too_big) {
        browser_close_committer(committer->browser);
        return;
    }

done_diffing:
    if (selected_files != NULL) {
        xfree(&selected_files);
    }
    InitCursor();
    if (!committer->allow_commit) {
        warnx("No changes detected");
        browser_close_committer(committer->browser);
    } else {
        word len = snprintf((char *)buf.text, sizeof(buf.text), "%d (+), %d (-)",
          committer->diff_adds, committer->diff_subs);
        (*committer->commit_static)->ctlData = (long) buf.text;
        (*committer->commit_static)->ctlValue = len;
        DrawOneCtl(committer->commit_static);
        ShowControl(committer->commit_static);
    }
}

void committer_commit(struct committer *committer) {
    struct browser *browser;
    word loglen, difflen;
    Handle logText;
    Handle diffText;

    HLock((Handle)committer->log_te);
    HLock((Handle)committer->diff_te);

    WaitCursor();

    progress("Committing changes...");

    loglen = TEGetText(0x1D, (Ref) &logText, 0L, refIsNewHandle, (Ref) NULL, (Handle) committer->log_te);
    difflen = TEGetText(0x1D, (Ref) &diffText, 0L, refIsNewHandle, (Ref) NULL, (Handle) committer->diff_te);
    repo_amend(committer->browser->repo, committer->diffed_files,
               committer->ndiffed_files, committer->diff_adds, committer->diff_subs,
               settings.author, logText, loglen, diffText, difflen); 

    HUnlock((Handle)committer->diff_te);
    HUnlock((Handle)committer->log_te);

    progress(NULL);
    InitCursor();

    browser = committer->browser;
    browser_close_committer(committer->browser);
    browser->state = BROWSER_STATE_UPDATE_AMENDMENT_LIST;
}

bool committer_handle_menu(struct focusable *focusable, word menu, word item) {
    struct committer *committer = (struct committer *)focusable->cookie;

    switch (menu) {
    case EDIT_MENU_ID:
        switch (item) {
        case EDIT_MENU_CUT_ID:
            if (committer->last_te == committer->log_te) {
                TECut((Handle)committer->log_te);
                committer_update_menu(committer);
            }
            return true;
        case EDIT_MENU_COPY_ID:
            if (committer->last_te) TECopy((Handle)committer->last_te);
            committer_update_menu(committer);
            return true;
        case EDIT_MENU_PASTE_ID:
            if (committer->last_te == committer->log_te) {
                TEPaste((Handle)committer->log_te);
                committer_update_menu(committer);
            }
            return true;
        case EDIT_MENU_SELECT_ALL_ID:
            if (committer->last_te) {
                //TESetSelect(0, 1024 * 32, committer->last_te);
            }
            committer_update_menu(committer);
            return true;
        }
        break;
    }

    return false;
}

size_t diff_output(const char *format, ...) {
    va_list argptr;
    size_t len, last_pos, last_line, i;

    if (committer_diffing == NULL) panic("diff_output without committer_diffing");

    if (committer_diffing->diff_line == NULL) {
        committer_diffing->diff_line = xNewHandle(DIFF_LINE_SIZE);
        committer_diffing->diff_line_pos = 0;
        HLock(committer_diffing->diff_line);
    }

    last_pos = committer_diffing->diff_line_pos;
    last_line = 0;

    va_start(argptr, format);

    if (format[0] == '%' && format[1] == 'c' && format[2] == '\0') {
        /* avoid having to vsprintf just to append 1 character */
        (*(committer_diffing->diff_line))[last_pos] = va_arg(argptr, int);
        len = 1;
    } else len = vsprintf(*(committer_diffing->diff_line) + last_pos, format,
                          argptr);
    va_end(argptr);
    committer_diffing->diff_line_pos += len;
    if (committer_diffing->diff_line_pos >= DIFF_LINE_SIZE) err(1, "diff line overflow!");

    if (len == 1 && (*(committer_diffing->diff_line))[last_pos] != '\r') return 1;

    for (i = last_pos; i < committer_diffing->diff_line_pos; i++) {
        if (((char *)*(committer_diffing->diff_line))[i] == '\r') {
            diff_append_line(*(committer_diffing->diff_line) + last_line,
                             i - last_line + 1, false);
            last_line = i + 1;
        }
    }

    if (last_line == committer_diffing->diff_line_pos) {
        committer_diffing->diff_line_pos = 0;
    } else if (last_line > 0) {
        memmove(*(committer_diffing->diff_line),
                *(committer_diffing->diff_line) + last_line,
                committer_diffing->diff_line_pos - last_line);
        committer_diffing->diff_line_pos -= last_line;
    }

    return len;
}

void diff_append_line(char *str, size_t len, bool flush) {

    if (committer_diffing == NULL) {
        panic("diff_append_line without committer_diffing");
    }

    if (committer_diffing->diff_chunk == NULL) {
        committer_diffing->diff_chunk = xNewHandle(DIFF_CHUNK_SIZE);
        committer_diffing->diff_chunk_pos = 0;
    }

    if (str[0] == '-' && str[1] != '-') {
        committer_diffing->diff_subs++;
    } else if (str[0] == '+' && str[1] != '+') {
        committer_diffing->diff_adds++;
    }

    if (committer_diffing->diff_chunk_pos + len >= DIFF_CHUNK_SIZE) {
        diff_chunk_write();
    }

    HLock(committer_diffing->diff_chunk);
    memcpy(*(committer_diffing->diff_chunk) +
           committer_diffing->diff_chunk_pos, str, len);
    HUnlock(committer_diffing->diff_chunk);
    committer_diffing->diff_chunk_pos += len;

    if (flush) {
        diff_chunk_write();
    }
}

void diff_chunk_write(void) {
    static bool warned = false;
    if (committer_diffing == NULL) {
        panic("diff_chunk_write without committer_diffing");
    }

    if ((committer_diffing->diff_te_len + committer_diffing->diff_chunk_pos) > MAX_TEXTEDIT_SIZE) {
        if (warned == false) {
            InitCursor();
            warn("Total diff can not be larger than 32k.");
            committer_diffing->diff_too_big = true;
            warned = true;
            WaitCursor();
        }
        return;
    }
    warned = false;
    HLock(committer_diffing->diff_chunk);
    (*committer_diffing->diff_te)->textFlags &= ~fReadOnly;
    TEInsert(0x0005, (Ref) *(committer_diffing->diff_chunk), committer_diffing->diff_chunk_pos, 0, 0, (Handle) committer_diffing->diff_te);
    (*committer_diffing->diff_te)->textFlags |= fReadOnly;
    HUnlock(committer_diffing->diff_chunk);

    committer_diffing->diff_te_len += committer_diffing->diff_chunk_pos;
    committer_diffing->diff_chunk_pos = 0;
}

void diff_finish(void) {
    if (committer_diffing == NULL) {
        panic("diff_finish without committer_diffing");
    }

    if (committer_diffing->diff_line != NULL) {
        if (committer_diffing->diff_line_pos) {
            diff_append_line(*(committer_diffing->diff_line),
                             committer_diffing->diff_line_pos, true);
        }

        DisposeHandle(committer_diffing->diff_line);
        committer_diffing->diff_line = NULL;
    }

    if (committer_diffing->diff_chunk != NULL) {
        if (committer_diffing->diff_chunk_pos) {
            diff_chunk_write();
        }
        DisposeHandle(committer_diffing->diff_chunk);
        committer_diffing->diff_chunk = NULL;
    }
    TEScroll(0, 0, 0, (Handle) committer_diffing->diff_te);
}

