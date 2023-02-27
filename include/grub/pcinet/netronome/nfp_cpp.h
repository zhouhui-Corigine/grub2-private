/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_cpp.h
 * Interface for low-level NFP CPP access.
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */
#ifndef GRUB_PCINET_NFP_CPP_HEADER
#define GRUB_PCINET_NFP_CPP_HEADER 1

#define PCI_64BIT_BAR_COUNT             3

#define NFP_CPP_NUM_TARGETS             16
/* Max size of area it should be safe to request */
#define NFP_CPP_SAFE_AREA_SIZE		SZ_2M

/* NFP_MUTEX_WAIT_* are timeouts in seconds when waiting for a mutex */
#define NFP_MUTEX_WAIT_FIRST_WARN	15
#define NFP_MUTEX_WAIT_NEXT_WARN	5
#define NFP_MUTEX_WAIT_ERROR		60

struct nfp_cpp;

/* Wildcard indicating a CPP read or write action
 *
 * The action used will be either read or write depending on whether a
 * read or write instruction/call is performed on the NFP_CPP_ID.  It
 * is recomended that the RW action is used even if all actions to be
 * performed on a NFP_CPP_ID are known to be only reads or writes.
 * Doing so will in many cases save NFP CPP internal software
 * resources.
 */
#define NFP_CPP_ACTION_RW               32

#define NFP_CPP_TARGET_ID_MASK          0x1f

#define NFP_CPP_ATOMIC_RD(target, island) \
  NFP_CPP_ISLAND_ID((target), 3, 0, (island))
#define NFP_CPP_ATOMIC_WR(target, island) \
  NFP_CPP_ISLAND_ID((target), 4, 0, (island))

/**
 * NFP_CPP_ID() - pack target, token, and action into a CPP ID.
 * @target:     NFP CPP target id
 * @action:     NFP CPP action id
 * @token:      NFP CPP token id
 *
 * Create a 32-bit CPP identifier representing the access to be made.
 * These identifiers are used as parameters to other NFP CPP
 * functions.  Some CPP devices may allow wildcard identifiers to be
 * specified.
 *
 * Return:      NFP CPP ID
 */
#define NFP_CPP_ID(target, action, token)			 \
  ((((target) & 0x7f) << 24) | (((token)  & 0xff) << 16) | \
   (((action) & 0xff) <<  8))

/**
 * NFP_CPP_ISLAND_ID() - pack target, token, action, and island into a CPP ID.
 * @target:     NFP CPP target id
 * @action:     NFP CPP action id
 * @token:      NFP CPP token id
 * @island:     NFP CPP island id
 *
 * Create a 32-bit CPP identifier representing the access to be made.
 * These identifiers are used as parameters to other NFP CPP
 * functions.  Some CPP devices may allow wildcard identifiers to be
 * specified.
 *
 * Return:      NFP CPP ID
 */
#define NFP_CPP_ISLAND_ID(target, action, token, island)	 \
  ((((target) & 0x7f) << 24) | (((token)  & 0xff) << 16) | \
   (((action) & 0xff) <<  8) | (((island) & 0xff) << 0))

/**
 * NFP_CPP_ID_TARGET_of() - Return the NFP CPP target of a NFP CPP ID
 * @id:         NFP CPP ID
 *
 * Return:      NFP CPP target
 */
static inline grub_uint8_t NFP_CPP_ID_TARGET_of(grub_uint32_t id)
{
  return (id >> 24) & NFP_CPP_TARGET_ID_MASK;
}

/**
 * NFP_CPP_ID_TOKEN_of() - Return the NFP CPP token of a NFP CPP ID
 * @id:         NFP CPP ID
 * Return:      NFP CPP token
 */
static inline grub_uint8_t NFP_CPP_ID_TOKEN_of(grub_uint32_t id)
{
  return (id >> 16) & 0xff;
}

/**
 * NFP_CPP_ID_ACTION_of() - Return the NFP CPP action of a NFP CPP ID
 * @id:         NFP CPP ID
 *
 * Return:      NFP CPP action
 */
