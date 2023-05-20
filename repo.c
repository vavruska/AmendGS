#line 2 "/host/AmendGS/repo.c"

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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <gsos.h>
#include <stdfile.h>
#include <resources.h>
#include <appleshare.h>
#include <string.h>
#include <orca.h>
#include <qdaux.h>
#include <misctool.h>

#include "AmendGS.h"
#include "bile.h"
#include "diff.h"
#include "repo.h"
#include "strnatcmp.h"
#include "util.h"

segment "repo";

struct repo* repo_init(struct bile *bile, word is_new);
void repo_sort_files(struct repo *repo);
void repo_sort_amendments(struct repo *repo);
word repo_get_file_attrs(struct repo *repo, StringPtr filename,
                         struct repo_file_attrs *attrs);
word repo_file_update(struct repo *repo, struct repo_file *file);
word repo_diff_header(struct repo *repo,
                      struct repo_amendment *amendment, char **ret);
word repo_migrate(struct repo *repo, word is_new);
bool repo_add_file_filter(struct FileParam *pbp);

struct repo* repo_open(const StringPtr file) {
    SFReplyRec2 reply;
    SFTypeList2 types;
    char prompt[] = "\pOpen Repo:";
    ResultBuf255Hndl path;
    ResultBuf255Hndl name;
    Str255 bilePath = { 0, { 0 } };

    struct bile *bile;

    if (file) {
        bile = bile_open(file);
    } else {
        types.numEntries = 1;
        types.fileTypeEntries[0].flags = 0;
        types.fileTypeEntries[0].fileType = REPO_TYPE;
        types.fileTypeEntries[0].auxType = BILE_AUX_TYPE;

        memset(&reply, 0, sizeof(SFReplyRec2));
        reply.nameRefDesc = refIsNewHandle;
        reply.pathRefDesc = refIsNewHandle;

        SFGetFile2(0x15, 0x15, refIsPointer, (Ref)prompt, NULL, &types, &reply);
        if (!reply.good) {
            return NULL;
        }
        path = (ResultBuf255Hndl)reply.pathRef;
        name = (ResultBuf255Hndl)reply.nameRef;
        strncpy(bilePath.text, (*path)->bufString.text, (*path)->bufString.length);
        bilePath.textLength = (*path)->bufString.length;
        bile = bile_open(&bilePath);
    }

    if (bile == NULL) {
        if (bile_error(NULL) == BILE_ERR_NEED_UPGRADE_1) {
            warn("File %s is a version 1 repo and must be upgraded",
                 (*name)->bufString.text);
        } else {
            warn("Opening repo %s failed: %d", (*name)->bufString.text,
                 bile_error(NULL));
        }
        return NULL;
    }

    progress("Verifying repository structure...");
    bile_verify(bile);

    progress("Reading repository...");
    return repo_init(bile, 0);

    if (reply.good) {
        DisposeHandle((Handle)reply.pathRef);
        DisposeHandle((Handle)reply.nameRef);
    }
}

struct repo* repo_create(void) {
    SFReplyRec reply;
    struct bile *bile;
    word error;
    char prompt[] = "\pCreate new repository:";
    GSString255 fullPath = { 0 };

    SFPutFile(0x15, 0x15, prompt, NULL, 254, &reply);

    if (!reply.good) {
        return NULL;
    }

    strcpy(fullPath.text, reply.fullPathname + 1);
    fullPath.length = reply.fullPathname[0];
    bile = bile_create((StringPtr)&reply.fullPathname, AMEND_CREATOR, REPO_TYPE);
    if (bile == NULL && bile_error(NULL) == dupPathname) {
        error = FSDelete(&fullPath);

        bile = bile_create((StringPtr)&reply.fullPathname, AMEND_CREATOR, REPO_TYPE);
    }
    if (bile == NULL) {
        panic("Failed to create %s: %d", p2cstr(reply.filename), error);
    }

    return repo_init(bile, 1);
}

struct repo* repo_init(struct bile *bile, word is_new) {
    struct bile_object *bob;
    struct repo *repo;
    size_t size;
    char *data;
    unsigned long i;

    repo = xmalloczero(sizeof(struct repo), "repo");
    repo->bile = bile;
    repo->next_file_id = 1;
    repo->next_amendment_id = 1;

    if (repo_migrate(repo, is_new) != 0) {
        xfree(&repo);
        return NULL;
    }

    /* fill in file info */
    repo->nfiles = bile_count_by_type(bile, REPO_FILE_RTYPE);
    if (repo->nfiles) {
        repo->files = xcalloc(repo->nfiles, sizeof(Ptr), "repo files");
        for (i = 0; i < repo->nfiles; i++) {
            bob = bile_get_nth_of_type(bile, i, REPO_FILE_RTYPE);
            if (bob == NULL) panic("no %ld file, but count said it should be there", i);
            size = bile_read_alloc(bile, REPO_FILE_RTYPE, bob->id, &data);
            if (size == 0) panic("failed fetching file %ld", bob->id);
            repo->files[i] = xmalloczero(sizeof(struct repo_file),
                                         "repo file");
            repo->files[i] = repo_parse_file(bob->id, (unsigned char *)data,
                                             size);
            if (repo->files[i]->id >= repo->next_file_id) repo->next_file_id = repo->files[i]->id + 1;
            xfree(&data);
        }
    }
    repo_sort_files(repo);

