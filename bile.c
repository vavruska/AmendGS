#line 2 "/host/AmendGS/bile.c"

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
#include <string.h>
#include <stdio.h>
#include <memory.h>
#include <resources.h>
#include <gsos.h>
#include <orca.h>
#include <appleshare.h>

#include "bile.h"
#include "util.h"

segment "bile";

/* for errors not specific to a bile */
static word _bile_error = 0;

static word _bile_open_ignore_primary_map = 0;

struct bile_object*	bile_alloc(struct bile *bile, const unsigned long type,
                               const unsigned long id, const size_t size);
struct bile_object*	bile_object_in_map(struct bile *bile,
                                       const unsigned long type, const unsigned long id);
word bile_read_map(struct bile *bile,
                       struct bile_object *map_ptr);
word bile_write_map(struct bile *bile);
size_t bile_xwriteat(struct bile *bile, const size_t pos,
                         const void *data, const size_t len);
void bile_check_sanity(struct bile *bile);

/* Public API */

word bile_error(struct bile *bile) {
    if (bile == NULL) {
        return _bile_error;
    }

    return bile->last_error;
}

struct bile *bile_create(const StringPtr filename, const long creator,
                         const unsigned long type) {
    struct bile *bile = NULL;
    size_t len;
    char *tmp;
    word frefnum;
    longword fsize;

    _bile_error = 0;

    /* create file */
    _bile_error = FCreate(0, filename, type, BILE_AUX_TYPE, BILE_ALLOCATE_SIZE);
    if (_bile_error) return NULL;

    _bile_error = FOpen(0, filename, readEnableAllowWrite, &frefnum, &fsize);
    if (_bile_error) {
        return NULL;
    }

    bile = xmalloczero(sizeof(struct bile), "bile_create");
    memcpy(bile->magic, BILE_MAGIC, BILE_MAGIC_LEN);
    bile->frefnum = frefnum;
    bile->map_ptr.type = BILE_TYPE_MAPPTR;
    memcpy(&bile->filename, filename, sizeof(bile->filename));

    /* write magic */
    len = BILE_MAGIC_LEN;
    tmp = xstrdup(BILE_MAGIC, "bile_create magic");
    
    _bile_error = FWrite(frefnum, tmp, &len);
    xfree(&tmp);
    if (_bile_error) {
        goto create_bail;
    }

    /* write header pointing to blank map */
    len = sizeof(bile->map_ptr);
    _bile_error = FWrite(frefnum, &bile->map_ptr, &len);
    if (_bile_error) {
        goto create_bail;
    }

    len = sizeof(bile->old_map_ptr);
    _bile_error = FWrite(frefnum, &bile->old_map_ptr, &len);
    if (_bile_error) {
        goto create_bail;
    }

    /* padding */
    len = BILE_HEADER_LEN - BILE_MAGIC_LEN - BILE_OBJECT_SIZE -
        BILE_OBJECT_SIZE;
    tmp = xmalloczero(len, "bile_create padding");

    _bile_error = FWrite(frefnum, tmp, &len);
    if (_bile_error) {
        goto create_bail;
    }
    xfree(&tmp);

    bile->file_size = FGetMark(bile->frefnum);

    if (bile->file_size != BILE_HEADER_LEN) {
        panic("bile_create: incorrect length after writing: %ld (!= %ld)",
              bile->file_size, (long)BILE_HEADER_LEN);
    }

    return bile;

create_bail:
    FClose(frefnum);
    bile->frefnum = -1;
    if (bile != NULL) {
        xfree(&bile);
    }
    return NULL;
}

struct bile *bile_open(const StringPtr filename) {
    struct bile *bile = NULL;
    char magic[BILE_MAGIC_LEN + 1];
    size_t file_size, size;
    word frefnum;

    _bile_error = 0;

    /* open file */
    _bile_error = FOpen(0, filename, readEnableAllowWrite, &frefnum, &file_size);
    if (_bile_error) {
        return NULL;
    }

    bile = xmalloczero(sizeof(struct bile), "bile_open");
    memcpy(bile->magic, BILE_MAGIC, sizeof(bile->magic));
    bile->frefnum = frefnum;
    memcpy(&bile->filename, filename, sizeof(bile->filename));
    bile->file_size = file_size;

