/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011,2012,2013  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/mm.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/cache.h>
#include <grub/pcinet/netronome/nfp_nffw.h>
#include <grub/pcinet/netronome/nfp_cpp.h>
#include <grub/pcinet.h>
#include <grub/pcinet/netronome/nfp_pipe.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define BAR_SLICE_MAX (8)
#define BAR_SLICE_EXPENSION_OFFSET (0x30000u)
#define NFP_RESOURCE_TBL_TARGET		0x7
#define NFP_RESOURCE_TBL_BASE		0x8100000000ULL

struct nfp_pipe_cpp_buffer *file_buffer;
struct nfp_pipe_cpp_buffer *file_control;
struct nfp_cpp *g_cpp;

struct barslice {
  grub_uint32_t bar;
  grub_uint32_t slice;
  grub_uint32_t nfp_target;
  grub_uint64_t nfp_base;
  grub_uint64_t nfp_size;
  grub_uint32_t nfp_expansion_bar;
};

struct nfp_cpp {
  struct barslice slice[BAR_SLICE_MAX];
  grub_uint64_t bar_base;
  grub_uint64_t bar_size;
  grub_uint64_t bar_apeture_width;
};

static grub_int32_t nfp_cpp_bar_slice_lookup(struct nfp_cpp *cpp, grub_uint32_t target,
                                      grub_uint64_t address, grub_uint64_t size)
{
  for (grub_int32_t i = 0; i < BAR_SLICE_MAX; i++) {
    if (cpp->slice[i].nfp_target == target) {
      if ((address >= cpp->slice[i].nfp_base)
           && (((address + size) < (cpp->slice[i].nfp_base + cpp->slice[i].nfp_size)))) {
        // We found a valid mapping that can help us here.
        return i;
      }
    }
  }
  grub_dprintf("nfp", "Error: Cannot find a slice for memory access"
               "(Target: %u, Addr: 0x%016"PRIxGRUB_UINT64_T", Size: 0x%016"PRIxGRUB_UINT64_T"\n", target, address, size);
  return -1;
}

static grub_int32_t memcpy64(volatile grub_uint64_t *dest,volatile grub_uint64_t *src, grub_size_t size)
{
  grub_int32_t result = 0;

  /* Check 64-bit alignment in address and size */
  if (((grub_uint64_t)dest & 0x7u) || ((grub_uint64_t)src & 0x7u) || (size & 0x7u))
    result = -1;

  if (!result) {
    for (grub_uint32_t i = 0; i < (size >> 3); i++)
    {
      (dest[i]) = (src[i]);
    }
  }

  return result;
}

grub_uint32_t nfp_cpp_read(struct nfp_cpp *cpp, grub_uint32_t cpp_id, grub_uint64_t address,
                          void *kernel_vaddr, grub_size_t length)
{
  grub_int32_t slice;
  grub_uint64_t relative_address;
  grub_uint32_t target = NFP_CPP_ID_TARGET_of(cpp_id);
  grub_uint32_t island = NFP_CPP_ID_ISLAND_of(cpp_id);

  /* Do we need to translate the DDR0 access into the 40-bit address*/
  if ((island == 0x18) && (target == 0x7))
    address = address | (((grub_uint64_t)0x1) << 37);

  /* The address must be a valid global 40bit address */
  slice = nfp_cpp_bar_slice_lookup(cpp, target, address, length);
  if (slice < 0)
    return 0;

  relative_address = address & ((cpp->bar_size >> 3u) - 1u);
  memcpy64((volatile grub_uint64_t *)kernel_vaddr,
           (volatile grub_uint64_t *)(cpp->bar_base + ((cpp->bar_size >> 3) * slice) + relative_address),
           length);

  return length;
}

grub_uint32_t nfp_cpp_write(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
                          grub_uint64_t address, const void *kernel_vaddr,
                          grub_size_t length)
{
  grub_int32_t slice;
  grub_uint64_t relative_address;
  grub_uint32_t target = NFP_CPP_ID_TARGET_of(cpp_id);
  grub_uint32_t island = NFP_CPP_ID_ISLAND_of(cpp_id);

  /* Do we need to translate the DDR0 access into the 40-bit address*/
  if ((island == 0x18) && (target == 0x7))
    address = address | (((grub_uint64_t)0x1) << 37);

  /* The address must be a valid global 40bit address */
  slice = nfp_cpp_bar_slice_lookup(cpp, target, address, length);
  if (slice < 0)
    return 0;

  relative_address = address & ((cpp->bar_size >> 3u) - 1u);
  memcpy64((volatile grub_uint64_t *)(cpp->bar_base + ((cpp->bar_size >> 3) * slice) + relative_address),
           (volatile grub_uint64_t *)kernel_vaddr, length);

  return length;
}