    /* fill in amendment info */
    repo->namendments = bile_count_by_type(bile, REPO_AMENDMENT_RTYPE);
    if (repo->namendments) {
        repo->amendments = xcalloc(repo->namendments, sizeof(Ptr),
                                   "repo amendments");
        for (i = 0; i < repo->namendments; i++) {
            bob = bile_get_nth_of_type(bile, i, REPO_AMENDMENT_RTYPE);
            if (bob == NULL) panic("no %ld amendment, but count said it should be there",
                                   i);
            size = bile_read_alloc(bile, REPO_AMENDMENT_RTYPE, bob->id,
                                   &data);
            if (size == 0) panic("failed fetching amendment %ld", bob->id);
            repo->amendments[i] = repo_parse_amendment(bob->id,
                                                       (unsigned char *)data, size);
            if (repo->amendments[i]->id >= repo->next_amendment_id) repo->next_amendment_id = repo->amendments[i]->id + 1;
            xfree(&data);
        }
    }
    repo_sort_amendments(repo);

    return repo;
}

void repo_close(struct repo *repo) {
    struct repo_file *file;
    struct repo_amendment *amendment;
    word i;

    for (i = 0; i < repo->namendments; i++) {
        amendment = repo->amendments[i];
        if (amendment == NULL) {
            continue;
        }

        if (amendment->log != NULL) {
            DisposeHandle(amendment->log);
        }

        if (amendment->file_ids != NULL) {
            xfree(&amendment->file_ids);
        }

        xfree(&amendment);
    }
    xfree(&repo->amendments);

    for (i = 0; i < repo->nfiles; i++) {
        file = repo->files[i];
        if (file == NULL) {
            continue;
        }

        xfree(&file);
    }
    xfree(&repo->files);

    bile_close(repo->bile);
    xfree(&repo);
}

struct repo_file* repo_parse_file(unsigned long id, unsigned char *data,
                                  size_t size) {
    struct repo_file *file;
    word len, datapos;

    datapos = 0;

    file = xmalloczero(sizeof(struct repo_file), "repo_parse_file");
    file->id = id;

    /* filename, pstr */
    len = data[0];
    memcpy(file->filename.text, data + 1, len);
    file->filename.text[len] = '\0';
    file->filename.textLength = len;
    datapos += (len + 1);

    /* type fourcc, creator fourcc */
    memcpy(&file->type, data + datapos, 4);
    datapos += 4;
    memcpy(&file->auxType, data + datapos, 4);
    datapos += 4;

    /* creation date, long */
    file->ctime = ((unsigned long)data[datapos] << 24) |
        ((unsigned long)data[datapos + 1] << 16) |
        ((unsigned long)data[datapos + 2] << 8) |
        ((unsigned long)data[datapos + 3]);
    //memcpy(&file->ctime, data + datapos, 4);
    datapos += 4;

    /* modification date, long */
    file->mtime = ((unsigned long)data[datapos] << 24) |
        ((unsigned long)data[datapos + 1] << 16) |
        ((unsigned long)data[datapos + 2] << 8) |
        ((unsigned long)data[datapos + 3]);
    datapos += 4;

    /* flags, unsigned char */
    file->flags = data[datapos];
    datapos += 1;

    if (datapos != size) panic("repo_parse_file object size %lu, data position %d", size,
                               datapos);

    return file;
}

struct repo_amendment* repo_parse_amendment(unsigned long id,
                                            unsigned char *data, size_t size) {
    struct repo_amendment *amendment;
    word len, i;

    amendment = xmalloczero(sizeof(struct repo_amendment),
                            "repo_parse_amendment");
    amendment->id = id;

    /* date */
    amendment->date = ((unsigned long)data[0] << 24) |
        ((unsigned long)data[1] << 16) |
        ((unsigned long)data[2] << 8) |
        ((unsigned long)data[3]);
    data += 4;

    /* author, pstr */
    len = data[0];
    if (len > sizeof(amendment->author) - 1) len = sizeof(amendment->author) - 1;
    memcpy(amendment->author, data + 1, len);
    amendment->author[len] = '\0';
    data += (data[0] + 1);

    /* files, word */
    amendment->nfiles = (data[0] << 8) | data[1];
    data += 2;

    if (amendment->nfiles) {
        amendment->file_ids = xcalloc(amendment->nfiles, sizeof(word),
                                      "amendment file_ids");
        for (i = 0; i < amendment->nfiles; i++) {
            amendment->file_ids[i] = (data[0] << 8) | data[1];
            data += 2;
        }
    }

    /* additions, word */
    amendment->adds = (data[0] << 8) | data[1];
    data += 2;

    /* subs, word */
    amendment->subs = (data[0] << 8) | data[1];
    data += 2;

    /* log message, word-length */
    len = (data[0] << 8) | data[1];
    data += 2;
    amendment->log = xNewHandle(len + 1);
    amendment->log_len = len;
    HLock(amendment->log);
    memcpy(*(amendment->log), data, len);
    (*(amendment->log))[len] = '\0';
    HUnlock(amendment->log);
    data += len;

    /* TODO: use datapos and check against size like repo_parse_file */

    return amendment;
}

