// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_rtsym.c
 * Interface for accessing run-time symbol table
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */


#include <grub/i18n.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/net/ethernet.h>
#include <grub/net/arp.h>
#include <grub/net/ip.h>
#include <grub/loader.h>
#include <grub/bufio.h>
#include <grub/kernel.h>
#include <grub/pcinet/netronome/nfp_nffw.h>
#include <grub/pcinet/netronome/nfp6000.h>
#include <grub/pcinet/netronome/nfp_cpp.h>

/* These need to match the linker */
#define SYM_TGT_LMEM		0
#define SYM_TGT_EMU_CACHE	0x17

struct nfp_rtsym_entry {
  grub_uint8_t	type;
  grub_uint8_t	target;
  grub_uint8_t	island;
  grub_uint8_t	addr_hi;
  grub_uint32_t	addr_lo;
  grub_uint16_t	name;
  grub_uint8_t	menum;
  grub_uint8_t	size_hi;
  grub_uint32_t	size_lo;
};

struct nfp_rtsym_table {
  struct nfp_cpp *cpp;
  grub_int32_t num;
  char *strtab;
  struct nfp_rtsym symtab[];
};

static grub_int32_t nfp_meid(grub_uint8_t island_id, grub_uint8_t menum)
{
  return (island_id & 0x3F) == island_id && menum < 12 ?
    (island_id << 4) | (menum + 4) : -1;
}

static void
nfp_rtsym_sw_entry_init(struct nfp_rtsym_table *cache, grub_uint32_t strtab_size,
      struct nfp_rtsym *sw, struct nfp_rtsym_entry *fw)
{
  sw->type = fw->type;
  sw->name = cache->strtab + grub_le_to_cpu16(fw->name) % strtab_size;
  sw->addr = ((grub_uint64_t)fw->addr_hi << 32) | grub_le_to_cpu32(fw->addr_lo);
  sw->size = ((grub_uint64_t)fw->size_hi << 32) | grub_le_to_cpu32(fw->size_lo);

  switch (fw->target) {
  case SYM_TGT_LMEM:
    sw->target = NFP_RTSYM_TARGET_LMEM;
    break;
  case SYM_TGT_EMU_CACHE:
    sw->target = NFP_RTSYM_TARGET_EMU_CACHE;
    break;
  default:
    sw->target = fw->target;
    break;
  }

  if (fw->menum != 0xff)
    sw->domain = nfp_meid(fw->island, fw->menum);
  else if (fw->island != 0xff)
    sw->domain = fw->island;
  else
    sw->domain = -1;
}

struct nfp_rtsym_table *nfp_rtsym_table_read(struct nfp_cpp *cpp)
{
  struct nfp_rtsym_table *rtbl;
  struct nfp_mip *mip;

  mip = nfp_mip_open(cpp);
  rtbl = __nfp_rtsym_table_read(cpp, mip);
  nfp_mip_close(mip);

  return rtbl;
}

struct nfp_rtsym_table *
__nfp_rtsym_table_read(struct nfp_cpp *cpp, const struct nfp_mip *mip)
{
  const grub_uint32_t dram = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0) |
                             NFP_ISL_EMEM0;
  grub_uint32_t strtab_addr, symtab_addr, strtab_size, symtab_size;
  struct nfp_rtsym_entry *rtsymtab;
  struct nfp_rtsym_table *cache;
  grub_uint32_t err, size;
  grub_int32_t n;

  if (!mip)
    return NULL;

  nfp_mip_strtab(mip, &strtab_addr, &strtab_size);
  nfp_mip_symtab(mip, &symtab_addr, &symtab_size);

  if (!symtab_size || !strtab_size || symtab_size % sizeof(*rtsymtab))
    return NULL;

  /* Align to 64 bits */
  symtab_size = round_up(symtab_size, 8);
  strtab_size = round_up(strtab_size, 8);

  rtsymtab = grub_malloc(symtab_size);
  if (!rtsymtab)
    return NULL;

  size = sizeof(*cache);
  size += symtab_size / sizeof(*rtsymtab) * sizeof(struct nfp_rtsym);
  size +=	strtab_size + 1;
  cache = grub_malloc(size);
  if (!cache)
    goto exit_free_rtsym_raw;

  cache->cpp = cpp;
  cache->num = symtab_size / sizeof(*rtsymtab);
  cache->strtab = (void *)&cache->symtab[cache->num];

  err = nfp_cpp_read(cpp, dram, symtab_addr, rtsymtab, symtab_size);
  if (err != symtab_size)
    goto exit_free_cache;

  err = nfp_cpp_read(cpp, dram, strtab_addr, cache->strtab, strtab_size);
  if (err != strtab_size)
    goto exit_free_cache;
  cache->strtab[strtab_size] = '\0';

  for (n = 0; n < cache->num; n++)
    nfp_rtsym_sw_entry_init(cache, strtab_size,
          &cache->symtab[n], &rtsymtab[n]);

  grub_free(rtsymtab);

  return cache;

exit_free_cache:
  grub_free(cache);
exit_free_rtsym_raw:
  grub_free(rtsymtab);
  return NULL;
}

/**
 * nfp_rtsym_lookup() - Return the RTSYM descriptor for a symbol name
 * @rtbl:	NFP RTsym table
 * @name:	Symbol name
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *
nfp_rtsym_lookup(struct nfp_rtsym_table *rtbl, const char *name)
{
  grub_int32_t n;

  if (!rtbl)
    return NULL;

  for (n = 0; n < rtbl->num; n++)
	  grub_printf("%s \n", rtbl->symtab[n].name);
    if (grub_strcmp(name, rtbl->symtab[n].name) == 0)
      return &rtbl->symtab[n];

  return NULL;
}




