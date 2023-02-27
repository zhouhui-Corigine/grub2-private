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

#ifndef GRUB_PCINET_PCINET_HEADER
#define GRUB_PCINET_PCINET_HEADER 1

#include <grub/pci.h>

struct grub_pcinet_card
{
  struct grub_pcinet_card *next;
  struct grub_pcinet_card **prev;
  grub_uint16_t vendor;
  grub_uint16_t device;
  const char *name;
  grub_err_t (*init) (grub_pci_device_t dev);
};

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

#define FOR_PCINET_CARDS(var) for (var = grub_pcinet_cards; var; var = var->next)
#endif