struct repo_file* repo_file_with_id(struct repo *repo, word id) {
    word i;

    for (i = 0; i < repo->nfiles; i++) {
        if (repo->files[i]->id == id) {
            return repo->files[i];
        }
    }

    return NULL;
}

word repo_diff_header(struct repo *repo, struct repo_amendment *amendment,
                      char **ret) {
    word header_len;
    word i;

    *ret = xmalloc(128 + amendment->log_len, "repo_diff_header");
    header_len = sprintf(*ret,
                         "Author:\t%s\r"
                         "Date:\t%s\r"
                         "\r  ",
                         amendment->author,
                         timeString(amendment->date));

    /* copy log, indenting each line */
    HLock(amendment->log);
    for (i = 0; i < amendment->log_len; i++) {
        *(*ret + header_len++) = (*(amendment->log))[i];

        if ((*(amendment->log))[i] == '\r' && i < amendment->log_len - 1) {
            *(*ret + header_len++) = ' ';
            *(*ret + header_len++) = ' ';
        }
    }
    HUnlock(amendment->log);
    *(*ret + header_len++) = '\r';
    *(*ret + header_len++) = '\r';

    return header_len;
}

void repo_show_diff_text(struct repo *repo, struct repo_amendment *amendment,
                         Handle te) {
    char truncbuf[64];
    struct bile_object *bob;
    size_t size;
    char *dtext;
    char *buf = NULL;
    unsigned long diff_len, all_len;
    word header_len, blen, trunc = 0;
    word warn_off;
    TERecordHndl teRec = (TERecordHndl)te;


    bob = bile_find(repo->bile, REPO_DIFF_RTYPE, amendment->id);
    if (bob == NULL) {
        warn("Failed finding DIFF %d, corrupted repo?", amendment->id);
        return;
    }

    diff_len = bob->size;
    if (diff_len == 0) {
        panic("diff zero bytes");
    }

    header_len = repo_diff_header(repo, amendment, &buf);

    all_len = header_len + diff_len;
    if (all_len >= MAX_TEXTEDIT_SIZE) {
        all_len = MAX_TEXTEDIT_SIZE;
        trunc = 1;
    }

    dtext = xmalloc(all_len, "repo_show_diff_text");
    memcpy(dtext, buf, header_len);
    xfree(&buf);

    size = bile_read_object(repo->bile, bob, dtext + header_len,
                            all_len - header_len);
    if (size == 0) {
        panic("failed reading diff %lu: %d", amendment->id,
              bile_error(repo->bile));
    }

    if (trunc) {
        warn_off = MAX_TEXTEDIT_SIZE - header_len -
            strlen(REPO_DIFF_TOO_BIG);
        blen = snprintf(truncbuf, sizeof(truncbuf), REPO_DIFF_TOO_BIG,
                        diff_len - warn_off);
        memcpy(dtext + MAX_TEXTEDIT_SIZE - blen, truncbuf, blen);
    }

    /* manually reset scroll without TESetSelect(0, 0, te) which redraws */
    (*teRec)->textFlags &= ~fReadOnly;
    TEInsert(0x0005, (Ref)dtext, all_len, 0, 0, te);
    (*teRec)->textFlags |= fReadOnly;
    xfree(&dtext);
}


struct repo_file* repo_add_file(struct repo *repo) {
    SFReplyRec2 reply;
    SFTypeList2 types;
    GSString255 pathname;
    PrefixRecGS prefixRec = { 2, 8 };
    char prompt[] = { "\pSelect File to Add" };
    ResultBuf255Hndl filePath, fileName;
    struct repo_file *file;
    word i;

    /* start SFGetFile2 from this dir */
    getpath(repo->bile->frefnum, NULL, &pathname, false);

    prefixRec.buffer.setPrefix = &pathname;
    SetPrefixGS(&prefixRec);

    types.numEntries = 0;
    types.fileTypeEntries[0].flags = 0;
    types.fileTypeEntries[0].fileType = 0x04;
    types.fileTypeEntries[0].auxType = 0;

    reply.nameRefDesc = refIsNewHandle;
    reply.pathRefDesc = refIsNewHandle;

    SFGetFile2(0x15, 0x15, refIsPointer, (Ref)prompt, NULL, &types, &reply);
    if (!reply.good) {
        return NULL;
    }

    filePath = (ResultBuf255Hndl)reply.pathRef;
    fileName = (ResultBuf255Hndl)reply.nameRef;