    /* verify magic */
    size = BILE_MAGIC_LEN;
    _bile_error =  FRead(frefnum, magic, &size);
    if (_bile_error) {
        goto open_bail;
    }

    if (strncmp(magic, BILE_MAGIC, BILE_MAGIC_LEN) != 0) {
        if (strncmp(magic, BILE1_MAGIC, BILE1_MAGIC_LEN) == 0) _bile_error = BILE_ERR_NEED_UPGRADE_1;
        else _bile_error = -1;
        goto open_bail;
    }

    /* load map pointer */
    size = sizeof(bile->map_ptr);
    _bile_error = FRead(frefnum, &bile->map_ptr, &size); 
    if (_bile_error) {
        goto open_bail;
    }

    /* old map pointer */
    size = sizeof(bile->old_map_ptr);
    _bile_error = FRead(frefnum, &bile->old_map_ptr, &size); 
    if (_bile_error) {
        goto open_bail;
    }

    if (_bile_open_ignore_primary_map) {
        if (!bile->old_map_ptr.size) goto open_bail;
        bile->map_ptr = bile->old_map_ptr;
    }

    if (bile->map_ptr.size) {
        if (bile_read_map(bile, &bile->map_ptr) != 0) {
            warn("bile_open: Failed reading map");
            goto open_bail;
        }
    }

    return bile;

open_bail:
    FClose(frefnum);
    bile->frefnum = -1;
    if (bile != NULL) {
        xfree(&bile);
    }

    return NULL;
}

void bile_check_sanity(struct bile *bile) {
    if (bile == NULL) {
        panic("bile_check_sanity: NULL bile");
    }

    if (memcmp(bile->magic, BILE_MAGIC, sizeof(bile->magic)) != 0) {
        panic("bile_check_sanity: bogus magic");
    }
}

struct bile *bile_open_recover_map(const StringPtr filename) {
    struct bile *bile;

    _bile_open_ignore_primary_map = 1;
    bile = bile_open(filename);
    _bile_open_ignore_primary_map = 0;

    if (bile) {
        bile->map_ptr = bile->old_map_ptr;
        bile_write_map(bile);
    }

    return bile;
}

word bile_flush(struct bile *bile, word and_vol) {
    word ret;

    bile_check_sanity(bile);

    ret = FFlush(bile->frefnum);

    return ret;
}

void bile_close(struct bile *bile) {
    bile_check_sanity(bile);

    _bile_error = 0;

    _bile_error = FClose(bile->frefnum);
    bile->frefnum = -1;
    if (bile->map != NULL) {
        xfree(&bile->map);
    }
}

struct bile_object *bile_find(struct bile *bile, const unsigned long 
                              type, const unsigned long id) {
    struct bile_object *o, *ocopy;
    char note[MALLOC_NOTE_SIZE];

    bile_check_sanity(bile);

    o = bile_object_in_map(bile, type, id);
    if (o == NULL) {
        return NULL;
    }

    snprintf(note, sizeof(note), "bile_find %s %lu", OSTypeToString(type),
             id);
    ocopy = xmalloc(BILE_OBJECT_SIZE, note);
    memcpy(ocopy, o, BILE_OBJECT_SIZE);

    return ocopy;
}

size_t bile_count_by_type(struct bile *bile, const unsigned long type) {
    struct bile_object *o;
    size_t n, count = 0;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    for (n = 0; n < bile->nobjects; n++) {
        o = &bile->map[n];
        if (o->type == type) count++;
    }

    return count;
}

size_t bile_sorted_ids_by_type(struct bile *bile, const unsigned long type,
                        unsigned long **ret) {
    struct bile_object *o;
    size_t count, size = 0, n, j, t;
    unsigned long *ids = NULL;

    count = 0;
    *ret = NULL;

    bile_check_sanity(bile);

    for (n = 0; n < bile->nobjects; n++) {
        o = &bile->map[n];
        if (o->type != type) {
            continue;
        }

        EXPAND_TO_FIT(ids, size, count * sizeof(size_t), sizeof(size_t),
                      10 * sizeof(size_t));
        ids[count++] = o->id;
    }

    for (n = 0; n < count; n++) {
        for (j = 0; j < count - n - 1; j++) {
            if (ids[j] > ids[j + 1]) {
                t = ids[j];
                ids[j] = ids[j + 1];
                ids[j + 1] = t;
            }
        }
    }

    *ret = ids;
    return count;
}

