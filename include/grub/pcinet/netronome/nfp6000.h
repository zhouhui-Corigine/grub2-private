/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */
#ifndef GRUB_PCINET_NFP6000_HEADER
#define GRUB_PCINET_NFP6000_HEADER 1
#include <grub/err.h>
#include <grub/types.h>

/* CPP Target IDs */
#define NFP_CPP_TARGET_INVALID          0
#define NFP_CPP_TARGET_NBI              1
#define NFP_CPP_TARGET_QDR              2
#define NFP_CPP_TARGET_ILA              6
#define NFP_CPP_TARGET_MU               7
#define NFP_CPP_TARGET_PCIE             9
#define NFP_CPP_TARGET_ARM              10
#define NFP_CPP_TARGET_CRYPTO           12
#define NFP_CPP_TARGET_ISLAND_XPB       14      /* Shared with CAP */
#define NFP_CPP_TARGET_ISLAND_CAP       14      /* Shared with XPB */
#define NFP_CPP_TARGET_CT_XPB           14
#define NFP_CPP_TARGET_LOCAL_SCRATCH    15
#define NFP_CPP_TARGET_CLS              NFP_CPP_TARGET_LOCAL_SCRATCH

#define NFP_ISL_EMEM0			24

#define NFP_MU_ADDR_ACCESS_TYPE_MASK	3ULL
#define NFP_MU_ADDR_ACCESS_TYPE_DIRECT	2ULL

#define PUSHPULL(_pull, _push)		((_pull) << 4 | (_push) << 0)
#define PUSH_WIDTH(_pushpull)		pushpull_width((_pushpull) >> 0)
#define PULL_WIDTH(_pushpull)		pushpull_width((_pushpull) >> 4)

static inline grub_int32_t pushpull_width(grub_int32_t pp)
{
  pp &= 0xf;

  if (pp == 0)
    return -GRUB_ERR_BAD_ARGUMENT;
  return 2 << pp;
}

grub_int32_t nfp_target_pushpull(grub_uint32_t cpp_id, grub_uint64_t address);
grub_int32_t nfp_target_cpp(grub_uint32_t cpp_island_id, grub_uint64_t cpp_island_address,
                            grub_uint32_t *cpp_target_id, grub_uint64_t *cpp_target_address,
                            const grub_uint32_t *imb_table);

#endif