    /* if the file is not in the same dir as the repo, bail */
    if (strncmp((*filePath)->bufString.text, pathname.text, pathname.length) != 0) {
        warn("Can't add files from a directory other than the repo's");
        return NULL;
    }

    /* make sure the file isn't already in the repo */

    for (i = 0; i < repo->nfiles; i++) {
        file = repo->files[i];
        if ((file->filename.textLength == (*fileName)->bufString.length) &&
            (strncmp(repo->files[i]->filename.text, (*fileName)->bufString.text, (*fileName)->bufString.length) == 0)) {
            char tmp[32];
            g2cstr(tmp, (*fileName)->bufString);
            warn("%s already exists in this repo", tmp);
            DisposeHandle((Handle)filePath);
            DisposeHandle((Handle)fileName);
            return NULL;
        }
    }

    WaitCursor();

    repo->nfiles++;
    repo->files = xrealloc(repo->files, repo->nfiles * sizeof(Ptr));
    file = repo->files[repo->nfiles - 1] =
        xmalloczero(sizeof(struct repo_file), "repo_add_file");

    file->id = repo->next_file_id;
    repo->next_file_id++;
    g2sstr(file->filename, (*fileName)->bufString);

    repo_file_update(repo, file);

    repo_sort_files(repo);

    InitCursor();

    DisposeHandle((Handle)filePath);
    DisposeHandle((Handle)fileName);

    return file;
}

word repo_file_update(struct repo *repo, struct repo_file *file) {
    struct repo_file_attrs attrs;
    size_t size, datapos;
    word error, len;
    unsigned char *data;

    error = repo_get_file_attrs(repo, &file->filename, &attrs);
    if (error && error != fileNotFound) {
        warn("Failed to get info for %s", file->filename);
        return -1;
    }

    if (error == fileNotFound) {
        file->flags |= REPO_FILE_DELETED;
    } else {
        file->flags &= ~REPO_FILE_DELETED;

        file->type = attrs.type;
        file->auxType = attrs.auxType;
        file->ctime = attrs.ctime;
        file->mtime = attrs.mtime;
    }

    /* filename len, filename, type, creator, ctime, mtime, flags */
    len = 1 + file->filename.textLength + 4 + 4 + 4 + 4 + 1;

    data = xmalloczero(len, "repo_file_update");
    datapos = 0;

    /* copy filename as pstr */
    memcpy(data, &file->filename, file->filename.textLength + 1);
    datapos += file->filename.textLength + 1;

    /* file type, creator, and dates */
    memcpy(data + datapos, &file->type, 4);
    datapos += 4;
    memcpy(data + datapos, &file->auxType, 4);
    datapos += 4;
    memcpy(data + datapos, &file->ctime, 4);
    datapos += 4;
    memcpy(data + datapos, &file->mtime, 4);
    datapos += 4;
    memcpy(data + datapos, &file->flags, 1);
    datapos += 1;

    if (datapos != len) {
        panic("repo_file_update: datapos %lu, expected %d", datapos, len);
    }

    size = bile_write(repo->bile, REPO_FILE_RTYPE, file->id, data, datapos);
    if (size != datapos) {
        panic("repo_file_update: failed writing file data: %d",
              bile_error(repo->bile));
    }

    xfree(&data);
    return 0;
}

word repo_get_file_attrs(struct repo *repo, StringPtr filename,
                         struct repo_file_attrs *attrs) {
    GSString255 path;
    FileInfoRecGS fiRec;
    word error;
    struct tm ts;

    /* lookup file type and creator */
    error = getpath(repo->bile->frefnum, filename, &path, true);
    if (error != 0) {
        return error;
    }

    error = FStat(&path, &fiRec);
    if (error) {
        return error;
    }

    attrs->type = fiRec.fileType;
    attrs->auxType = fiRec.auxType;
    ts.tm_sec = fiRec.createDateTime.second;
    ts.tm_min = fiRec.createDateTime.minute;
    ts.tm_hour = fiRec.createDateTime.hour;
    ts.tm_year = fiRec.createDateTime.year;
    ts.tm_mon = fiRec.createDateTime.month;
    ts.tm_mday = fiRec.createDateTime.day;
    ts.tm_wday = fiRec.createDateTime.weekDay;
    attrs->ctime = ConvSeconds(TimeRec2Secs, 0, (Pointer)&fiRec.createDateTime);

    ts.tm_sec = fiRec.modDateTime.second;
    ts.tm_min = fiRec.modDateTime.minute;
    ts.tm_hour = fiRec.modDateTime.hour;
    ts.tm_year = fiRec.modDateTime.year;
    ts.tm_mon = fiRec.modDateTime.month;
    ts.tm_mday = fiRec.modDateTime.day;
    ts.tm_wday = fiRec.modDateTime.weekDay;
    attrs->mtime = ConvSeconds(TimeRec2Secs, 0, (Pointer)&fiRec.modDateTime);

    return 0;
}