struct bile_object *bile_get_nth_of_type(struct bile *bile, 
                                         const unsigned long index,
                                         const unsigned long type) {
    struct bile_object *o, *ocopy;
    size_t n, count = 0;
    char note[MALLOC_NOTE_SIZE];

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    for (n = 0; n < bile->nobjects; n++) {
        o = &bile->map[n];
        if (o->type != type) {
            continue;
        }

        if (count == index) {
            snprintf(note, sizeof(note), "bile_get_nth %s %lu",
                     OSTypeToString(type), index);
            ocopy = xmalloc(BILE_OBJECT_SIZE, note);
            memcpy(ocopy, o, BILE_OBJECT_SIZE);
            return ocopy;
        }
        count++;
    }

    return NULL;
}

unsigned long bile_next_id(struct bile *bile, const unsigned long type) {
    struct bile_object *o;
    size_t n;
    unsigned long id = 1;
    unsigned long highest;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    for (n = 0; n < bile->nobjects; n++) {
        o = &bile->map[n];
        if (o->type == type && o->id >= id) id = o->id + 1;
    }

    if (bile_read(bile, BILE_TYPE_HIGHESTID, type, &highest,
                  sizeof(unsigned long)) == sizeof(unsigned long)) {
        if (highest > id) {
            id = highest + 1;
        }
    }

    return id;
}

word bile_delete(struct bile *bile, const unsigned long type, 
                 const unsigned long id) {
    static char zero[128] = { 0 };
    struct bile_object *o;
    size_t pos, size, wsize, n;
    unsigned long highest;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    o = bile_object_in_map(bile, type, id);
    if (o == NULL) {
        _bile_error = bile->last_error = -1;
        return -1;
    }

    o->type = BILE_TYPE_PURGE;
    pos = o->pos;
    size = o->size + BILE_OBJECT_SIZE;

    _bile_error = bile->last_error = FSeek(bile->frefnum, pos); 
    if (_bile_error) {
        return -1;
    }

    while (size > 0) {
        wsize = MIN(128, size);
        size -= wsize;

        _bile_error = bile->last_error = FWrite(bile->frefnum, zero, &size);
        if (_bile_error) {
            return -1;
        }
    }

    /*
     * If this is the highest id of this type, store it so it won't get
     * handed out again from bile_next_id
     */
    highest = id;
    for (n = 0; n < bile->nobjects; n++) {
        o = &bile->map[n];
        if (o->type == type && o->id > highest) {
            highest = o->id;
            break;
        }
    }

    if (highest == id) {
        /* store the type as the id, and the highest id as the data */
        bile_write(bile, BILE_TYPE_HIGHESTID, type, &highest,
                   sizeof(unsigned long));

        /* bile_write wrote a new map for us */
    } else {
        bile_write_map(bile);
        if (_bile_error) {
            return -1;
        }
    }

    return 0;
}