static inline grub_uint8_t NFP_CPP_ID_ACTION_of(grub_uint32_t id)
{
  return (id >> 8) & 0xff;
}

/**
 * NFP_CPP_ID_ISLAND_of() - Return the NFP CPP island of a NFP CPP ID
 * @id: NFP CPP ID
 *
 * Return:      NFP CPP island
 */
static inline grub_uint8_t NFP_CPP_ID_ISLAND_of(grub_uint32_t id)
{
  return (id >> 0) & 0xff;
}

/* NFP Interface types - logical interface for this CPP connection
 * 4 bits are reserved for interface type.
 */
#define NFP_CPP_INTERFACE_TYPE_INVALID      0x0
#define NFP_CPP_INTERFACE_TYPE_PCI          0x1
#define NFP_CPP_INTERFACE_TYPE_ARM          0x2
#define NFP_CPP_INTERFACE_TYPE_RPC          0x3
#define NFP_CPP_INTERFACE_TYPE_ILA          0x4

/**
 * NFP_CPP_INTERFACE() - Construct a 16-bit NFP Interface ID
 * @type:       NFP Interface Type
 * @unit:       Unit identifier for the interface type
 * @channel:    Channel identifier for the interface unit
 *
 * Interface IDs consists of 4 bits of interface type,
 * 4 bits of unit identifier, and 8 bits of channel identifier.
 *
 * The NFP Interface ID is used in the implementation of
 * NFP CPP API mutexes, which use the MU Atomic CompareAndWrite
 * operation - hence the limit to 16 bits to be able to
 * use the NFP Interface ID as a lock owner.
 *
 * Return:      Interface ID
 */
#define NFP_CPP_INTERFACE(type, unit, channel)	\
  ((((type) & 0xf) << 12) |		\
   (((unit) & 0xf) <<  8) |		\
   (((channel) & 0xff) << 0))

/**
 * NFP_CPP_INTERFACE_TYPE_of() - Get the interface type
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's type
 */
#define NFP_CPP_INTERFACE_TYPE_of(interface)   (((interface) >> 12) & 0xf)

/**
 * NFP_CPP_INTERFACE_UNIT_of() - Get the interface unit
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's unit
 */
#define NFP_CPP_INTERFACE_UNIT_of(interface)   (((interface) >>  8) & 0xf)

/**
 * NFP_CPP_INTERFACE_CHANNEL_of() - Get the interface channel
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's channel
 */
#define NFP_CPP_INTERFACE_CHANNEL_of(interface)   (((interface) >>  0) & 0xff)

/* Implemented in nfp_cpplib.c */
grub_uint32_t nfp_cpp_read(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
                 grub_uint64_t address, void *kernel_vaddr, grub_size_t length);
grub_uint32_t nfp_cpp_write(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
                  grub_uint64_t address, const void *kernel_vaddr,
                  grub_size_t length);
grub_int32_t nfp_cpp_readl(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
      grub_uint64_t address, grub_uint32_t *value);
grub_int32_t nfp_cpp_writel(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
       grub_uint64_t address, grub_uint32_t value);
grub_int32_t nfp_cpp_readq(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
      grub_uint64_t address, grub_uint64_t *value);
grub_int32_t nfp_cpp_writeq(struct nfp_cpp *cpp, grub_uint32_t cpp_id,
       grub_uint64_t address, grub_uint64_t value);


grub_int32_t nfp_cpp_mutex_init(struct nfp_cpp *cpp, grub_int32_t target,
           grub_uint64_t address, grub_uint32_t key_id);
struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, grub_int32_t target,
            grub_uint64_t address,
            grub_uint32_t key_id);
void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex);
grub_int32_t nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex);
grub_int32_t nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex);
grub_int32_t nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex);
grub_int32_t nfp_cpp_mutex_reclaim(struct nfp_cpp *cpp, grub_int32_t target,
        grub_uint64_t address);
#endif
