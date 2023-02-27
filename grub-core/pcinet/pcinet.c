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
#include <grub/pci.h>
#include <grub/err.h>
#include <grub/net.h>
#include <grub/dl.h>
#include <grub/pcinet/pcinet.h>
#include <grub/command.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_pcinet_card *grub_pcinet_cards = NULL;

grub_net_t (*grub_pcinet_open) (const char *name) = NULL;


static grub_err_t pci_dev_init(grub_pci_device_t dev)
{
  grub_uint8_t header_type;
  grub_uint16_t vendor, device;
  struct grub_pcinet_card* pcicard;

  grub_pci_address_t addr;

  addr = grub_pci_make_address(dev, GRUB_PCI_REG_HEADER_TYPE);
  header_type = grub_pci_read_byte(addr);

  if (header_type == GRUB_PCI_HEADER_TYPE_CARDBUS) {
    grub_dprintf("pcinet", "CardBus doesn't support BARs\n");
    return GRUB_ERR_BAD_DEVICE;
  }
  addr = grub_pci_make_address(dev, GRUB_PCI_REG_VENDOR);
  vendor = grub_pci_read_word(addr);
  addr = grub_pci_make_address(dev, GRUB_PCI_REG_DEVICE);
  device = grub_pci_read_word(addr);

  FOR_PCINET_CARDS(pcicard)
  {
    if (pcicard->vendor != vendor || pcicard->device != device)
      continue;
    pcicard->init(dev);
  }

  return GRUB_ERR_NONE;
}

static grub_net_t
grub_pcinet_open_real (const char *name)
{
  const char *bus, *device, *function;
  grub_pci_device_t dev;

  if (grub_strncmp (name, "pci:", sizeof ("pci:") - 1) == 0)
  {
    bus = name + grub_strlen("pci:");
    dev.bus = grub_strtol(bus, &device, 10);
    if(device == NULL || *device != ':')
    {
      grub_error (GRUB_ERR_BAD_DEVICE, N_("pci device format is wrong"));
      return NULL;
    }

    dev.device = grub_strtol(device + 1, &function, 10);
    if(function == NULL || *function != '.')
    {
      grub_error (GRUB_ERR_BAD_DEVICE, N_("pci device format is wrong"));
      return NULL;
    }

    dev.function = grub_strtol(function + 1, NULL, 10);
  }
  else
  {
    grub_error (GRUB_ERR_BAD_DEVICE, N_("no pci device is specified"));
    return NULL;
  }
  pci_dev_init(dev);

  return NULL;
}

static grub_err_t
grub_pcinet_test (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("two arguments expected"));
  grub_pcinet_open_real(args[1]);

  return GRUB_ERR_NONE;

}

static grub_command_t cmd_pcinet_test;
GRUB_MOD_INIT(pcinet)
{
  cmd_pcinet_test = grub_register_command ("pcinet_test", grub_pcinet_test,
					N_("SHORTNAME PCIDEV"),
					N_("Test the pcidev."));
  grub_pcinet_open = grub_pcinet_open_real;
}

GRUB_MOD_FINI(pcinet)
{
  grub_pcinet_open = NULL;
}