size_t bile_read_object(struct bile *bile, const struct bile_object *o,
                        void *data, const size_t len) {
    struct bile_object verify;
    size_t rsize, wantlen;

    bile_check_sanity(bile);

    if (o == NULL) {
        panic("bile_read_object: NULL object passed");
    }
    if (data == NULL) {
        panic("bile_read_object: NULL data pointer passed");
    }
    if (len == 0) {
        panic("bile_read_object: zero len");
    }

    _bile_error = bile->last_error = 0;

    if (o->pos + BILE_OBJECT_SIZE + o->size > bile->file_size) {
        warn("bile_read_object: object %s:%ld pos %ld size %ld > "
             "file size %ld", OSTypeToString(o->type), o->id, o->pos,
             o->size, bile->file_size);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }

    _bile_error = bile->last_error = FSeek(bile->frefnum, o->pos);
    if (_bile_error) {
        warn("bile_read_object: object %s:%lu points to bogus position "
             "%lu", OSTypeToString(o->type), o->id, o->pos);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }

    rsize = BILE_OBJECT_SIZE;
    _bile_error = bile->last_error = FRead(bile->frefnum,  &verify, &rsize);
    if (_bile_error) {
        return 0;
    }

    if (verify.id != o->id) {
        warn("bile_read_object: object %s:%ld pos %ld wrong id %ld, "
             "expected %ld", OSTypeToString(o->type), o->id, o->pos,
             verify.id, o->id);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }
    if (verify.type != o->type) {
        warn("bile_read_object: object %s:%ld pos %ld wrong type %ld, "
             "expected %ld", OSTypeToString(o->type), o->id, o->pos,
             verify.type, o->type);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }
    if (verify.size != o->size) {
        warn("bile_read_object: object %s:%ld pos %ld wrong size %ld, "
             "expected %ld", OSTypeToString(o->type), o->id, o->pos,
             verify.size, o->size);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }

    wantlen = len;
    if (wantlen > o->size) {
        wantlen = o->size;
    }

    rsize = wantlen;
    _bile_error = bile->last_error = FRead(bile->frefnum, data, &rsize); 
    if (_bile_error) {
        return 0;
    }

    if (rsize != wantlen) {
        warn("bile_read_object: %s:%lu: needed to read %ld, read %ld",
             OSTypeToString(o->type), o->id, wantlen, rsize);
        _bile_error = bile->last_error = BILE_ERR_BOGUS_OBJECT;
        return 0;
    }

    return rsize;
}

size_t bile_read(struct bile *bile, const unsigned long type, 
                 const unsigned long id, void *data, const size_t len) {
    struct bile_object *o;

    bile_check_sanity(bile);

    if (data == NULL) panic("bile_read: NULL data pointer passed");

    _bile_error = bile->last_error = 0;

    o = bile_object_in_map(bile, type, id);
    if (o == NULL) {
        _bile_error = bile->last_error = -1;
        return 0;
    }

    return bile_read_object(bile, o, data, len);
}

size_t bile_read_alloc(struct bile *bile, const unsigned long type,
                       const unsigned long id, void *data_ptr) {
    struct bile_object *o;
    size_t ret;
    char **data;
    char note[MALLOC_NOTE_SIZE];

    bile_check_sanity(bile);

    if (data_ptr == NULL) {
        panic("bile_read: NULL data pointer passed");
    }

    data = (char **)data_ptr;
    _bile_error = bile->last_error = 0;

    o = bile_object_in_map(bile, type, id);
    if (o == NULL) {
        _bile_error = bile->last_error = -1;
        *data = NULL;
        return 0;
    }

    snprintf(note, sizeof(note), "bile_read_alloc %s %ld",
             OSTypeToString(type), id);
    *data = xmalloczero(o->size, note);
    ret = bile_read_object(bile, o, *data, o->size);

    return ret;
}

size_t bile_write(struct bile *bile, const unsigned long type, 
                  const unsigned long id, const void *data, const size_t len) {
    struct bile_object *old, *new_obj;
    size_t wrote;

    bile_check_sanity(bile);

    if (len == 0) {
        panic("bile_write: zero len passed");
    }
    if (data == NULL) {
        panic("bile_write: NULL data pointer passed");
    }

    _bile_error = bile->last_error = 0;

    if ((old = bile_object_in_map(bile, type, id)) != NULL) {
        old->type = BILE_TYPE_PURGE;
    }

    new_obj = bile_alloc(bile, type, id, len);

    wrote = bile_xwriteat(bile, new_obj->pos, new_obj, BILE_OBJECT_SIZE);
    if (wrote != BILE_OBJECT_SIZE || bile->last_error) {
        return 0;
    }
    wrote = bile_xwriteat(bile, new_obj->pos + BILE_OBJECT_SIZE, data, len);
    if (wrote != len || bile->last_error) {
        return 0;
    }

    FGetEOF(bile->frefnum, &bile->file_size);

    bile_write_map(bile);
    if (bile->last_error) {
        return 0;
    }

    return wrote;
}

