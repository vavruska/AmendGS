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
#include <stdlib.h>
#include <gsos.h>
#include <resources.h>
#include <memory.h>
#include <misctool.h>
#include <appleshare.h>
#include <orca.h>

#include "AmendGS.h"
#include "repo.h"
#include "bile.h"
#include "patch.h"
#include "util.h"

static word patch_state;
static char patch_err[128];
static Str255 prevFile;

enum {
    PATCH_STATE_HEADER_FROM,
    PATCH_STATE_TO,
    PATCH_STATE_CHUNK_HEADER,
    PATCH_STATE_SETUP_CHUNK,
    PATCH_STATE_CONTEXT
};

int patch_open_source_file(struct repo *repo, StringPtr filename);
int patch_open_temp_dest_file(struct repo *repo, StringPtr tmpFile);

int patch_open_source_file(struct repo *repo, StringPtr filename) {
    int error, i;
    word ret = 0;

    for (i = 0; i < repo->nfiles; i++) {
        if ((repo->files[i]->filename.textLength == filename->textLength) &&
            (strncmp(repo->files[i]->filename.text, filename->text, filename->textLength) != 0)) {
            continue;
        }

        error = FOpen(repo->bile->frefnum, filename, readEnable, &ret, NULL);
        if (error && error == fileNotFound) {

            error = FCreate(repo->bile->frefnum, filename, repo->files[i]->type, repo->files[i]->auxType, 0);
            /* this should never happen?? */
            if (error && error != dupPathname) {
                err(1, "Failed to create %s: %d", filename->text, error);
            }
            error = FOpen(repo->bile->frefnum, filename, writeEnable, &ret, NULL);
        }

        if (error) {
            err(1, "Failed to open %s: %d", filename->text, error);
        }

        return ret;
    }

    return 0;
}

int patch_open_temp_dest_file(struct repo *repo, StringPtr tmpFile) {
    word error, ret = 0;
    FileInfoRecGS fileRec;
    ResultBuf255 judgeName = { 255, { 0, { 0 } } };
    JudgeNameRecGS judgeRec = { 6, 0, 3, NULL, 255, &judgeName, 0 };
    word tmpCount = 0;

    memset(tmpFile, 0, sizeof(Str255));
    judgeRec.fileSysID = FGetFSTId(repo->bile->frefnum);

    do {
        do {
            //check to see if the file exists as we dont want to overwrite an existing file
            sprintf(tmpFile->text, "tmp.%u", tmpCount++);
            tmpFile->textLength = strlen(tmpFile->text);
            getpath(repo->bile->frefnum, tmpFile, &judgeName.bufString, true);
        } while (FStat(&judgeName.bufString, &fileRec) != fileNotFound);
        //not sure we need this...in fact I know we dont as what do I expect it to do?
        //I was going to go down a different path and just left it in.
        JudgeNameGS(&judgeRec);
    } while (judgeRec.nameFlags & 0x4000);

    error = FCreate(repo->bile->frefnum, tmpFile, 0x04, 0, 0);
    if (error && error != dupPathname) {
        err(1, "Failed to create %s: %d", tmpFile->text, error);
    }

    error = FOpen(repo->bile->frefnum, tmpFile, writeEnable, &ret, NULL);
    if (error) {
        err(1, "Failed to open %s: %d", tmpFile->text, error);
    }

    return ret;
}

void doRename(struct repo *repo, StringPtr srcfile, StringPtr tmpFile, GSString255Ptr backupPath) {
    GSString255 source, dest;
    FileInfoRecGS fileRec;

    memset(&source, 0, sizeof(GSString255));
    memset(&dest, 0, sizeof(GSString255));
    getpath(repo->bile->frefnum, srcfile, &source, true);
    memcpy(&dest, backupPath, sizeof(GSString255));
    strncat(dest.text, srcfile->text, srcfile->textLength);
    dest.length = strlen(dest.text);
    //get the stats
    FStat(&source, &fileRec);
    //move the original file to backup dir
    FRename(&source, &dest);
    memcpy(&dest, &source, sizeof(GSString255));
    getpath(repo->bile->frefnum, tmpFile, &source, true);
    //rename to tmp file as the original file
    FRename(&source, &dest);

    //set the stats upto the creation time
    fileRec.pCount = 6;
    SetFileInfoGS(&fileRec);
}

