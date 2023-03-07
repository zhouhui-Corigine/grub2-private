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
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/dl.h>
#include <grub/pcinet.h>
#include <grub/command.h>
#include <grub/file.h>
#include <grub/list.h>
#include <grub/kernel.h>
#include <grub/pcinet/netronome/nfp_pipe.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_pcinet_card *grub_pcinet_cards = NULL;

static grub_err_t
grub_pcinet_fs_dir (grub_device_t device, const char *path __attribute__ ((unused)),
		 grub_fs_dir_hook_t hook __attribute__ ((unused)),
		 void *hook_data __attribute__ ((unused)))
{
  if (!device->pcinet)
    return grub_error (GRUB_ERR_BUG, "invalid net device");
  return GRUB_ERR_NONE;
}

grub_err_t grub_pcinet_fs_open (struct grub_file *file, const char *name)
{
  grub_err_t err;

  file->device->pcinet->packs.first = NULL;
  file->device->pcinet->packs.last = NULL;
  file->device->pcinet->offset = 0;
  file->device->pcinet->eof = 0;
  file->device->pcinet->stall = 0;
  file->device->pcinet->name = grub_strdup(name);
  if (!file->device->pcinet->name){
      return grub_errno;
  }

  if (!file->device->pcinet->dev->open)
    return GRUB_ERR_BAD_DEVICE;

  err = file->device->pcinet->dev->open(file, name, 5000);
  if(err) {
    while (file->device->pcinet->packs.first)
	  {
	    grub_netbuff_free (file->device->pcinet->packs.first->nb);
	    grub_net_remove_packet (file->device->pcinet->packs.first);
	  }
    grub_free (file->device->pcinet->name);
    return err;
  }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_pcinet_fs_close (grub_file_t file)
{
  if (file->device->pcinet->dev->close)
    file->device->pcinet->dev->close(file);

  while (file->device->pcinet->packs.first)
    {
      grub_netbuff_free (file->device->pcinet->packs.first->nb);
      grub_net_remove_packet (file->device->pcinet->packs.first);
    }
  grub_free (file->device->pcinet->name);
  return GRUB_ERR_NONE;
}

/*  Read from the packets list*/
static grub_ssize_t
grub_pcinet_fs_read_real (grub_file_t file, char *buf, grub_size_t len)
{
  grub_pcinet_t net = file->device->pcinet;
  struct grub_net_buff *nb;
  char *ptr = buf;
  grub_size_t amount, total = 0;

  while (net->packs.first)
	{
	  nb = net->packs.first->nb;
	  amount = nb->tail - nb->data;
	  if (amount > len)
	    amount = len;
	  len -= amount;
	  total += amount;
	  file->device->pcinet->offset += amount;
	  if (grub_file_progress_hook)
	    grub_file_progress_hook (0, 0, amount, file);
	  if (buf)
	    {
	      grub_memcpy (ptr, nb->data, amount);
	      ptr += amount;
	    }
	  if (amount == (grub_size_t) (nb->tail - nb->data))
	  {
      grub_netbuff_free (nb);
	    grub_net_remove_packet (net->packs.first);
	  }
	  else
	    nb->data += amount;

    if(!file->device->pcinet->eof && net->dev->read)
      net->dev->read(file);

	  if (!len)
    {
       return total;
    }
	}
	return total;
}

static grub_off_t
have_ahead (struct grub_file *file)
{
  grub_pcinet_t net = file->device->pcinet;
  grub_off_t ret = net->offset;
  struct grub_net_packet *pack;
  for (pack = net->packs.first; pack; pack = pack->next)
    ret += pack->nb->tail - pack->nb->data;
  return ret;
}

static grub_err_t
grub_pcinet_seek_real (struct grub_file *file, grub_off_t offset)
{
  grub_err_t err;

  if (offset == file->device->pcinet->offset)
    return GRUB_ERR_NONE;

  if (offset > file->device->pcinet->offset)
  {
    if (have_ahead(file) < offset)
      return GRUB_ERR_BUG;
	  grub_pcinet_fs_read_real (file, NULL, offset - file->device->pcinet->offset);
    return grub_errno;
  }
  else
  {
    if (offset < OS_FILE_DEFAULT_BUFFER_SIZE)
      file->device->pcinet->packs.first->nb->data = file->device->pcinet->packs.first->nb->head;
    else {
      const char *file_name;

      while (file->device->pcinet->packs.first)
      {
        grub_netbuff_free (file->device->pcinet->packs.first->nb);
        grub_net_remove_packet (file->device->pcinet->packs.first);
      }
      file->device->pcinet->dev->close(file);
      file->device->pcinet->packs.first = NULL;
      file->device->pcinet->packs.last = NULL;
      file->device->pcinet->eof = 0;
      file_name = (file->name[0] == '(') ? grub_strchr (file->name, ')') : NULL;
      if (file_name)
        file_name++;
      err = file->device->pcinet->dev->open(file, file_name, 15000);
      if (err)
        return err;
    }
    file->device->pcinet->offset = 0;
    grub_pcinet_fs_read_real (file, NULL, offset);
    return grub_errno;
  }
}

static grub_ssize_t
grub_pcinet_fs_read (grub_file_t file, char *buf, grub_size_t len)
{
  if (file->offset != file->device->pcinet->offset)
  {
    grub_err_t err;
    err = grub_pcinet_seek_real (file, file->offset);
    if (err)
	    return err;
  }

  return grub_pcinet_fs_read_real (file, buf, len);
}

static struct grub_fs grub_pcinet_fs =
  {
    .name = "pcinet",
    .fs_dir = grub_pcinet_fs_dir,
    .fs_open = grub_pcinet_fs_open,
    .fs_read = grub_pcinet_fs_read,
    .fs_close = grub_pcinet_fs_close,
    .fs_label = NULL,
    .fs_uuid = NULL,
    .fs_mtime = NULL,
  };

static struct grub_pcinet_card* pci_dev_get_and_init(grub_pci_device_t dev)
{
  grub_uint8_t header_type;
  grub_uint16_t vendor, device;
  struct grub_pcinet_card* pcicard;
  grub_pci_address_t addr;

  addr = grub_pci_make_address(dev, GRUB_PCI_REG_HEADER_TYPE);
  header_type = grub_pci_read_byte(addr);

  if (header_type == GRUB_PCI_HEADER_TYPE_CARDBUS) {
    grub_dprintf("pcinet", "CardBus doesn't support BARs\n");
    return NULL;
  }
  addr = grub_pci_make_address(dev, GRUB_PCI_REG_VENDOR);
  vendor = grub_pci_read_word(addr);
  addr = grub_pci_make_address(dev, GRUB_PCI_REG_DEVICE);
  device = grub_pci_read_word(addr);

  FOR_PCINET_CARDS(pcicard)
  {
    if (pcicard->vendor != vendor || pcicard->device != device)
      continue;

    if (!pcicard->inited && pcicard->init)
    {
      pcicard->init(dev);
      pcicard->inited = 1;
    }
    break;
  }

  return pcicard;
}

static grub_pcinet_t grub_pcinet_open_real (const char *name)
{
  grub_pcinet_t ret;
  grub_pci_device_t dev;
  struct grub_pcinet_card* pcinet_card;
  const char *bus, *device, *function;

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
  pcinet_card = pci_dev_get_and_init(dev);
  if(!pcinet_card)
    return NULL;
  ret = grub_zalloc (sizeof (*ret));
  if (!ret)
    return NULL;
  ret->dev = pcinet_card;
  ret->fs = &grub_pcinet_fs;

  return ret;
}

GRUB_MOD_INIT(pcinet)
{
  grub_pcinet_open = grub_pcinet_open_real;
}

GRUB_MOD_FINI(pcinet)
{
  grub_pcinet_open = NULL;
}