word bile_marshall_object(struct bile *bile,
                          const struct bile_object_field *fields, 
                          const size_t nfields, void *object, void *ret_ptr, 
                          size_t *ret_size, char *note) {
    char **ret;
    char *data, *ptr;
    size_t size = 0, fsize = 0, n;
    bool write = false;

    bile_check_sanity(bile);

    if (ret_ptr == NULL) {
        panic("bile_pack_object invalid ret");
    }

    ret = (char **)ret_ptr;
    *ret = NULL;
    *ret_size = 0;

iterate_fields:
    for (n = 0; n < nfields; n++) {
        if (fields[n].size < 0) {
            /*
             * Dynamically-sized field, get length from its length field
             * and multiply by negative size, so -1 is the actual length but
             * -4 could be used if length field is number of longs
             */
            ptr = (char *)object + fields[n].object_len_off;
            fsize = *(size_t *)ptr * -(fields[n].size);
        } else fsize = fields[n].size;

        if (write) {
            if (fields[n].size < 0) {
                /* field is dynamically allocated, first write size */
                memcpy(data + size, &fsize, sizeof(fsize));
                size += sizeof(fsize);
                if (!fsize) {
                    continue;
                }
            }

            ptr = (char *)object + fields[n].struct_off;
            if (fields[n].size < 0) {
                ptr = (char *)*(unsigned long *)ptr;
                if (ptr == 0) {
                    panic("bile_pack_object field[%lu] points to NULL", n);
                }
            }

            memcpy(data + size, ptr, fsize);
        } else if (fields[n].size < 0) {
            /* account for dynamic field length */
            size += sizeof(fsize);
        }

        size += fsize;
    }

    if (!write) {
        data = xmalloc(size, note);
        write = true;
        size = 0;
        goto iterate_fields;
    }

    *ret = data;
    *ret_size = size;

    return 0;
}

word bile_unmarshall_object(struct bile *bile,
                       const struct bile_object_field *fields, const size_t nfields,
                       const void *data, const size_t data_size, void *object,
                       const size_t object_size, bool deep, char *note) {
    size_t off, fsize = 0, n;
    char *ptr, *dptr;

    bile_check_sanity(bile);

    for (off = 0, n = 0; n < nfields; n++) {
        if (fields[n].size < 0) {
            /* dynamically-sized field, read length */
            memcpy(&fsize, (char *)data + off, sizeof(fsize));
            off += sizeof(fsize);
        } else fsize = fields[n].size;

        if (off + fsize > data_size) {
            panic("bile_unmarshall_object: overflow at field %lu of %lu!",
                                           n + 1, nfields);
        }

        ptr = (char *)object + fields[n].struct_off;

        if (fields[n].size < 0 && deep) {
            if (fsize == 0) {
                memset(ptr, 0, sizeof(dptr));
                continue;
            }
            dptr = xmalloc(fsize, note);
            memcpy(ptr, &dptr, sizeof(dptr));
            ptr = dptr;
        }

        if (fields[n].size < 0 && !deep) memset(ptr, 0, sizeof(dptr));
        else {
            if (fields[n].size > 0 &&
                fields[n].struct_off + fsize > object_size) panic("bile_unmarshall_object: overflow writing to object "
                                                                  "at field %lu! (%lu > %lu)", n + 1,
                                                                  fields[n].struct_off + fsize, object_size);
            memcpy(ptr, (char *)data + off, fsize);
        }

        off += fsize;
    }

    return 0;
}

word bile_verify(struct bile *bile) {
    size_t n, size, pos;
    char data;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    for (n = 0, pos = 0; n < bile->nobjects; n++) {
        size = bile_read_object(bile, &bile->map[n], &data, 1);
        if (bile_error(bile)) {
            return bile_error(bile);
        } else if (size == 0) {
            return -1;
        }

        if (bile->map[n].pos <= pos) {
            return -1;
        }
        pos = bile->map[n].pos + bile->map[n].size;
    }

    return 0;
}

/* Private API */

struct bile_object *bile_object_in_map(struct bile *bile, 
                                       const unsigned long type,
                                       const unsigned long id) {
    struct bile_object *o;
    size_t n;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    /* look backwards, optimizing for newer data */
    for (n = bile->nobjects; n > 0; n--) {
        o = &bile->map[n - 1];
        if (o->type == type && o->id == id) {
            return o;
        }
    }

    return NULL;
}