word repo_checkout_file(struct repo *repo, struct repo_file *file,
                        StringPtr filename) {
    GSString255 newPath, filePath = { 0 };
    ChangePathRecGS changeRec = { 2, &filePath, &newPath };
    struct bile_object *textob;
    size_t size;
    word error, frefnum;
    char *text;

    s2gstr(newPath, (*filename));

    getpath(repo->bile->frefnum, &file->filename, &filePath, true);
    error = FSDelete(&newPath);
    if (error && error != fileNotFound) {
        warn("Unable to delete %s", newPath.text);
        return -1;
    }

    ChangePathGS(&changeRec);


    textob = bile_find(repo->bile, REPO_TEXT_RTYPE, file->id);
    if (textob == NULL) {
        warn("No copy of file %s exists in repo", file->filename);
        return -1;
    }

    error = FSDelete(&filePath);
    if (error && error != fileNotFound) {
        warn("Unable to remove %s. error %x", p2cstr((char *) &file->filename), error);
    }

    error = FCreate(repo->bile->frefnum, &file->filename, file->type, file->auxType, 0);
    if (error && error != dupPathname) {
        warn("Failed to create file %s: %d", p2cstr((char *)filename),
             error);
        xfree(&textob);
        return -1;
    }

    error = FOpen(repo->bile->frefnum, &file->filename, writeEnable, &frefnum, NULL);
    if (error) {
        panic("Failed to open file %s: %d", p2cstr((char *)&filename), error);
    }

    /* TODO: add offset to bile_read to read in chunks */
    text = xmalloc(textob->size, "repo_checkout_file");
    size = bile_read_object(repo->bile, textob, text, textob->size);
    if (size != textob->size) {
        panic("Failed to read text object %ld: %d", textob->id,
              bile_error(repo->bile));
    }

    error = FWrite(frefnum, text, &size);
    if (error) {
        panic("Failed to write file to %s: %d", p2cstr((char *)&filename), error);
    }

    xfree(&text);
    xfree(&textob);
    FClose(frefnum);

    return 0;
}

void repo_sort_files(struct repo *repo) {
    struct repo_file *file;
    word i, j;

    for (i = 0; i < repo->nfiles; i++) {
        for (j = 0; j < repo->nfiles - i - 1; j++) {
            if (strnatcmp(repo->files[j]->filename.text,
                          repo->files[j + 1]->filename.text) == 1) {
                file = repo->files[j];
                repo->files[j] = repo->files[j + 1];
                repo->files[j + 1] = file;
            }
        }
    }
}

void repo_sort_amendments(struct repo *repo) {
    struct repo_amendment *amendment;
    word i, j;

    /* reverse order, newest amendment first */
    for (i = 0; i < repo->namendments; i++) {
        for (j = 0; j < repo->namendments - i - 1; j++) {
            if (repo->amendments[j]->id < repo->amendments[j + 1]->id) {
                amendment = repo->amendments[j];
                repo->amendments[j] = repo->amendments[j + 1];
                repo->amendments[j + 1] = amendment;
            }
        }
    }
}

word repo_diff_file(struct repo *repo, struct repo_file *file) {
    Str255 fromfilename, tofilename;
    GSString255 fromfilepath, tofilepath;
    Str255 label0, label1;
    struct repo_file_attrs attrs;
    size_t size;
    char *text;
    word error, ret = D_SAME, frefnum, tofile_empty = 0;

    /* write out old file */
    strcpy(fromfilename.text, "tmp");
    fromfilename.textLength = strlen(fromfilename.text);
    error = getpath(repo->bile->frefnum, &fromfilename, &fromfilepath, true);
    if (error) {
        panic("Failed to get refInfo %d", error);
    }

    /* create file */
    error = FCreate(repo->bile->frefnum, &fromfilename, file->type, file->auxType, 0);
    if (error && error != dupPathname) {
        panic("Failed to create file %s: %d", p2cstr((char *) &fromfilename),
              error);
    }

    error = FOpen(repo->bile->frefnum, &fromfilename, readEnableAllowWrite, &frefnum, NULL);
    if (error) {
        panic("Failed to open file %s: %d", p2cstr((char *) &fromfilename), error);
    }

    error = FSetEOF(frefnum, 0);
    if (error) {
        panic("Failed to truncate file %s: %d", p2cstr((char *) &fromfilename),
              error);
    }

    if (file->flags & REPO_FILE_DELETED) {
        /* don't write any TEXT resource, the from file should be blank */
    } else {
        /* if there's no existing TEXT resource, it's a new file */

        size = bile_read_alloc(repo->bile, REPO_TEXT_RTYPE, file->id,
                               &text);
        if (size > 0) {
            error = FWrite(frefnum, text, &size);
            if (error) {
                panic("Failed to write old file to %s: %d",
                      p2cstr((char *) &fromfilename), error);
            }
            xfree(&text);
        }
    }

    FClose(frefnum);

    memcpy((char *) &tofilename, (char *) &file->filename, sizeof(tofilename));
    error = getpath(repo->bile->frefnum, &tofilename, &tofilepath, true);
    if (error) {
        panic("Failed to get refInfo %d", error);
    }

    error = repo_get_file_attrs(repo, &tofilename, &attrs);
    if (error == fileNotFound) {
        /* file no longer exists, create empty temp file */

        error = FCreate(repo->bile->frefnum, &file->filename, file->type, file->auxType, 0);
        if (error && error != dupPathname) {
            panic("Failed to create file %s: %d", tofilename.text,
                  error);
        }

        tofile_empty = 1;
    } else if (error) {
        panic("Failed to get info for %s", tofilename.text);
    }
    /* specify diff header labels to avoid printing tmp filename */
    /* (TODO: use paths relative to repo) */
    snprintf((char *)label0.text, sizeof(label0.text), "%s\t%s", file->filename.text,
             timeString(file->mtime ? file->mtime : attrs.mtime));

    snprintf((char *)label1.text, sizeof(label1.text), "%s\t%s", file->filename.text,
             timeString(attrs.mtime));
    label0.textLength = strlen(label0.text);
    label1.textLength = strlen(label1.text);

    label[0] = label0.text;
    label[1] = label1.text;

    strcpy(fromfilename.text, fromfilepath.text);
    fromfilename.textLength = fromfilepath.length;
    strcpy(tofilename.text, tofilepath.text);
    tofilename.textLength = tofilepath.length;

    ret = diffreg(&fromfilename, &tofilename, D_PROTOTYPE);

    /* delete temp file */
    error = FSDelete(&fromfilepath);
    if (error) {
        panic("Failed to delete temp file %s: %d", fromfilename.text,
              error);
    }

    if (tofile_empty) {
        error = FSDelete(&tofilepath);
        if (error) {
            panic("Failed to delete temp file %s: %d",
                  tofilename.text, error);
        }
    }

    if (ret == D_SAME) {
        return 0;
    }

    return 1;
}

