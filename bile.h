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

#ifndef __BILE_H__
#define __BILE_H__

#include <types.h>
#include <stdbool.h>
//#include "util.h"

/*
 * File format:
 * [ bile header - BILE_HEADER_LEN ]
 *   [ BILE_MAGIC - BILE_MAGIC_LEN ]
 *   [ map pointer object ]
 *     [ pointer position - long ]
 *     [ pointer size - long ]
 *     [ pointer type (_BL>) - long ]
 *     [ pointer id - long ]
 *   [ previous map pointer object ]
 *     [ pointer position - long ]
 *     [ pointer size - long ]
 *     [ pointer type (_BL>) - long ]
 *     [ pointer id - long ]
 *   [ padding for future use ]
 * [ object[0] start (map points to this as its position) ]
 *   [ object[0] position - long ]
 *   [ object[0] size - long ]
 *   [ object[0] type - long ]
 *   [ object[0] id - long ]
 * [ object[1] start ]
 *   [ .. ]
 * [ map object - (pointed to by map pointer position) ]
 *     [ map position - long ]
 *     [ map size - long ]
 *     [ map type (_BLM) - long ]
 *     [ map id - long ]
 *     [ map contents ]
 */
#define BILE_MAGIC			"BILE2"
#define BILE_MAGIC_LEN		5
//'_BLM' 1598180429
#define BILE_TYPE_MAP		0x4D4C425FL
//'_BL>' 1598180414
#define BILE_TYPE_MAPPTR	0x3E4C425FL
//'_BLP' 1598180432
#define BILE_TYPE_PURGE		0x504C425FL
//'_BLH' 1598180424
#define BILE_TYPE_HIGHESTID	0x484C425FL

struct bile_object {
	unsigned long pos;
	unsigned long size;
	unsigned long type;
	unsigned long id;
};
#define BILE_OBJECT_SIZE	(sizeof(struct bile_object))
#define BILE_HEADER_LEN		256

/* allocate filesystem space in chunks of this */
#define BILE_ALLOCATE_SIZE	8192

#ifndef BILE1_MAGIC
#define BILE1_MAGIC			"BILE1"
#endif
#ifndef BILE1_MAGIC_LEN
#define BILE1_MAGIC_LEN		5
#endif

#define BILE_AUX_TYPE 'AMND'

#define BILE_ERR_NEED_UPGRADE_1	-4000
#define BILE_ERR_BOGUS_OBJECT	-4001

struct bile_highest_id {
	long type;
	unsigned long highest_id;
};

struct bile {
	struct bile_object map_ptr;
	struct bile_object old_map_ptr;
	word frefnum, last_error;
	Str255 filename;
	size_t file_size;
	struct bile_object *map; /* array of bile_objects */
	size_t nobjects;
	char magic[5];
};

struct bile_object_field {
	size_t struct_off;
	size_t size;
	size_t object_len_off;
};

word					bile_error(struct bile *bile);

struct bile *			bile_create(const StringPtr filename,
						  const long creator, const unsigned long type);
struct bile *			bile_open(const StringPtr filename);
struct bile *			bile_open_recover_map(const StringPtr filename);
word					bile_flush(struct bile *bile, word and_vol);
void					bile_close(struct bile *bile);

struct bile_object *	bile_find(struct bile *bile, const unsigned long type,
						  const unsigned long id);
size_t					bile_count_by_type(struct bile *bile,
						  const unsigned long type);
size_t					bile_sorted_ids_by_type(struct bile *bile,
						  const unsigned long type, unsigned long **ret);
struct bile_object *	bile_get_nth_of_type(struct bile *bile,
						  const unsigned long index, const unsigned long type);
unsigned long			bile_next_id(struct bile *bile, const unsigned long type);
word					bile_delete(struct bile *bile, const unsigned long type,
						  const unsigned long id);
size_t					bile_read_object(struct bile *bile,
						  const struct bile_object *o, void *data,
						  const size_t len);
size_t					bile_read(struct bile *bile, const unsigned long type,
						  const unsigned long id, void *data,
						  const size_t len);
size_t					bile_read_alloc(struct bile *bile,
						  const unsigned long type, const unsigned long id,
						  void *data_ptr);
size_t					bile_write(struct bile *bile, unsigned long type,
						  const unsigned long id, const void *data,
						  const size_t len);
word					bile_verify(struct bile *bile);

word					bile_marshall_object(struct bile *bile,
						  const struct bile_object_field *fields,
						  const size_t nfields, void *object,
						  void *ret_ptr, size_t *ret_size, char *note);
word					bile_unmarshall_object(struct bile *bile,
						  const struct bile_object_field *fields,
						  const size_t nfields, const void *data,
						  const size_t data_size, void *object,
						  const size_t object_size, bool deep, char *note);

#endif