struct bile_object *bile_alloc(struct bile *bile, 
                               const unsigned long type, 
                               const unsigned long id,
                               const size_t size) {
    size_t last_pos = BILE_HEADER_LEN;
    size_t n, map_pos;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    /* find a gap big enough to hold our object + its size */
    for (n = 0; n < bile->nobjects; n++) {
        if (bile->map[n].pos - last_pos >= (size + BILE_OBJECT_SIZE)) {
            break;
        }
        last_pos = bile->map[n].pos + BILE_OBJECT_SIZE + bile->map[n].size;
    }

    /*
     * The map is always sorted, so walk the map to find out where to
     * wedge a copy of this new object, then return a pointer to it in
     * the map.
     */

    map_pos = bile->nobjects;
    for (n = 0; n + 1 < bile->nobjects; n++) {
        if (n == 0 && last_pos < bile->map[n].pos) {
            map_pos = 0;
            break;
        }
        if (last_pos > bile->map[n].pos &&
            last_pos < bile->map[n + 1].pos) {
            map_pos = n + 1;
            break;
        }
    }

    bile->nobjects++;
    bile->map = xreallocarray(bile->map, bile->nobjects, BILE_OBJECT_SIZE);

    if (map_pos + 1 < bile->nobjects) {
        /* shift remaining objects up */
        memmove(&bile->map[map_pos + 1], &bile->map[map_pos],
                sizeof(struct bile_object) * (bile->nobjects - map_pos - 1));
    }

    bile->map[map_pos].type = type;
    bile->map[map_pos].id = id;
    bile->map[map_pos].size = size;
    bile->map[map_pos].pos = last_pos;

    return &bile->map[map_pos];
}

word bile_read_map(struct bile *bile, struct bile_object *map_ptr) {
    size_t size;
    struct bile_object map_obj, *map;
    word error; 

    bile_check_sanity(bile);

    if (map_ptr->pos + map_ptr->size > bile->file_size) {
        warn("bile_read_map: map points to %lu + %lu, but file is only %lu",
             map_ptr->pos, map_ptr->size, bile->file_size);
        return -1;
    }

    if (map_ptr->size % BILE_OBJECT_SIZE != 0) {
        warn("bile_read_map: map pointer size is not a multiple of object "
             "size (%lu): %lu", BILE_OBJECT_SIZE, map_ptr->size);
        return -1;
    }

    /* read and verify map object header map_ptr points to */
    error = _bile_error = FSeek(bile->frefnum, map_ptr->pos);
    if (_bile_error) {
        return -1;
    }

    size = sizeof(struct bile_object);
    _bile_error = FRead(bile->frefnum, &map_obj, &size);
    if (_bile_error) {
        return -1;
    }

    if (map_obj.pos != map_ptr->pos) {
        warn("bile_read_map: map pointer points to %lu but object "
             "there has position %lu", map_ptr->pos, map_obj.pos);
        return -1;
    }

    if (map_obj.size != map_ptr->size) {
        warn("bile_read_map: map is supposed to have size %lu but "
             "object pointed to has size %lu", map_ptr->size, map_obj.size);
        return -1;
    }

    /* read entire map */
    size = map_obj.size;
    map = xmalloczero(size, "bile_read_map");
    _bile_error = FRead(bile->frefnum, map, &size);
    if (_bile_error) {
        xfree(&map);
        return -1;
    }

    bile->map = map;
    bile->nobjects = map_obj.size / BILE_OBJECT_SIZE;

    return 0;
}

