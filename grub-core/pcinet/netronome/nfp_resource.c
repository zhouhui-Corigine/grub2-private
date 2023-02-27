/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_resource.c
 * Author: Jakub Kicinski <jakub.kicinski@netronome.com>
 *         Jason McMullan <jason.mcmullan@netronome.com>
 *
 *         Slightly modified version for u-boot. Symbol lookup by key replaced with
 *         simple string match to simplify dependencies.
 */
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/pcinet/netronome/nfp_nffw.h>
#include <grub/pcinet/netronome/nfp6000.h>

#define NFP_RESOURCE_TBL_TARGET		NFP_CPP_TARGET_MU
#define NFP_RESOURCE_TBL_BASE		0x8100000000ULL

/* NFP Resource Table self-identifier */
#define NFP_RESOURCE_TBL_NAME		"nfp.res"
#define NFP_RESOURCE_TBL_KEY		0x00000000 /* Special key for entry 0 */

#define NFP_RESOURCE_ENTRY_NAME_SZ	8

/**
 * struct nfp_resource_entry - Resource table entry
 * @mutex:	NFP CPP Lock
 * @mutex.owner:	NFP CPP Lock, interface owner
 * @mutex.key:		NFP CPP Lock, posix_crc32(name, 8)
 * @region:	Memory region descriptor
 * @region.name:	ASCII, zero padded name
 * @region.reserved:	padding
 * @region.cpp_action:	CPP Action
 * @region.cpp_token:	CPP Token
 * @region.cpp_target:	CPP Target ID
 * @region.page_offset:	256-byte page offset into target's CPP address
 * @region.page_size:	size, in 256-byte pages
 */
struct nfp_resource_entry {
	struct nfp_resource_entry_mutex {
		grub_uint32_t owner;
		grub_uint32_t key;
	} mutex;
	struct nfp_resource_entry_region {
		grub_uint8_t  name[NFP_RESOURCE_ENTRY_NAME_SZ];
		grub_uint8_t  reserved[5];
		grub_uint8_t  cpp_action;
		grub_uint8_t  cpp_token;
		grub_uint8_t  cpp_target;
		grub_uint32_t page_offset;
		grub_uint32_t page_size;
	} region;
};

#define NFP_RESOURCE_TBL_SIZE		4096
#define NFP_RESOURCE_TBL_ENTRIES	(NFP_RESOURCE_TBL_SIZE /	\
					 sizeof(struct nfp_resource_entry))

struct nfp_resource {
	char name[NFP_RESOURCE_ENTRY_NAME_SZ + 1];
	grub_uint32_t cpp_id;
	grub_uint64_t addr;
	grub_uint64_t size;
	struct nfp_cpp_mutex *mutex;
};

static grub_int32_t nfp_cpp_resource_find(struct nfp_cpp *cpp, struct nfp_resource *res)
{
	char name_pad[NFP_RESOURCE_ENTRY_NAME_SZ] = {};
	struct nfp_resource_entry entry;
	grub_uint32_t cpp_id, ret, i;

	cpp_id = NFP_CPP_ID(NFP_RESOURCE_TBL_TARGET, 3, 0);  /* Atomic read */

	grub_strncpy(name_pad, res->name, sizeof(name_pad));

	/* Search for a matching entry */
	if (!grub_memcmp(name_pad, NFP_RESOURCE_TBL_NAME "\0\0\0\0\0\0\0\0", 8)) {
		grub_dprintf("nfp", "Grabbing device lock not supported\n");
		return -GRUB_ERR_BAD_ARGUMENT;
	}

	for (i = 0; i < NFP_RESOURCE_TBL_ENTRIES; i++) {
		grub_uint64_t addr = NFP_RESOURCE_TBL_BASE +
			sizeof(struct nfp_resource_entry) * i;

		ret = nfp_cpp_read(cpp, cpp_id, addr, &entry, sizeof(entry));
		if (ret != sizeof(entry)) {
			grub_dprintf("nfp", "Read nfp resource %s error %d", res->name, ret);
			return -GRUB_ERR_IO;
		}
		/* We no longer use the key mechanism - just a string compare */
		if (grub_memcmp(name_pad, entry.region.name, 8))
			continue;

		/* Found match! */
		res->mutex = NULL;
		res->cpp_id = NFP_CPP_ID(entry.region.cpp_target,
					 entry.region.cpp_action,
					 entry.region.cpp_token);
		res->addr = (grub_uint64_t)entry.region.page_offset << 8;
		res->size = (grub_uint64_t)entry.region.page_size << 8;

		return 0;
	}

	return -GRUB_ERR_FILE_NOT_FOUND;
}