grub_int32_t nfp_cpp_readl(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
                  grub_uint64_t address, grub_uint32_t *value)
{
  grub_uint32_t tmp;
  grub_int32_t n;

  n = nfp_cpp_read(cpp, cpp_id, address, &tmp, sizeof(tmp));
  if (n != sizeof(tmp))
    return n < 0 ? n : -GRUB_ERR_IO;

  *value = grub_le_to_cpu32(tmp);
  return 0;
}

void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex)
{
  (void)mutex;
}

grub_int32_t nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex)
{
  (void)mutex;
  return 0;
}

grub_int32_t nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex)
{
  (void)mutex;
  return 0;
}

grub_int32_t nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex)
{
  (void)mutex;
  return 0;
}

struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, grub_int32_t target, grub_uint64_t address, grub_uint32_t key_id)
{
  (void)cpp;
  (void)target;
  (void)address;
  (void)key_id;
  return (struct nfp_cpp_mutex *)-1;
}


static grub_err_t nfp_cpp_bar_slice_setup(struct nfp_cpp *cpp, grub_uint32_t bar, grub_uint32_t slice,
                                      grub_uint32_t target, grub_uint64_t base, grub_uint64_t size,
                                      grub_uint32_t expansion_reg)
{
  grub_uint32_t* p;

  // BAR0 *must* be pre-configured to access Expansion BAR regs
  if (!cpp->bar_base)
    return GRUB_ERR_IO;

  // Bar slice setup request
  cpp->slice[slice].bar = 0;
  cpp->slice[slice].slice = slice;
  cpp->slice[slice].nfp_target = target;
  cpp->slice[slice].nfp_base = base;
  cpp->slice[slice].nfp_size = size;
  cpp->slice[slice].nfp_expansion_bar = expansion_reg;

  p = (grub_uint32_t *)(cpp->bar_base + BAR_SLICE_EXPENSION_OFFSET + (slice << 2));
  if (*p != cpp->slice[slice].nfp_expansion_bar) {
    *p = cpp->slice[slice].nfp_expansion_bar;
    grub_dprintf("nfp", "Configuring Expansion BAR %u.%u (Value= 0x%08x)\n", bar, slice, *p);
  }
  else {
    grub_dprintf("nfp", "Expansion BAR %u.%u already configured (Value= 0x%08x)\n", bar, slice, *p);
  }

  return 0;
}

static void nfp_os_update_symbol_bar_set(struct nfp_cpp *cpp)
{
  grub_uint64_t base_addr;
  grub_uint64_t expansion_reg;
  struct nfp_rtsym_table *rtbl;
  struct nfp_rtsym * sym_control = NULL;
  struct nfp_rtsym * sym_buffer = NULL;


  file_buffer = grub_zalloc(sizeof(struct nfp_pipe_cpp_buffer));
  file_control = grub_zalloc(sizeof(struct nfp_pipe_cpp_buffer));
  if(!file_buffer || !file_control) {
    grub_dprintf("nfp","Can not allocate memory for file buffer!\n");
    return;
  }

  rtbl = nfp_rtsym_table_read(cpp);
  if (rtbl) {
    sym_control = (struct nfp_rtsym *)nfp_rtsym_lookup(rtbl, "os_update_control");
    sym_buffer = (struct nfp_rtsym *)nfp_rtsym_lookup(rtbl, "os_update_buffer");
  }

  if (!sym_control || !sym_buffer) {
    grub_dprintf("nfp", "NFP Firmware not detected. Using fallback NFP addresses:\n");
    file_buffer->name = OS_FILE_BUFFER;
    file_buffer->addr = OS_FILE_DEFAULT_BUFFER_ADDR;
    file_buffer->size = OS_FILE_DEFAULT_BUFFER_SIZE;
    file_buffer->cppid = NFP_CPP_ISLAND_ID(OS_FILE_DEFAULT_TARGET,
               NFP_CPP_ACTION_RW, 0,
               OS_FILE_DEFAULT_DOMAIN);

    file_control->name = OS_FILE_CONTROL;
    file_control->addr = OS_FILE_DEFAULT_CONTROL_ADDR;
    file_control->size = OS_FILE_DEFAULT_CONTROL_SIZE;
    file_control->cppid = file_buffer->cppid;
  }
  else {

  }
  grub_dprintf("nfp", "Symbol: %s, Address: 0x%016"PRIxGRUB_UINT64_T"\n", file_buffer->name, file_buffer->addr);
  grub_dprintf("nfp", "Symbol: %s, Address: 0x%016"PRIxGRUB_UINT64_T"\n", file_control->name, file_control->addr);

  // Setup Sym Control
  base_addr = 0x2000000000 + file_control->addr;
  expansion_reg = (0x1 << 29) + (0x1 << 27) + (0x7 << 23) + ((base_addr >> 19) & 0x1FFFE0);
  nfp_cpp_bar_slice_setup(cpp, 0, 3, 0x7, base_addr, 0x1000000, (grub_uint32_t)expansion_reg);

  // Setup Sym Buffer
  base_addr = 0x2000000000 + file_buffer->addr;
  expansion_reg = (0x1 << 29) + (0x1 << 27) + (0x7 << 23) + ((base_addr >> 19) & 0x1FFFE0);
  nfp_cpp_bar_slice_setup(cpp, 0, 4, 0x7, base_addr, 0x1000000, (grub_uint32_t)expansion_reg);
}