word repo_file_changed(struct repo *repo, struct repo_file *file) {
    GSString255 path = { 0 };
    FileInfoRecGS fiRec;
    struct bile_object *bob;
    long fsize;
    word error;

    /* if there's no existing TEXT resource, it's a new file */
    bob = bile_find(repo->bile, REPO_TEXT_RTYPE, file->id);
    if (bob == NULL) {
        return 1;
    }
    fsize = bob->size;
    xfree(&bob);

    /* lookup file type and creator */
    error = getpath(repo->bile->frefnum, &file->filename, &path, true);
    if (error != 0) {
        return error;
    }

    error = FStat(&path, &fiRec);
    if (error) {
        return error;
    }

    if (!error) {
        if (file->flags & REPO_FILE_DELETED) {
            return 1;
        }
    } else {
        if (file->flags & REPO_FILE_DELETED) {
            return 0;
        }
        return 1;
    }

    if (fiRec.eof != fsize) {
        return 1;
    }
    if (ConvSeconds(TimeRec2Secs, 0, (Pointer)&fiRec.createDateTime) != file->ctime) {
        return 1;
    }
    if (ConvSeconds(TimeRec2Secs, 0, (Pointer)&fiRec.modDateTime) != file->mtime) {
        return 1;
    }

    return 0;
}

void repo_export_patch(struct repo *repo, struct repo_amendment *amendment,
                       StringPtr filename) {
    struct bile_object *bob;
    size_t size;
    char *buf = NULL;
    word error, frefnum;

    bob = bile_find(repo->bile, REPO_DIFF_RTYPE, amendment->id);
    if (bob == NULL) {
        panic("failed finding DIFF %d", amendment->id);
    }

    /*
     * Don't use our creator here because we don't want finder opening us
     * to view diffs, they are plain text
     */
    error = FCreate(0, filename, DIFF_FILE_TYPE, DIFF_AUX_TYPE, 0);
    if (error && error != dupPathname) {
        warn("Failed to create file %s: %d", p2cstr((char *)filename),
             error);
        return;
    }

    error = FOpen(0, filename, writeEnable, &frefnum, NULL);
    if (error) {
        panic("Failed to open file %s: %d", p2cstr((char *)filename), error);
    }

    error = FSetEOF(frefnum, 0);
    if (error) {
        panic("Failed to truncate file %s: %d", p2cstr((char *)filename),
              error);
    }

    size = repo_diff_header(repo, amendment, &buf);
    error = FWrite(frefnum, buf, &size);
    if (error) {
        panic("Failed to write diff header to %s: %d",  p2cstr((char *)filename),
              error);
    }
    xfree(&buf);

    buf = xmalloc(bob->size, "repo_export_patch");
    size = bile_read_object(repo->bile, bob, buf, bob->size);
    error = FWrite(frefnum, buf, &size);
    if (error) {
        panic("Failed to write diff to %s: %d", p2cstr((char *)filename), error);
    }

    xfree(&buf);
    xfree(&bob);

    FClose(frefnum);
}


