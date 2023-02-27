/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_nffw.h
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */
#ifndef GRUB_PCINET_NFP_NFFW_HEADER
#define GRUB_PCINET_NFP_NFFW_HEADER 1

#include <grub/pcinet/netronome/nfp.h>
#include <grub/pcinet/netronome/nfp_cpp.h>
/* Implemented in nfp_nffw.c */

struct nfp_nffw_info;

struct nfp_nffw_info *nfp_nffw_info_open(struct nfp_cpp *cpp);
void nfp_nffw_info_close(struct nfp_nffw_info *state);
int nfp_nffw_info_mip_first(struct nfp_nffw_info *state, grub_uint32_t *cpp_id, grub_uint64_t *off);

/* Implemented in nfp_mip.c */

struct nfp_mip;

struct nfp_mip *nfp_mip_open(struct nfp_cpp *cpp);
void nfp_mip_close(struct nfp_mip *mip);

const char *nfp_mip_name(const struct nfp_mip *mip);
void nfp_mip_symtab(const struct nfp_mip *mip, grub_uint32_t *addr, grub_uint32_t *size);
void nfp_mip_strtab(const struct nfp_mip *mip, grub_uint32_t *addr, grub_uint32_t *size);

/* Implemented in nfp_rtsym.c */

enum nfp_rtsym_type {
	NFP_RTSYM_TYPE_NONE	= 0,
	NFP_RTSYM_TYPE_OBJECT	= 1,
	NFP_RTSYM_TYPE_FUNCTION	= 2,
	NFP_RTSYM_TYPE_ABS	= 3,
};

#define NFP_RTSYM_TARGET_NONE		0
#define NFP_RTSYM_TARGET_LMEM		-1
#define NFP_RTSYM_TARGET_EMU_CACHE	-7

/**
 * struct nfp_rtsym - RTSYM descriptor
 * @name:	Symbol name
 * @addr:	Address in the domain/target's address space
 * @size:	Size (in bytes) of the symbol
 * @type:	NFP_RTSYM_TYPE_* of the symbol
 * @target:	CPP Target identifier, or NFP_RTSYM_TARGET_*
 * @domain:	CPP Target Domain (island)
 */
struct nfp_rtsym {
	const char *name;
	grub_uint64_t addr;
	grub_uint64_t size;
	enum nfp_rtsym_type type;
	int target;
	int domain;
};

struct nfp_rtsym_table;

struct nfp_rtsym_table *nfp_rtsym_table_read(struct nfp_cpp *cpp);
struct nfp_rtsym_table *
__nfp_rtsym_table_read(struct nfp_cpp *cpp, const struct nfp_mip *mip);
const struct nfp_rtsym *
nfp_rtsym_lookup(struct nfp_rtsym_table *rtbl, const char *name);
#endif /* NFP_NFFW_H */