word bile_write_map(struct bile *bile) {
    struct bile_object *obj, *new_map_obj, *new_map,
    *new_map_obj_in_new_map = NULL;
    size_t new_map_size, new_nobjects, new_map_id;
    size_t n;
    word ret;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    /* allocate a new map slightly larger than we need */
    new_nobjects = bile->nobjects;
    if (bile->map_ptr.pos) new_map_id = bile->map_ptr.id + 1;
    else {
        /* new file, map never written, allocate another object for map */
        new_nobjects++;
        new_map_id = 1;
    }
    new_map_size = BILE_OBJECT_SIZE * new_nobjects;
    new_map_obj = bile_alloc(bile, BILE_TYPE_MAP, new_map_id,
                             new_map_size);
    new_map = xcalloc(BILE_OBJECT_SIZE, new_nobjects, "bile_write_map");

    for (n = 0, new_nobjects = 0; n < bile->nobjects; n++) {
        obj = &bile->map[n];

        if (obj->type == BILE_TYPE_MAP && obj->pos == bile->map_ptr.pos) {
            /* don't include old map in new one */
            continue;
        }
        if (obj->type == BILE_TYPE_PURGE) {
            continue;
        }

        if (obj->type == BILE_TYPE_MAP && obj->pos == new_map_obj->pos) {
            new_map_obj_in_new_map = &new_map[new_nobjects];
        }

        new_map[new_nobjects++] = *obj;
    }

    /* shrink to new object count */
    new_map_size = BILE_OBJECT_SIZE * new_nobjects;
    new_map_obj->size = new_map_size;
    new_map_obj_in_new_map->size = new_map_size;

    /* write object header */
    bile_xwriteat(bile, new_map_obj->pos, new_map_obj, BILE_OBJECT_SIZE);
    if (bile->last_error) {
        return -1;
    }

    /* and then the map contents */
    bile_xwriteat(bile, new_map_obj->pos + BILE_OBJECT_SIZE, new_map,
                  new_map_size);
    if (bile->last_error) {
        return -1;
    }

    FGetEOF(bile->frefnum, &bile->file_size);

    if ((ret = bile_flush(bile, false)) != noError) {
        warn("bile_write_map: flush failed: %d", ret);
        return -1;
    }

    /* successfully wrote new map, switch over */
    xfree(&bile->map);
    bile->nobjects = new_nobjects;
    bile->map = new_map;
    bile->old_map_ptr.pos = bile->map_ptr.pos;
    bile->old_map_ptr.size = bile->map_ptr.size;
    bile->old_map_ptr.id = bile->map_ptr.id;
    bile->map_ptr.pos = new_map_obj->pos;
    bile->map_ptr.size = new_map_obj->size;
    bile->map_ptr.id = new_map_obj->id;

    /* write new pointer to point at new map object */
    bile_xwriteat(bile, BILE_MAGIC_LEN, &bile->map_ptr,
                  sizeof(bile->map_ptr));
    if (bile->last_error) {
        return -1;
    }
    bile_xwriteat(bile, BILE_MAGIC_LEN + sizeof(bile->map_ptr),
                  &bile->old_map_ptr, sizeof(bile->old_map_ptr));
    if (bile->last_error) {
        return -1;
    }

    if ((ret = bile_flush(bile, false)) != noError) {
        warn("bile_write_map: final flush failed: %d", ret);
        return -1;
    }

    return 0;
}

size_t bile_xwriteat(struct bile *bile, const size_t pos, 
                     const void *data, const size_t len) {
    size_t wsize, tsize;

    bile_check_sanity(bile);

    _bile_error = bile->last_error = 0;

    if (pos + len > bile->file_size) {
        /* add new space aligning to BILE_ALLOCATE_SIZE */
        tsize = pos + len;
        tsize += BILE_ALLOCATE_SIZE - (tsize % BILE_ALLOCATE_SIZE);
        FSetEOF(bile->frefnum, tsize);

        _bile_error = bile->last_error = toolerror();
        if (_bile_error) {
            return 0;
        }
        _bile_error = bile->last_error = bile_flush(bile, true);
        if (_bile_error) {
            return 0;
        }
    }

    _bile_error = bile->last_error = FSeek(bile->frefnum, pos); 
    if (_bile_error) {
        return 0;
    }

    wsize = len;
    _bile_error = bile->last_error = FWrite(bile->frefnum, data, &len);
    if (_bile_error) {
        return 0;
    }
    if (wsize != len) {
        panic("bile_xwriteat: word write of %lu at %lu to %s: %lu",
                                               len, pos, bile->filename.text, wsize);
    }

    FGetEOF(bile->frefnum, &bile->file_size);
    return wsize;
}



