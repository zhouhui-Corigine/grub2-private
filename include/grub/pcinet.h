/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011  Free Software Foundation, Inc.
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

#ifndef GRUB_PCINET_HEADER
#define GRUB_PCINET_HEADER 1

#include <grub/pci.h>
#include <grub/net.h>
#include <grub/fs.h>

struct grub_pcinet_card
{
  struct grub_pcinet_card *next;
  struct grub_pcinet_card **prev;
  grub_uint32_t inited;
  grub_uint16_t vendor;
  grub_uint16_t device;
  const char *name;
  grub_err_t (*init) (grub_pci_device_t dev);
  grub_err_t (*open) (struct grub_file *file, const char* name, grub_uint64_t timeout_ms);
  grub_err_t (*read) (struct grub_file *file);
  grub_err_t (*close) (struct grub_file *file);
};

typedef struct grub_pcinet
{
  char *name;
  struct grub_pcinet_card* dev;
  grub_net_packets_t packs;
  grub_off_t offset;
  grub_fs_t fs;
  int eof;
  int stall;
} *grub_pcinet_t;

extern grub_pcinet_t (*EXPORT_VAR(grub_pcinet_open)) (const char *name);
extern struct grub_pcinet_card *grub_pcinet_cards;

static inline void
grub_pcinet_card_register (struct grub_pcinet_card *card)
{
  grub_list_push (GRUB_AS_LIST_P (&grub_pcinet_cards),
                  GRUB_AS_LIST (card));
}

static inline void grub_pcinet_card_unregister (struct grub_pcinet_card *card)
{
  grub_list_remove (GRUB_AS_LIST (card));
}

grub_err_t grub_pcinet_fs_open(struct grub_file *file, const char *file_name);

#define FOR_PCINET_CARDS(var) for (var = grub_pcinet_cards; var; var = var->next)
#endif