word patch_process(struct repo *repo, StringPtr filename) {
    GSString255 backupPath = { 0 };
    Str255 tofilename = { 0 }, tmpFilename;
    char buf[BUFSIZ], sourceBuf[BUFSIZ];
    size_t i;
    longword patch_size;
    word linenum = 0, error, ret = 0;
    long linelen, sourceLen = 0;
    word patch_frefnum = 0, source_frefnum = 0, dest_frefnum = 0;
    char *line;
    int source_line = 0, source_delta, dest_line, dest_delta = 0, count;
    long fromLine = 0;
    TimeRec now;
    long secs;
    CreateRecGS createRec = { 5, &backupPath, 0x00E3, 0x000F, 0, 0x000D };
    bool partial = false;

    now = ReadTimeHex();
    getpath(repo->bile->frefnum, NULL, &backupPath, false);
    secs = ConvSeconds(TimeRec2Secs, 0, (Pointer)&now);
    backupPath.length += sprintf(backupPath.text + backupPath.length, "bck%lX:", secs);

    //create the backup dir
    CreateGS(&createRec);

    patch_err[0] = 0;

    error = FOpen(0, filename, readEnable, &patch_frefnum, &patch_size);
    if (error) {
        err(1, "Failed to open patch %s: %d", p2cstr((char *)filename), error);
    }

    patch_state = PATCH_STATE_HEADER_FROM;

    for (i = 0; i < patch_size; i += linelen) {
        linelen = 0;
        if (patch_state != PATCH_STATE_SETUP_CHUNK) {
            linelen = FSReadLine(patch_frefnum, buf, sizeof(buf) - 1);
            if (linelen < 0) {
                break;
            }
            buf[linelen] = '\0';
            linenum++;
            line = buf;
        }

        switch (patch_state) {
        case PATCH_STATE_HEADER_FROM:
            if (strncmp(line, "--- ", 4) != 0) {
                break;
            }
            patch_state = PATCH_STATE_TO;
            if ((strncmp(line, "@@ ", 3) != 0) || (tofilename.textLength == 0)) {
                break;
            }
            //if @@ and we were already processing a file then
            //continue to the next chunk
        case PATCH_STATE_CHUNK_HEADER:
        {

            if (strncmp(line, "@@ ", 3) != 0) {
                snprintf(patch_err, sizeof(patch_err),
                         "Expected '@@ ' on line %d", linenum);
                ret = -1;
                goto patch_done;
            }
            if (sscanf(line, "@@ %d,%d %d,%d @@%n",
                       &source_line, &source_delta, &dest_line, &dest_delta,
                       &count) != 4 || count < 1) {
                snprintf(patch_err, sizeof(patch_err),
                         "Malformed '@@ ' on line %d", linenum);
                ret = -1;
                goto patch_done;
            }
            source_line = abs(source_line);
            if (fromLine < source_line) {
                patch_state = PATCH_STATE_SETUP_CHUNK;
            } else  {
                patch_state = PATCH_STATE_CONTEXT;
            }
            break;
        }
        case PATCH_STATE_TO:
            if (strncmp(line, "+++ ", 4) != 0) {
                snprintf(patch_err, sizeof(patch_err),
                         "Expected '+++ ' on line %d", linenum);
                ret = -1;
                goto patch_done;
            }
            line += 4;
            linelen -= 4;

            tofilename.text[0] = '\0';
            for (i = 0; i < linelen; i++) {
                if (line[i] == '\0' || line[i] == '\t') {
                    memcpy(tofilename.text, line, i);
                    tofilename.text[i] = '\0';
                    tofilename.textLength = i;
                    break;
                }
            }
            if (tofilename.text[0] == '\0') {
                snprintf(patch_err, sizeof(patch_err),
                         "Failed to parse filename after +++ on line %d",
                         linenum);
                ret = -1;
                goto patch_done;
            }
            //check to see if the previous file is equal to this file.
            //if not finish out the old file, rename and start a new file
            if (source_frefnum) {
                if ((prevFile.textLength != tofilename.textLength) ||
                    (strncmp(prevFile.text, tofilename.text, prevFile.textLength))) {
                    do {
                        sourceLen = FSReadLine(source_frefnum, sourceBuf, sizeof(sourceBuf) - 2);
                        if (sourceLen >= 0) {
                            sourceBuf[sourceLen] = '\0';
                            if (sourceLen < 1023) {
                                strcat(sourceBuf, "\r");
                                sourceLen++;
                            }
                            FWrite(dest_frefnum, sourceBuf, (longword *)&sourceLen);
                        }
                    } while (sourceLen > 0);

                    FClose(source_frefnum);
                    FClose(dest_frefnum);
                    doRename(repo, &prevFile, &tmpFilename, &backupPath);
                    partial = true;
                    fromLine = 0;
                }
            }
            source_frefnum = patch_open_source_file(repo, &tofilename);
            if (!source_frefnum) {
                ret = -1;
                goto patch_done;
            }
            memcpy(&prevFile, &tofilename, sizeof(Str255));

            progress("Patching %s", tofilename.text);

            dest_frefnum = patch_open_temp_dest_file(repo, &tmpFilename);
            if (!dest_frefnum) {
                ret = -1;
                goto patch_done;
            }

            patch_state = PATCH_STATE_CHUNK_HEADER;
            break;
        case PATCH_STATE_SETUP_CHUNK:
            fromLine++;
            sourceLen = FSReadLine(source_frefnum, sourceBuf, sizeof(sourceBuf) - 2);
            if (sourceLen < 0) {
                break;
            }
            sourceBuf[sourceLen] = '\0';
            if (sourceLen < 1023) {
                strcat(sourceBuf, "\r");
                sourceLen++;
            }
            FWrite(dest_frefnum, sourceBuf, (longword *)&sourceLen);
            if (fromLine + 1 >= source_line) {
                patch_state = PATCH_STATE_CONTEXT;
            }
            break;
        case PATCH_STATE_CONTEXT:
            if (line[0] == '+') {
                longword len = linelen;
                strcat(line, "\r");
                FWrite(dest_frefnum, line + 1, &len);
                dest_delta--;
            }
            if (line[0] == '-' || line[0] == ' ') {
                fromLine++;
                sourceLen = FSReadLine(source_frefnum, sourceBuf, sizeof(sourceBuf) - 2);
            }
            if (line[0] == ' ') {

                sourceBuf[sourceLen] = '\0';
                if (((sourceLen + 1) != linelen) || strncmp(sourceBuf, line + 1, sourceLen)) {
                    snprintf(patch_err, sizeof(patch_err),
                             "Malformed 'Chunk Data' at line %d", linenum);
                    ret = -1;
                    goto patch_done;
                }
                if (sourceLen < 1023) {
                    strcat(sourceBuf, "\r");
                    sourceLen++;
                }
                FWrite(dest_frefnum, sourceBuf, (longword *)&sourceLen);
                dest_delta--;
            }
            if (dest_delta <= 0) {
                patch_state = PATCH_STATE_HEADER_FROM;  //PATCH_STATE_CHUNK_HEADER;
            }
            break;
        default:
            err(1, "Invalid patch state %d", patch_state);
        }
    }
    if (!strlen(patch_err)) {
        do {
            sourceLen = FSReadLine(source_frefnum, sourceBuf, sizeof(sourceBuf) - 2);
            if (sourceLen >= 0) {
                sourceBuf[sourceLen] = '\0';
                if (sourceLen < 1023) {
                    strcat(sourceBuf, "\r");
                    sourceLen++;
                }
                FWrite(dest_frefnum, sourceBuf, (longword *)&sourceLen);
            }
        } while (sourceLen > 0);
    }

patch_done:
    if (patch_frefnum) {
        FClose(patch_frefnum);
    }
    if (source_frefnum) {
        FClose(source_frefnum);
    }
    if (dest_frefnum) {
        FClose(dest_frefnum);
    }

    progress(NULL);
    //copy the orginal to the backup dir and rename the new file
    if (!strlen(patch_err)) {
        doRename(repo, &tofilename, &tmpFilename, &backupPath);
        note("Patching successful. Previous revisison have been transfers to %s", backupPath.text);
    } else {
        warn(patch_err);
        if (partial) {
            warn("Patching was partially successful. Check %s "
                 "to see previous revisions of successfully patched files", backupPath.text);
        }
    }

    return ret;
}
