// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/*
 * nfp_mip.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 */

#include <grub/pci.h>
#include <grub/err.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/dl.h>
#include <grub/pcinet/netronome/nfp_cpp.h>
#include <grub/pcinet/netronome/nfp_nffw.h>

#define NFP_MIP_SIGNATURE	grub_cpu_to_le32(0x0050494d)  /* "MIP\0" */
#define NFP_MIP_VERSION		grub_cpu_to_le32(1)
#define NFP_MIP_MAX_OFFSET	(256 * 1024)

struct nfp_mip {
  grub_uint32_t signature;
  grub_uint32_t mip_version;
  grub_uint32_t mip_size;
  grub_uint32_t first_entry;

  grub_uint32_t version;
  grub_uint32_t buildnum;
  grub_uint32_t buildtime;
  grub_uint32_t loadtime;

  grub_uint32_t symtab_addr;
  grub_uint32_t symtab_size;
  grub_uint32_t strtab_addr;
  grub_uint32_t strtab_size;

  char name[16];
  char toolchain[32];
};

/* Read memory and check if it could be a valid MIP */
static grub_int32_t
nfp_mip_try_read(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
                 grub_uint64_t addr, struct nfp_mip *mip)
{
  grub_int32_t ret;

  ret = nfp_cpp_read(cpp, cpp_id, addr, mip, sizeof(*mip));
  if (ret != sizeof(*mip)) {
    grub_dprintf("nfp", "Failed to read MIP data (%d, %zu)\n",
            ret, sizeof(*mip));
    return -GRUB_ERR_IO;
  }
  if (mip->signature != NFP_MIP_SIGNATURE) {
    grub_dprintf("nfp", "Incorrect MIP signature (0x%08x)\n",
             grub_cpu_to_le32(mip->signature));
    return -GRUB_ERR_BAD_SIGNATURE;
  }
  if (mip->mip_version != NFP_MIP_VERSION) {
    grub_dprintf("nfp", "Unsupported MIP version (%d)\n",
       grub_cpu_to_le32(mip->mip_version));
    return -GRUB_ERR_BAD_SIGNATURE;
  }

  return 0;
}

/* Try to locate MIP using the resource table */
static grub_int32_t nfp_mip_read_resource(struct nfp_cpp *cpp, struct nfp_mip *mip)
{
  struct nfp_nffw_info *nffw_info;
  grub_uint32_t cpp_id;
  grub_uint64_t addr;
  grub_int32_t err;

  nffw_info = nfp_nffw_info_open(cpp);
  if (IS_ERR(nffw_info))
    return PTR_ERR(nffw_info);

  err = nfp_nffw_info_mip_first(nffw_info, &cpp_id, &addr);
  if (err)
    goto exit_close_nffw;

  err = nfp_mip_try_read(cpp, cpp_id, addr, mip);
exit_close_nffw:
  nfp_nffw_info_close(nffw_info);
  return err;
}

/**
 * nfp_mip_open() - Get device MIP structure
 * @cpp:	NFP CPP Handle
 *
 * Copy MIP structure from NFP device and return it.  The returned
 * structure is handled internally by the library and should be
 * freed by calling nfp_mip_close().
 *
 * Return: pointer to mip, NULL on failure.
 */
struct nfp_mip *nfp_mip_open(struct nfp_cpp *cpp)
{
  struct nfp_mip *mip;
  grub_int32_t err;

  mip = grub_malloc(sizeof(*mip));
  if (!mip)
    return NULL;

  err = nfp_mip_read_resource(cpp, mip);
  if (err) {
    grub_free(mip);
    return NULL;
  }

  mip->name[sizeof(mip->name) - 1] = 0;

  return mip;
}

void nfp_mip_close(struct nfp_mip *mip)
{
  grub_free(mip);
}

const char *nfp_mip_name(const struct nfp_mip *mip)
{
  return mip->name;
}

/**
 * nfp_mip_symtab() - Get the address and size of the MIP symbol table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol table
 * @size:	Location for size of MIP symbol table
 */
void nfp_mip_symtab(const struct nfp_mip *mip, grub_uint32_t *addr, grub_uint32_t *size)
{
  *addr = grub_cpu_to_le32(mip->symtab_addr);
  *size = grub_cpu_to_le32(mip->symtab_size);
}

/**
 * nfp_mip_strtab() - Get the address and size of the MIP symbol name table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol name table
 * @size:	Location for size of MIP symbol name table
 */
void nfp_mip_strtab(const struct nfp_mip *mip, grub_uint32_t *addr, grub_uint32_t *size)
{
  *addr = grub_cpu_to_le32(mip->strtab_addr);
  *size = grub_cpu_to_le32(mip->strtab_size);
}