static grub_int32_t
nfp_resource_try_acquire(struct nfp_cpp *cpp, struct nfp_resource *res,
			 struct nfp_cpp_mutex *dev_mutex)
{
	grub_int32_t err;

	if (nfp_cpp_mutex_lock(dev_mutex))
		return -GRUB_ERR_WAIT;

	err = nfp_cpp_resource_find(cpp, res);
	if (err)
		goto err_unlock_dev;

	err = nfp_cpp_mutex_trylock(res->mutex);
	if (err)
		goto err_res_mutex_free;

	nfp_cpp_mutex_unlock(dev_mutex);

	return 0;

err_res_mutex_free:
	nfp_cpp_mutex_free(res->mutex);
err_unlock_dev:
	nfp_cpp_mutex_unlock(dev_mutex);

	return err;
}

/**
 * nfp_resource_acquire() - Acquire a resource handle
 * @cpp:	NFP CPP handle
 * @name:	Name of the resource
 *
 * NOTE: This function locks the acquired resource
 *
 * Return: NFP Resource handle, or ERR_PTR()
 */
struct nfp_resource *
nfp_resource_acquire(struct nfp_cpp *cpp, const char *name)
{
	struct nfp_cpp_mutex *dev_mutex;
	struct nfp_resource *res;
	grub_int32_t err;

	res = grub_malloc(sizeof(*res));
	if (!res)
		return ERR_PTR(-GRUB_ERR_OUT_OF_MEMORY);

	grub_strncpy(res->name, name, NFP_RESOURCE_ENTRY_NAME_SZ);

	dev_mutex = nfp_cpp_mutex_alloc(cpp, NFP_RESOURCE_TBL_TARGET,
					NFP_RESOURCE_TBL_BASE,
					NFP_RESOURCE_TBL_KEY);
	if (!dev_mutex) {
		grub_free(res);
		return ERR_PTR(-GRUB_ERR_OUT_OF_MEMORY);
	}

	err = nfp_resource_try_acquire(cpp, res, dev_mutex);
	if (err)
		goto err_free;

	nfp_cpp_mutex_free(dev_mutex);

	return res;

err_free:
	nfp_cpp_mutex_free(dev_mutex);
	grub_free(res);
	return ERR_PTR(err);
}

/**
 * nfp_resource_release() - Release a NFP Resource handle
 * @res:	NFP Resource handle
 *
 * NOTE: This function implictly unlocks the resource handle
 */
void nfp_resource_release(struct nfp_resource *res)
{
	nfp_cpp_mutex_unlock(res->mutex);
	nfp_cpp_mutex_free(res->mutex);
	grub_free(res);
}

/**
 * nfp_resource_cpp_id() - Return the cpp_id of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: NFP CPP ID
 */
grub_uint32_t nfp_resource_cpp_id(struct nfp_resource *res)
{
	return res->cpp_id;
}

/**
 * nfp_resource_name() - Return the name of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: const char pointer to the name of the resource
 */
const char *nfp_resource_name(struct nfp_resource *res)
{
	return res->name;
}

/**
 * nfp_resource_address() - Return the address of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: Address of the resource
 */
grub_uint64_t nfp_resource_address(struct nfp_resource *res)
{
	return res->addr;
}

/**
 * nfp_resource_size() - Return the size in bytes of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: Size of the resource in bytes
 */
grub_uint64_t nfp_resource_size(struct nfp_resource *res)
{
	return res->size;
}

