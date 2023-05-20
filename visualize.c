
/*
 * Copyright (c) 2023 Chris Vavruska <chris@vavruska.com> (Apple //gs verison)
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

segment "visualize";

#include <types.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <window.h>
#include <event.h>
#include <control.h>
#include <quickdraw.h>
#include <resources.h>
#include <list.h>
#include <gsos.h>
#include <textedit.h>
#include <intmath.h>
#include <orca.h>
#include <memory.h>
#include <event.h>

#include "AmendGS.h"
#include "committer.h"
#include "repo.h"
#include "bile.h"
#include "browser.h"
#include "util.h"
#include "visualize.h"
#include "patch.h"

struct buffer {
    handle buffer;
    size_t bufSize;
    long position;
    word column;
    word maxLineLength;
};

struct visualize {
    WindowPtr win;
    Handle diffText;
    longword diffLen;
    CtlRecHndl doneButton;
    CtlRecHndl leftScroll;
    CtlRecHndl rightScroll;
    CtlRecHndl vertScroll;
    CtlRecHndl leftRect;
    CtlRecHndl rightRect;
    word lines;
    struct buffer leftBuffer;
    struct buffer rightBuffer;
};

enum {
    VISUALIZE_STATE_HEADER_FROM,
    VISUALIZE_STATE_TO,
    VISUALIZE_STATE_CHUNK_HEADER,
    VISUALIZE_STATE_SETUP_CHUNK,
    VISUALIZE_STATE_CONTEXT
};


int visualize_rollback(struct visualize *visualize, struct repo *repo,
                        struct repo_amendment *amendment, struct repo_file *file);
int visualize_file(struct visualize *visualize, word vrefnum, StringPtr filename);
int visualize_buildBuffers(struct visualize *visualize, word vrefnum, 
                           StringPtr readfilename, StringPtr filename, bool isCommit); 
void visualize_fixScrollbars(struct visualize *visualize);
void handleVertScrollbar(struct visualize *visualize, EventRecord *event);
void handleHorizontalScrollbar(EventRecord *event, CtlRecHndl ctl, Rect *rect, struct buffer *buffer, word maxLine);
void DrawBuffer(Rect *rectRect, struct buffer *buffer);
void visualize_writeBuffer(struct repo *repo, struct buffer *buffer, StringPtr tmpFilename);

static char visualizer_err[128];
extern word programID;

#pragma databank 1
static void DrawWindow(void) {
    DrawControls(GetPort());
}
#pragma databank 0

#pragma databank 1
static pascal void DrawSelectMember(RectPtr theRect, MemRecPtr MemberPtr, CtlRecHndl listHandle) {
    StringPtr file = (StringPtr)MemberPtr->memPtr;

    EraseRect(theRect);
    MoveTo(theRect->h1 + 5, theRect->v2 - 2);
    DrawString((char *) file);

    if (MemberPtr->memFlag & memSelected) {
        InvertRect(theRect);
    }
}
#pragma databank 0

void visualize_commit(struct committer *committer) {
    struct visualize visualize;
    WindowPtr win;
    bool done = false;
    EventRecord  currentEvent;
    word ctlId;
    MemRecHndl listHand;
    word x;

    memset(&visualize, 0, sizeof(struct visualize));
    visualize.diffLen = TEGetText(0x1D, (Ref) &(visualize.diffText), 0L, 
                                  refIsNewHandle, (Ref) NULL, 
                                  (Handle) committer->diff_te);

    if (committer->ndiffed_files == 1) {
        progress("Building display...");
        if (visualize_buildBuffers(&visualize, committer->browser->repo->bile->frefnum,
                                   &committer->diffed_files[0].file->filename,
                                   &committer->diffed_files[0].file->filename,
                                   true) == 0) {
            progress(NULL);
            visualize_file(&visualize, committer->browser->repo->bile->frefnum,
                           &committer->diffed_files[0].file->filename);
        }
    } else if (committer->ndiffed_files > 1) {
        currentEvent.wmTaskMask = 0x001FFFFFL;
        win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource, 
                         VISUALIZE_SELECT_WINDOW_ID, rWindParam1);
        //build list
        listHand = (MemRecHndl)xNewHandle(committer->ndiffed_files * sizeof(MemRec));
        for (x = 0; x < committer->ndiffed_files; x++) {
            (*listHand[x]).memFlag = 0;
            (*listHand)[x].memPtr = (Pointer) &committer->diffed_files[x].file->filename;
        }

        NewList2((Pointer)&DrawSelectMember, 1, (Ref)*listHand, refIsPointer, x, 
                 (Handle)GetCtlHandleFromID(win, VISUALIZE_SELECT_LIST_ID));
        SelectMember2(1, (Handle)GetCtlHandleFromID(win, VISUALIZE_SELECT_LIST_ID));
        ShowWindow(win);

        while (!done) {
            ctlId = DoModalWindow(&currentEvent, NULL, (VoidProcPtr)NULL, NULL, 0x4028);
            switch (ctlId) {
            case  VISUALIZE_SELECT_VISUALIZE_BUTTON_ID:
                x = NextMember2(0, (Handle) GetCtlHandleFromID(win, VISUALIZE_SELECT_LIST_ID));
                progress("Building display...");
                if (visualize_buildBuffers(&visualize, committer->browser->repo->bile->frefnum, 
                                           &committer->diffed_files[x - 1].file->filename, 
                                           &committer->diffed_files[x - 1].file->filename,
                                           true) == 0) {
                    progress(NULL);
                    visualize_file(&visualize, committer->browser->repo->bile->frefnum,
                                   &committer->diffed_files[x - 1].file->filename);
                }
                progress(NULL);
                break;
            case VISUALIZE_SELECT_DONE_BUTTON_ID:
                done = true;
                break;
            case VISUALIZE_SELECT_LIST_ID:
                break;
            }
        }
        CloseWindow(win);
        DisposeHandle((Handle) listHand);
    } else {
        warnx("No files to visualize");
    }
    DisposeHandle(visualize.diffText);
}

void visualize_amendment(struct browser *browser, struct repo_amendment *amendment, 
                         word diff_files, struct repo_file **repo_files) {
    struct visualize visualize;
    WindowPtr win;
    bool done = false;
    EventRecord  currentEvent;
    word ctlId;
    MemRecHndl listHand;
    word x;

    memset(&visualize, 0, sizeof(struct visualize));
    visualize.diffLen = TEGetText(0x1D, (Ref) &(visualize.diffText), 0L, 
                                  refIsNewHandle, (Ref) NULL, 
                                  (Handle) browser->diff_te);

    if (diff_files == 1) {
        visualize_rollback(&visualize, browser->repo, amendment, repo_files[0]);
    } else if (diff_files > 1) {
        currentEvent.wmTaskMask = 0x001FFFFFL;
        win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource, 
                         VISUALIZE_SELECT_WINDOW_ID, rWindParam1);
        //build list
        listHand = (MemRecHndl)xNewHandle(diff_files * sizeof(MemRec));
        for (x = 0; x < diff_files; x++) {
            (*listHand)[0].memPtr = (Pointer) &repo_files[x]->filename;
        }

        NewList2((Pointer)&DrawSelectMember, 0, (Ref)*listHand, refIsPointer, x, 
                 (Handle)GetCtlHandleFromID(win, VISUALIZE_SELECT_LIST_ID));
        ShowWindow(win);

        while (!done) {
            ctlId = DoModalWindow(&currentEvent, NULL, (VoidProcPtr)NULL, NULL, 0x4028);
            switch (ctlId) {
            case  VISUALIZE_SELECT_VISUALIZE_BUTTON_ID:
                x = NextMember2(0, (Handle) GetCtlHandleFromID(win, VISUALIZE_SELECT_LIST_ID));
                visualize_rollback(&visualize, browser->repo, amendment, repo_files[x - 1]);
                break;
            case VISUALIZE_SELECT_DONE_BUTTON_ID:
                done = true;
                break;
            case VISUALIZE_SELECT_LIST_ID:
                break;
            }
        }
        CloseWindow(win);
        DisposeHandle((Handle) listHand);        
    } else {
        warnx("No files to visualize");
    }
    DisposeHandle(visualize.diffText);
}

int visualize_rollback(struct visualize *visualize, struct repo *repo,
                        struct repo_amendment *amendment, struct repo_file *file) {
    struct bile_object *textob, *diffob;
    size_t dSize, size;
    char *dtext = NULL, *text;
    Str255 tmpFilename;
    int tmpFd, i, j;
    struct repo_amendment *a;
    struct bile_object *aob;
    size_t aSize;
    char *atext = NULL;

    textob = bile_find(repo->bile, REPO_TEXT_RTYPE, file->id);
    if (textob == NULL) {
        warn("No copy of file %s exists in repo", file->filename);
        return -1;
    }

    text = xmalloc(textob->size, "visualize_rollback");
    size = bile_read_object(repo->bile, textob, text, textob->size);
    if (size != textob->size) {
        panic("Failed to read text object %ld: %d", textob->id,
              bile_error(repo->bile));
    }

    diffob = bile_find(repo->bile, REPO_DIFF_RTYPE, amendment->id);
    dtext = xmalloc(diffob->size, "repo_show_diff_text");
    dSize = bile_read_object(repo->bile, diffob, dtext, diffob->size);
    if (dSize != diffob->size) {
        panic("Failed to read text object %ld: %d", diffob->id,
              bile_error(repo->bile));
    }

    tmpFd = patch_open_temp_dest_file(repo, &tmpFilename);
    FSetEOF(tmpFd, 0);
    //FWrite(tmpFd, text, &size);
    FClose(tmpFd);
    xfree(&text);

    progress("Building display...");
    //walk the amendments backwards to undo amends 1 and a time until
    //we get to this amendment.
    for (i = repo->namendments - 1; i >= 0; i--) {
        a = repo->amendments[i];
        if (a->id == amendment->id) {
            break;
        }
        for (j = 0; j < a->nfiles; j++) {
            if (a->file_ids[j] == file->id) {
                aob = bile_find(repo->bile, REPO_DIFF_RTYPE, a->id);
                atext = xmalloc(aob->size, "repo_show_diff_text");
                aSize = bile_read_object(repo->bile, aob, atext, aob->size);
                if (aSize != aob->size) {
                    panic("Failed to read text object %ld: %d", aob->id,
                          bile_error(repo->bile));
                }
                visualize->diffText = &atext;
                visualize->diffLen = aSize;
                visualize_buildBuffers(visualize, repo->bile->frefnum, &tmpFilename, &file->filename, false);
                xfree(&atext);
                FDelete(&tmpFilename);
                visualize_writeBuffer(repo, &visualize->rightBuffer, &tmpFilename);
                DisposeHandle(visualize->leftBuffer.buffer);
                DisposeHandle(visualize->rightBuffer.buffer);
                break;
            }
        }
    }
    visualize->diffText = &dtext;
    visualize->diffLen = dSize;
    visualize_buildBuffers(visualize, repo->bile->frefnum, &tmpFilename, &file->filename, false);
    progress(NULL);

    visualize_file(visualize, repo->bile->frefnum, &tmpFilename);

    xfree(&dtext);
    xfree(&text);
    DisposeHandle(visualize->leftBuffer.buffer);
    DisposeHandle(visualize->rightBuffer.buffer);
    FDelete(&tmpFilename);
    return 1;
}

int visualize_file(struct visualize *visualize, word vrefnum, StringPtr filename) {
    Str255 title;
    EventRecord currentEvent;
    bool done = false;
    word taskCode;
    long id = 0;
    GrafPortPtr port = GetPort();

    visualize->win = NewWindow2(NULL, NULL, &DrawWindow, NULL, refIsResource,
                     VISUALIZE_WINDOW_ID, rWindParam1);

    SetPort(visualize->win);
    snprintf(title.text, sizeof(title.text), "%s: Visualize Diff : %s",
             PROGRAM_NAME, p2cstr((char *)filename));
    title.textLength = strlen(title.text);
    SetWTitle((char *)&title, visualize->win);

    visualize->leftScroll = GetCtlHandleFromID(visualize->win, VISUALIZE_LEFT_SCROLL_ID);
    visualize->rightScroll = GetCtlHandleFromID(visualize->win, VISUALIZE_RIGHT_SCROLL_ID);
    visualize->vertScroll = GetCtlHandleFromID(visualize->win, VISUALIZE_VERT_SCROLL_ID);
    visualize->doneButton = GetCtlHandleFromID(visualize->win, VISUALIZE_DONE_BUTTON_ID);
    visualize->leftRect = GetCtlHandleFromID(visualize->win, VISUALIZE_LEFT_RECT_ID);
    visualize->rightRect = GetCtlHandleFromID(visualize->win, VISUALIZE_RIGHT_RECT_ID);
    HideControl(visualize->doneButton);
    HideControl(visualize->leftRect);
    HideControl(visualize->rightRect);

    visualize_fixScrollbars(visualize);
    ShowWindow(visualize->win);
    DrawBuffer(&(*visualize->leftRect)->ctlRect, &visualize->leftBuffer);
    DrawBuffer(&(*visualize->rightRect)->ctlRect, &visualize->rightBuffer);


    currentEvent.wmTaskMask = 0x1BE9E6L;
    while (!done) {
        taskCode = TaskMaster(everyEvent, &currentEvent);

        switch (taskCode) {
        case wInControl:
            id = currentEvent.wmTaskData4;
            switch (id) {
            case VISUALIZE_DONE_BUTTON_ID:
                done = true;
                break;
            case VISUALIZE_VERT_SCROLL_ID:
                handleVertScrollbar(visualize, &currentEvent);
                break;
            case VISUALIZE_LEFT_SCROLL_ID:
                handleHorizontalScrollbar(&currentEvent, visualize->leftScroll,
                                          &(*visualize->leftRect)->ctlRect,
                                          &visualize->leftBuffer, visualize->leftBuffer.maxLineLength);
                break;
            case VISUALIZE_RIGHT_SCROLL_ID:
                handleHorizontalScrollbar(&currentEvent, visualize->rightScroll,
                                          &(*visualize->rightRect)->ctlRect,
                                          &visualize->rightBuffer, visualize->rightBuffer.maxLineLength);
                break;
            default:
                break;
            }
            break;
        case wInGoAway:
            done = true;
            break;

        case keyDownEvt:
        case autoKeyEvt :
        {
                word commandKeyDown = currentEvent.modifiers & appleKey;
                if(commandKeyDown && 
                   ((currentEvent.message == 'w') || (currentEvent.message == 'W'))) {
                    done = true;
                }
                break;
        }
        default:
            break;

        }
    }

    CloseWindow(visualize->win);
    SetPort(port);
    DisposeHandle(visualize->leftBuffer.buffer);
    DisposeHandle(visualize->rightBuffer.buffer);
}

long parseDiff(char *diff, longword diffLen, longword *pos, char *buf, long bufLen) { 
    long returnCount = 0;

    while (((*pos + returnCount) < diffLen) && 
           (diff[*pos+returnCount] != '\r')) {
        if (returnCount > bufLen) {
            return -1;
        }
        buf[returnCount] = *(diff + *pos + returnCount);
        returnCount++;
    }
    if (returnCount <= bufLen ) {
        buf[returnCount] = 0;
    }
    *pos += returnCount;
    return returnCount;
}

void visualize_fixScrollbars(struct visualize *visualize) {
    long newDataSize;
    word newViewSize;

    newDataSize = visualize->lines;
    newViewSize = 19;
    SetCtlParams(newDataSize, newViewSize, visualize->vertScroll);

    // 4 is the length of the line number
    newDataSize = (visualize->leftBuffer.maxLineLength + 4 > 37) ?
        visualize->leftBuffer.maxLineLength + 4 : 37;
    newViewSize = 37;
    SetCtlParams(newDataSize, newViewSize, visualize->leftScroll);

    newDataSize = (visualize->rightBuffer.maxLineLength + 4 > 37) ?
        visualize->rightBuffer.maxLineLength + 4 : 37;
    newViewSize = 37;
    SetCtlParams(newDataSize, newViewSize, visualize->rightScroll);
}

word fullLinesRemain(struct buffer *buffer) {
    char *buf = *(buffer->buffer);
    long pos = buffer->position;
    word count = 0;
    word col = 0;

    pos += 2;
    while (pos < buffer->bufSize) {
        if ((buf[pos] == 0xff) && (buf[pos+1] == 0xff)) {
            count++;
            col = 0;
            if (count > 38) {
                break;
            }
            pos += 2;
        } else {
            pos++;
            col++;
        }
    }
    if (col) {
        count++;
    }
    return count > 38 ? 19 : count - 19;
}

bool scrollBuffer(struct buffer *buffer, int pos) {
    int direction = 1;
    char *curr;

    if (pos < 0) {
        direction = -1;
    }
    if (direction == -1 && buffer->position == 0) {
        return false;
    }

    if (direction == 1) {
        buffer->position += (2 * direction); //skip over the line marker
    } else {
        buffer->position--; //skip over the line marker
    }
    curr = *(buffer->buffer) + buffer->position;
    while (pos) {
        if ((curr[0] == 0xff ) && (curr[direction] == 0xff)) {
            if (direction < 0) {
                curr--;
                buffer->position--;
            }
            pos += -direction;
            if (pos) {
                curr += direction;
                buffer->position += direction;
            }
        } else {
            curr += direction;
            if ((buffer->position + direction > buffer->bufSize) ||
                (buffer->position + direction < 0)) {
                break;
            }
            buffer->position += direction;
        }
    }
    return true;
}

void gotoLine(struct buffer *buffer, word line) {
    size_t pos = 0;
    char *buf = *(buffer->buffer);

    while ((pos < buffer->bufSize) && line) {
        if ((buf[pos] == 0xff) && (buf[pos+1] == 0xff)) {
            line--;
            if (!line) {
                break;
            }
        }
        pos++;
    }
    buffer->position = pos;
}

void handleVertScrollbar(struct visualize *visualize, EventRecord *event) {
    word part;
    int offset;
    int lineDiff = 0;
    bool updateScreen = false;
    CtlRecHndl control;

    part = FindControl(&control, event->where.h, event->where.v, FrontWindow());
    if (part == downArrow) {
        if ((lineDiff =fullLinesRemain(&visualize->rightBuffer)) > 1) {
            lineDiff = 1;
        }
    } else if (part == upArrow) {
        lineDiff = -1;
    } else if (part == pageUp) {
        lineDiff = -19;
    } else if (part == pageDown) {
        lineDiff = fullLinesRemain(&visualize->rightBuffer);
    } else if (part == thumb) {
        word line = GetCtlValue(visualize->vertScroll) + 1;
        gotoLine(&visualize->leftBuffer, line);
        gotoLine(&visualize->rightBuffer, line);
        updateScreen = true;
    }

    if (lineDiff) {
        if (scrollBuffer(&visualize->leftBuffer, lineDiff)) {
            scrollBuffer(&visualize->rightBuffer, lineDiff);
            visualize->leftBuffer.position =  visualize->leftBuffer.position < 0 ? 0 : visualize->leftBuffer.position;
            visualize->rightBuffer.position =  visualize->rightBuffer.position < 0 ? 0 : visualize->rightBuffer.position;
            updateScreen = true;
        }
    }
    if (updateScreen) {
            DrawBuffer(&(*visualize->leftRect)->ctlRect, &visualize->leftBuffer);
            DrawBuffer(&(*visualize->rightRect)->ctlRect, &visualize->rightBuffer);
    }
    offset = GetCtlValue(visualize->vertScroll);
    offset += lineDiff;
    if (offset < 0) {
        offset = 0;
    }
    SetCtlValue(offset, visualize->vertScroll);

}

void handleHorizontalScrollbar(EventRecord *event, CtlRecHndl ctl, Rect *rect, struct buffer *buffer, word maxLine) {
    word part;
    word curCol = buffer->column;
    CtlRecHndl control;

    part = FindControl(&control, event->where.h, event->where.v, FrontWindow());

    if (part == downArrow) {
        buffer->column++;
    } else if (part == upArrow) {
        buffer->column--;
    } else if (part == pageUp) {
        buffer->column += -38;
    } else if (part == pageDown) {
        buffer->column += 38;
    } else  if (part == thumb) {
        buffer->column = GetCtlValue(ctl);
    }
    if (((int)(buffer->column)) < 0) {
        buffer->column = 0;
    }
    if (buffer->column > ((maxLine + 5) - 38)) {
        buffer->column = (maxLine + 5) - 38;
    }
    if (curCol != buffer->column) {
        DrawBuffer(rect, buffer);
        SetCtlValue(buffer->column, ctl);
    }
}

void addLineToBuffer(struct buffer *buffer, word lineNum, char *line, word lineLen) {
    size_t bufSize = GetHandleSize(buffer->buffer);
    char *bufData;
    word *l;

    if ((buffer->bufSize + sizeof(word) + sizeof(word) + lineLen + 1) > bufSize) {
        SetHandleSize(bufSize + BUFFER_INCREMENT, buffer->buffer);
    }

    HLock(buffer->buffer);
    bufData = *(buffer->buffer) + buffer->bufSize;
    l = (word *)bufData;
    *l = 0xFFFF; //start of line marker
    bufData += sizeof(word);
    l = (word *)bufData;
    *l = lineNum;
    bufData += sizeof(word);
    buffer->bufSize += 4; 
    buffer->bufSize += snprintf(bufData, lineLen+1, "%s", line);
    HUnlock(buffer->buffer);
}

int visualize_buildBuffers(struct visualize *visualize, word vrefNum, 
                           StringPtr readFilename, StringPtr filename,
                           bool isCommit) {
    word fd;
    longword eof, diffPos = 0;
    char diffBuf[BUFSIZ], sourceBuf[BUFSIZ];
    char *line;
    word visualState, filelen;
    Str255 tofilename = { 0 };
    int ret = 0, i, k, linelen;
    int source_line, source_delta, dest_line, dest_delta = 0, count;
    long fromLine = 1;
    long sourceLen;
    word leftLineNum = 1, rightLineNum = 1, linenum = 0, totalLines = 0;

    visualState = VISUALIZE_STATE_HEADER_FROM;
    visualizer_err[0] = 0;

    memset(&visualize->leftBuffer, 0, sizeof(struct buffer));
    memset(&visualize->rightBuffer, 0, sizeof(struct buffer));
    visualize->leftBuffer.buffer = xNewHandle(BUFFER_INCREMENT);
    visualize->rightBuffer.buffer = xNewHandle(BUFFER_INCREMENT);

    FOpen(vrefNum,
          readFilename,
          readEnable, 
          &fd, &eof);

    for (i = 0; i < visualize->diffLen; i += linelen) {
        linelen = 0;
        if (visualState != VISUALIZE_STATE_SETUP_CHUNK) {
            linelen = parseDiff(*(visualize->diffText), visualize->diffLen, &diffPos, diffBuf, sizeof(diffBuf) - 1);
            diffPos++;
            i++; //account for the newline
            line = diffBuf;
            linenum++;

        }

        switch (visualState) {
        case VISUALIZE_STATE_HEADER_FROM:
            if (strncmp(diffBuf, "--- ", 4) == 0) {
                visualState = VISUALIZE_STATE_TO;
                break;
            }
            if ((strncmp(line, "@@ ", 3) != 0) || (tofilename.textLength == 0)) {
                break;
            }
            //if @@ and we were already processing a file then 
            //continue to the next chunk
        case VISUALIZE_STATE_CHUNK_HEADER:
        {
            if (strncmp(line, "@@ ", 3) != 0) {
                snprintf(visualizer_err, sizeof(visualizer_err),
                         "Expected '@@ ' on line %d", linenum);
                ret = -1;
                goto visualize_done;
            }
            if (sscanf(line, "@@ %d,%d %d,%d @@%n",
                       &source_line, &source_delta, &dest_line, &dest_delta,
                       &count) != 4 || count < 1) {
                snprintf(visualizer_err, sizeof(visualizer_err),
                         "Malformed '@@ ' on line %d", linenum);
                ret = -1;
                goto visualize_done;
            }
            dest_line = abs(dest_line);
            if (fromLine < dest_line) {
                visualState = VISUALIZE_STATE_SETUP_CHUNK;
            } else  {
                visualState = VISUALIZE_STATE_CONTEXT;
            }
            break;
        }
        case VISUALIZE_STATE_TO:
            if (strncmp(diffBuf, "+++ ", 4) != 0) {
                snprintf(visualizer_err, sizeof(visualizer_err),
                         "Expected '+++ ' on line %d", linenum);
                ret = -1;
                goto visualize_done;
            }
            line += 4;
            filelen = linelen - 4;

            tofilename.text[0] = '\0';
            for (k = 0; k < filelen; k++) {
                if (line[k] == '\0' || line[k] == '\t') {
                    memcpy(tofilename.text, line, k);
                    tofilename.text[k + 1] = '\0';
                    tofilename.textLength = k;
                    break;
                }
            }
            if (tofilename.text[0] == '\0') {
                snprintf(visualizer_err, sizeof(visualizer_err),
                         "Failed to parse filename after +++ on line %d",
                         linenum);
                ret = -1;
                goto visualize_done;
            }

            /* make sure we found a diff that applies to this file*/
            if ((tofilename.textLength != filename->textLength) ||
                strncmp(tofilename.text, filename->text,filename->textLength)) {
                visualState = VISUALIZE_STATE_HEADER_FROM;
                tofilename.textLength = 0;
            } else {
                visualState = VISUALIZE_STATE_CHUNK_HEADER;
            }
            break;
        case VISUALIZE_STATE_SETUP_CHUNK:
            if (fromLine >= dest_line) {
                visualState = VISUALIZE_STATE_CONTEXT;
            } else {
                fromLine++;
                sourceBuf[0] = ' ';
                sourceLen = FSReadLine(fd, sourceBuf + 1, sizeof(sourceBuf) - 2);
                if (sourceLen < 0) {
                    break;
                }
                sourceBuf[++sourceLen] = '\0';
                if (sourceLen < 1023) {
                    strcat(sourceBuf, "\r");
                    sourceLen++;
                }

                totalLines++;
                addLineToBuffer(&visualize->leftBuffer,leftLineNum, sourceBuf, sourceLen);
                addLineToBuffer(&visualize->rightBuffer, rightLineNum, sourceBuf, sourceLen);
                leftLineNum++;
                rightLineNum++;
                visualize->leftBuffer.maxLineLength = MAX(visualize->leftBuffer.maxLineLength, sourceLen);
                visualize->rightBuffer.maxLineLength = MAX(visualize->rightBuffer.maxLineLength, sourceLen);
            }
            break;
        case VISUALIZE_STATE_CONTEXT:
            totalLines++;
            if (diffBuf[0] == '+') {
                strcat(diffBuf, "\r");
                addLineToBuffer(&visualize->leftBuffer, leftLineNum, "X\r", 2);
                addLineToBuffer(&visualize->rightBuffer, rightLineNum, diffBuf, linelen + 1);
                if (isCommit) {
                    FSReadLine(fd, sourceBuf, sizeof(sourceBuf) - 2);
                }
                dest_delta--;
                fromLine++;
                rightLineNum++;
                visualize->rightBuffer.maxLineLength = MAX(visualize->rightBuffer.maxLineLength, linelen);
            } else if (diffBuf[0] == '-') {
                strcat(diffBuf, "\r");
                addLineToBuffer(&visualize->leftBuffer, leftLineNum, diffBuf, linelen + 1);
                addLineToBuffer(&visualize->rightBuffer, rightLineNum, "X\r", 2);
                if (!isCommit) {
                    FSReadLine(fd, sourceBuf, sizeof(sourceBuf) - 2);
                }
                leftLineNum++;
                visualize->leftBuffer.maxLineLength = MAX(visualize->leftBuffer.maxLineLength, linelen);
            } else if (diffBuf[0] == ' ') {

                fromLine++;
                sourceBuf[0] = ' ';
                sourceLen = FSReadLine(fd, sourceBuf+1, sizeof(sourceBuf) - 2);
                sourceBuf[++sourceLen] = '\0';
                if ((sourceLen != linelen) || strncmp(sourceBuf+1, diffBuf+1, linelen)) {
                    snprintf(visualizer_err, sizeof(visualizer_err),
                             "Malformed 'Chunk Data' at line %d", linenum);
                    ret = -1;
                    goto visualize_done;
                }
                if (sourceLen < 1023) {
                    strcat(sourceBuf, "\r");
                    sourceLen++;
                }
                addLineToBuffer(&visualize->leftBuffer, leftLineNum, sourceBuf, sourceLen);
                addLineToBuffer(&visualize->rightBuffer, rightLineNum, sourceBuf, sourceLen);
                leftLineNum++;
                rightLineNum++;
                visualize->leftBuffer.maxLineLength = MAX(visualize->leftBuffer.maxLineLength, sourceLen);
                visualize->rightBuffer.maxLineLength = MAX(visualize->rightBuffer.maxLineLength, sourceLen);
                dest_delta--;
            }
            if (dest_delta <= 0) {
                visualState = VISUALIZE_STATE_HEADER_FROM;
            }
            break;
        default:
            err(1, "Invalid visualizer state %d", visualState);
        }
    }
    do {
        sourceLen = FSReadLine(fd, sourceBuf+1, sizeof(sourceBuf) - 2);
        if (sourceLen >= 0) {
            sourceBuf[0] = ' ';
            sourceBuf[++sourceLen] = '\0';
            if (sourceLen < 1022) {
                strcat(sourceBuf, "\r");
                sourceLen++;
            }
            totalLines++;
            addLineToBuffer(&visualize->leftBuffer, leftLineNum, sourceBuf, sourceLen);
            addLineToBuffer(&visualize->rightBuffer, rightLineNum, sourceBuf, sourceLen);
            leftLineNum++;
            rightLineNum++;
            visualize->leftBuffer.maxLineLength = MAX(visualize->leftBuffer.maxLineLength, sourceLen);
            visualize->rightBuffer.maxLineLength = MAX(visualize->rightBuffer.maxLineLength, sourceLen);
        }
    } while (sourceLen >= 0);