void repo_amend(struct repo *repo, struct diffed_file *diffed_files,
                word nfiles, word adds, word subs, char *author, Handle log,
                word loglen, Handle diff, unsigned long difflen) {

    Str255 tfilename;
    struct repo_amendment *amendment;
    unsigned long datalen, fsize;
    unsigned char *tdata;
    char *amendment_data;
    GSString255 path = { 0, { 0 } };
    FileInfoRecGS fiRec = { 4, 0 };
    size_t size;
    word i, error, frefnum;
    TimeRec tm;

    amendment = xmalloczero(sizeof(struct repo_amendment),
                            "repo_amend amendment");
    amendment->id = repo->next_amendment_id;
    tm = ReadTimeHex();
    amendment->date = ConvSeconds(TimeRec2Secs, 0, (Pointer)&tm);

    /* find files with actual data changes */
    amendment->nfiles = 0;
    amendment->file_ids = xcalloc(sizeof(word), nfiles,
                                  "repo_amend file_ids");
    for (i = 0; i < nfiles; i++) {
        if (diffed_files[i].flags & DIFFED_FILE_TEXT) {
            amendment->file_ids[amendment->nfiles] =
                diffed_files[i].file->id;
            amendment->nfiles++;
        }
    }
    if (amendment->nfiles == 0) {
        panic("repo_amendment passed nfiles %d but actual files is 0",
              nfiles);
    }

    strlcpy(amendment->author, author, sizeof(amendment->author));
    amendment->adds = adds;
    amendment->subs = subs;

    /* caller expects to be able to free their version, so make our own */
    amendment->log_len = loglen;
    amendment->log = xNewHandle(loglen);
    HLock(log);
    HLock(amendment->log);
    memcpy(*(amendment->log), *log, loglen);
    HUnlock(amendment->log);
    HUnlock(log);

    repo_marshall_amendment(amendment, &amendment_data, &datalen);

    /* store diff */
    HLock(diff);
    progress("Storing diff...");
    size = bile_write(repo->bile, REPO_DIFF_RTYPE, amendment->id, *diff,
                      difflen);
    if (size != difflen) {
        panic("Failed storing diff in repo file: %d",
              bile_error(repo->bile));
    }
    HUnlock(diff);

    /* store amendment */
    progress("Storing amendment metadata...");
    size = bile_write(repo->bile, REPO_AMENDMENT_RTYPE, amendment->id,
                      amendment_data, datalen);
    if (size != datalen) {
        panic("Failed storing amendment in repo file: %d",
              bile_error(repo->bile));
    }
    xfree(&amendment_data);

    /* store new versions of each file */
    for (i = 0; i < nfiles; i++) {
        if (diffed_files[i].flags & DIFFED_FILE_TEXT) {
            memcpy(&tfilename, &diffed_files[i].file->filename,
                   sizeof(tfilename));
            progress("Storing updated %s...", p2cstr((char *) &tfilename));

            /* update file contents if file wasn't deleted */
            error = getpath(repo->bile->frefnum, &tfilename, &path, true);

            error = FStat(&path, &fiRec);
            if (error && error != fileNotFound) {
                panic("Error getting file info for %s",
                      p2cstr((char *) &tfilename));
            }

            if (error != fileNotFound) {
                error = FOpen(repo->bile->frefnum, &tfilename, readEnable, &frefnum, &fsize);
                if (error) {
                    panic("Failed to open file %s: %d", p2cstr((char *) &tfilename),
                          error);
                }


                tdata = xmalloc(fsize, "repo_amend data");
                error = FRead(frefnum, tdata, &fsize);
                if (error) {
                    panic("Failed to read %ul of file %s: %d", fsize,
                          p2cstr((char *) &tfilename), error);
                }

                FClose(frefnum);

                size = bile_write(repo->bile, REPO_TEXT_RTYPE,
                                  diffed_files[i].file->id, tdata, fsize);
                if (size != fsize) {
                    panic("Failed to write new text file at %s: %d",
                          p2cstr((char *) &tfilename), bile_error(repo->bile));
                }
                xfree(&tdata);
            }
        }

        if (diffed_files[i].flags & DIFFED_FILE_METADATA) {
            repo_file_update(repo, diffed_files[i].file);
        }
    }

    /* flush volume */
    bile_flush(repo->bile, 1);

    repo->next_amendment_id = amendment->id + 1;

    /* update amendment list */
    repo->namendments++;
    repo->amendments = xreallocarray(repo->amendments, repo->namendments,
                                     sizeof(Ptr));
    repo->amendments[repo->namendments - 1] = amendment;

    repo_sort_amendments(repo);
}