static grub_err_t nfp6000_pci_dev_init(grub_pci_device_t dev)
{
  grub_int8_t is_64;
  grub_int32_t mem_type;
  grub_uint32_t base_low, base_high;
  grub_uint32_t size_low, size_high;
  grub_uint64_t base, size;
  grub_pci_address_t addr;

  addr = grub_pci_make_address(dev, GRUB_PCI_REG_ADDRESS_REG0);
  base_low = grub_pci_read(addr);
  grub_pci_write(addr, 0xffffffff);
  size_low = grub_pci_read(addr);
  grub_pci_write(addr, base_low);

  base = base_low & ~0xf;
  size = size_low & ~0xf;
  base_high = 0x0;
  size_high = 0xffffffff;
  is_64 = 0;
  mem_type = base_low & GRUB_PCI_ADDR_MEM_TYPE_MASK;

  if (mem_type == GRUB_PCI_ADDR_MEM_TYPE_64) {
    addr = grub_pci_make_address(dev, GRUB_PCI_REG_ADDRESS_REG1);
    base_high = grub_pci_read(addr);
    grub_pci_write(addr, 0xffffffff);
    size_high = grub_pci_read(addr);
    grub_pci_write(addr, base_high);
    is_64 = 1;
  }

  base = base | ((grub_uint64_t)base_high << 32);
  size = size | ((grub_uint64_t)size_high << 32);

  if ((!is_64 && size_low) || (is_64 && size))
    size = ~size + 1;

  g_cpp = grub_zalloc(sizeof(struct nfp_cpp));
  if (!g_cpp)
    return GRUB_ERR_OUT_OF_MEMORY;

  g_cpp->bar_base = base;
  g_cpp->bar_size = size;
  g_cpp->bar_base =(grub_addr_t)grub_pci_device_map_range(dev, base, size);

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write_word (addr, grub_pci_read_word (addr)
		    | GRUB_PCI_COMMAND_MEM_ENABLED | GRUB_PCI_COMMAND_BUS_MASTER);
  nfp_cpp_bar_slice_setup(g_cpp, 0, 1, 0xe, 0x0, 0x100000, 0x27000000);
  nfp_cpp_bar_slice_setup(g_cpp, 0, 2, 0x7, 0x8100000000, 0x1000000, 0x03838100);
  nfp_os_update_symbol_bar_set(g_cpp);

  return GRUB_ERR_NONE;
}

static struct grub_pcinet_card nfp6000 = {
  .name = "netronome",
  .vendor = 0x19ee,
  .device = 0x4000,
  .init = nfp6000_pci_dev_init,
  .open = grub_pcinet_card_fs_open,
  .read = grub_pcinet_card_fs_read,
  .close = grub_pcinet_card_fs_close,
};

GRUB_MOD_INIT(nfp6000)
{
  grub_pcinet_card_register(&nfp6000);
}

GRUB_MOD_FINI(nfp6000)
{
  grub_pcinet_card_unregister(&nfp6000);
}