visualize_done:
    FClose(fd);

    if (strlen(visualizer_err)) {
        warn(visualizer_err);
    }
    visualize->lines = totalLines;

    return ret;
}

void DrawOneLineFast(Rect *lineRect, word charsPerLine, char *data, word start) {
    extern word Characters[256][8];
    extern word redSpace[8], greenSpace[8];
    word x, y;
    char lineStr[7];
    byte type, theChar;
    char *line = data;
    word lineData[8][37];
    word linePos = 0;
    bool done = false;
    LocInfo loc = { 0x80, (Pointer)&lineData, charsPerLine * 2, { 0, 0, 8, 296 }};
    word lineNum;

    line += 2; //skip over start of line marker
    lineNum = *((word *)line);

    line += 2; //skip over line number
    type = *line;
    line++;

    if (type != 'X') {
        snprintf(lineStr, sizeof(lineStr), "%04d ", lineNum);
        for (x = 0; x < strlen(lineStr); x++) {
            if (x >= start) {
                if (lineStr[x] != ' ' || type == ' ') {
                    lineData[0][linePos] = Characters[lineStr[x]][0];
                    lineData[1][linePos] = Characters[lineStr[x]][1];
                    lineData[2][linePos] = Characters[lineStr[x]][2];
                    lineData[3][linePos] = Characters[lineStr[x]][3];
                    lineData[4][linePos] = Characters[lineStr[x]][4];
                    lineData[5][linePos] = Characters[lineStr[x]][5];
                    lineData[6][linePos] = Characters[lineStr[x]][6];
                    lineData[7][linePos] = Characters[lineStr[x]][7];
                } else {
                    if (type =='-') {
                        lineData[0][linePos] = redSpace[0];
                        lineData[1][linePos] = redSpace[1];
                        lineData[2][linePos] = redSpace[2];
                        lineData[3][linePos] = redSpace[3];
                        lineData[4][linePos] = redSpace[4];
                        lineData[5][linePos] = redSpace[5];
                        lineData[6][linePos] = redSpace[6];
                        lineData[7][linePos] = redSpace[7];
                    } else if (type == '+') {
                        lineData[0][linePos] = greenSpace[0];
                        lineData[1][linePos] = greenSpace[1];
                        lineData[2][linePos] = greenSpace[2];
                        lineData[3][linePos] = greenSpace[3];
                        lineData[4][linePos] = greenSpace[4];
                        lineData[5][linePos] = greenSpace[5];
                        lineData[6][linePos] = greenSpace[6];
                        lineData[7][linePos] = greenSpace[7];
                    } else {
                        lineData[0][linePos] = Characters[32][0];
                        lineData[1][linePos] = Characters[32][1];
                        lineData[2][linePos] = Characters[32][2];
                        lineData[3][linePos] = Characters[32][3];
                        lineData[4][linePos] = Characters[32][4];
                        lineData[5][linePos] = Characters[32][5];
                        lineData[6][linePos] = Characters[32][6];
                        lineData[7][linePos] = Characters[32][7];
                    }
                }
                linePos++;
            }
        }

        if (start < 5) {
            start = 0;
        } else if (start >=0) {
            start -= 5;
        }
        for (x = 0; linePos < charsPerLine; x++) {
            theChar = line[x];
            if (theChar == 13 || done) {
                done = true;
                theChar = 32;
            }
            if (theChar == 9) {
                for (y = 0; y < 4; y++) {
                    if ((x >= start) && (linePos < charsPerLine)) {
                        lineData[0][linePos] = Characters[32][0];
                        lineData[1][linePos] = Characters[32][1];
                        lineData[2][linePos] = Characters[32][2];
                        lineData[3][linePos] = Characters[32][3];
                        lineData[4][linePos] = Characters[32][4];
                        lineData[5][linePos] = Characters[32][5];
                        lineData[6][linePos] = Characters[32][6];
                        lineData[7][linePos] = Characters[32][7];
                        linePos++;
                    }
                }
            } else {
                if (x >= start) {
                    lineData[0][linePos] = Characters[theChar][0];
                    lineData[1][linePos] = Characters[theChar][1];
                    lineData[2][linePos] = Characters[theChar][2];
                    lineData[3][linePos] = Characters[theChar][3];
                    lineData[4][linePos] = Characters[theChar][4];
                    lineData[5][linePos] = Characters[theChar][5];
                    lineData[6][linePos] = Characters[theChar][6];
                    lineData[7][linePos] = Characters[theChar][7];
                    linePos++;
                }
            }
        }
    } else {
        for (x = 0; x < charsPerLine; x++) {
            lineData[0][linePos] = Characters[32][0];
            lineData[1][linePos] = Characters[32][1];
            lineData[2][linePos] = Characters[32][2];
            lineData[3][linePos] = Characters[32][3];
            lineData[4][linePos] = Characters[32][4];
            lineData[5][linePos] = Characters[32][5];
            lineData[6][linePos] = Characters[32][6];
            lineData[7][linePos] = Characters[32][7];
            linePos++;
        }
    }
    PPToPort(&loc, &loc.boundsRect, lineRect->h1, lineRect->v1, modeCopy);
}