void repo_marshall_amendment(struct repo_amendment *amendment, char **retdata,
                             unsigned long *retlen) {
    word len, pos = 0;
    char *data;
    word i;
    char clen;

    /* date (long) */
    len = sizeof(long);

    /* author (pstr) */
    len += 1 + strlen(amendment->author);

    /* nfiles (word) */
    len += sizeof(word) + (amendment->nfiles * sizeof(word));

    /* adds (word) */
    len += sizeof(word);

    /* deletions (word) */
    len += sizeof(word);

    /* log (wstr) */
    len += sizeof(word) + amendment->log_len;

    *retdata = xmalloc(len, "repo_marshall_amendment");
    data = *retdata;

    data[pos++] = (amendment->date >> 24) & 0xff;
    data[pos++] = (amendment->date >> 16) & 0xff;
    data[pos++] = (amendment->date >> 8) & 0xff;
    data[pos++] = amendment->date & 0xff;

    clen = strlen(amendment->author);
    data[pos++] = clen;
    for (i = 0; i < clen; i++) data[pos++] = amendment->author[i];

    data[pos++] = (amendment->nfiles >> 8) & 0xff;
    data[pos++] = amendment->nfiles & 0xff;
    for (i = 0; i < amendment->nfiles; i++) {
        data[pos++] = (amendment->file_ids[i] >> 8) & 0xff;
        data[pos++] = amendment->file_ids[i] & 0xff;
    }

    data[pos++] = (amendment->adds >> 8) & 0xff;
    data[pos++] = amendment->adds & 0xff;

    data[pos++] = (amendment->subs >> 8) & 0xff;
    data[pos++] = amendment->subs & 0xff;

    HLock(amendment->log);
    data[pos++] = (amendment->log_len >> 8) & 0xff;
    data[pos++] = amendment->log_len & 0xff;
    memcpy(data + pos, *(amendment->log), amendment->log_len);
    pos += amendment->log_len;
    HUnlock(amendment->log);

    if (pos != len) panic("repo_marshall_amendment: accumulated len %d != expected %d",
                          pos, len);

    *retlen = len;
}

word repo_migrate(struct repo *repo, word is_new) {
#if 0
    struct bile_object *bob;
    Str255 tname;
    Handle h;
    size_t size;
    unsigned long id;
    long ntmpls;
#endif
    char ver;

    if (is_new) {
        ver = REPO_CUR_VERS;
    } else {
        if (bile_read(repo->bile, REPO_VERS_RTYPE, 1, &ver, 1) != 1) {
            ver = 1;
        }

        if (ver == REPO_CUR_VERS) {
            return 0;
        }

        if (ask("Migrate this repo from version %d to %d to open it?",
                ver, REPO_CUR_VERS) != ASK_YES) {
            progress(NULL);
            return -1;
        }
    }

    if (!is_new) {
        progress("Backing up repo...");
        repo_backup(repo);
    }

    /* AmendGS...nothing to migrate from..this code wont work but no sense in fixing it */

    /* per-version migrations */

    /* 1 had no version number :( */
    /* 1->2 added a version */
    /* 2->3 was switching from resource forks to bile */

    /* store new version */
    ver = REPO_CUR_VERS;
    if (bile_write(repo->bile, REPO_VERS_RTYPE, 1, &ver, 1) != 1) {
        panic("Failed writing new version: %d", bile_error(repo->bile));
    }

    bile_verify(repo->bile);

#if 0
    progress("Copying templates...");

    /* copy templates from our resource file to bile file */
    while ((bob = bile_get_nth_of_type(repo->bile, 0, 'TMPL')) != NULL) {
        bile_delete(repo->bile, 'TMPL', bob->id);
        xfree(&bob);
    }

    ntmpls = CountResources(REPO_TMPL_RTYPE);
    for (long i = 0; i < ntmpls; i++) {
        h = LoadResource(REPO_TMPL_RTYPE, i + 1);
        if (h == NULL) panic("Failed fetching TMPL %d", i + 1);
        //GetResInfo(h, &id, &type, &tname);

        /*
         * The template name should be an OSType as a string, use it as
         * the id
         */
        if (tname.text[0] != 4) {
            panic("TMPL %ld has bogus name \"%s\"", id, p2cstr((char *) tname));
        }
        tmpltype = ((unsigned long)tname.text[1] << 24) |
        ((unsigned long)tname.text[2] << 16) |
        ((unsigned long)tname.text[3] << 8) |
        ((unsigned long)tname.text[4]);
        size = bile_write(repo->bile, 'TMPL', tmpltype, *h, GetHandleSize(h));
        if (size != GetHandleSize(h)) {
            panic("word write saving TMPL %ld: %d", id, bile_error(repo->bile));
        }

        DetachResource(REPO_TMPL_RTYPE, i + 1);
    }
#endif
    progress("Doing a full check of the new repo...");
    bile_verify(repo->bile);

    progress(NULL);

    return 0;
}

void repo_backup(struct repo *repo) {
    ResultBuf255 pathname = { 255, { 0 } };
    GSString255 destPath;
    char *pos;
    word error;

    pos = strrchr(pathname.bufString.text, ':');
    if (pos) {
        pos++;
        *pos = 0;
    }
    memcpy(&destPath, &pathname.bufString, sizeof(GSString255));
    strcat(pathname.bufString.text, repo->bile->filename.text);
    strcat(destPath.text, "repo.backup");
    pathname.bufString.length = strlen(pathname.bufString.text);
    destPath.length = strlen(destPath.text);
    error = copy_file(&pathname.bufString, &destPath, true);
    if (error) {
        panic("Failed backing up repo: %d", error);
    }
}