void DrawBuffer(Rect *rectRect, struct buffer *buffer) {
    word x;
    Rect bufRect;
    char *line;
    char *end;
    word charsPerLine = ((rectRect->h2 - rectRect->h1) / 8) - 1;

    memcpy(&bufRect, rectRect, sizeof(Rect));
    bufRect.v1 += 2;
    bufRect.h1 += 3;
    HLock(buffer->buffer);
    line = (*buffer->buffer) + buffer->position;
    end = (*buffer->buffer) + buffer->bufSize;

    for (x = 0; x < 19;x ++) {
        DrawOneLineFast(&bufRect, charsPerLine, line, buffer->column);
        line += 2; //skip over the line marker
        line += 2; //skip over the line number
        //Search of EOL marker
        while ((*line != 13) && (line < end)) {
            line++;
        }
        line++; //skip over EOL marker (CR);
        bufRect.v1 += 8;
    }
    HUnlock(buffer->buffer);
}


void visualize_writeBuffer(struct repo *repo, struct buffer *buffer, StringPtr tmpFilename) {
    char *cur, *end, *line;
    int fd;
    size_t size;

    cur = *(buffer->buffer);
    end = cur + buffer->bufSize;
    fd = patch_open_temp_dest_file(repo, tmpFilename);

    while (cur < end) {
        cur += 5; //skip marker and number, type add/remove/common
        line = cur;
        while ((*cur != 13) && (cur < end)) {
            cur++;
        }
        cur++; //include the CR
        size = cur - line;
        FWrite(fd, line, &size);
    }
    FClose(fd);
}

