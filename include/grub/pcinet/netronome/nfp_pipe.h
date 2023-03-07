/*
 * Copyright (C) 2014,2017 Netronome Systems, Inc.
 * All right reserved.
 *
 */

#ifndef GRUB_PCINET_NFP_PIPE_HEADER
#define GRUB_PCINET_NFP_PIPE_HEADER 1

#include <grub/file.h>
#include <grub/pcinet/netronome/nfp_cpp.h>

/* These are the required symbols for os update to work */
#define OS_FILE_BUFFER "os_file_buffer"
#define OS_FILE_CONTROL "os_file_control"

/* The minimum buffer size is derived from the maximum flash sector size */
#define PIPE_MINIMUM_BUFFER_SIZE (4096u)
#define PIPE_OPERATION_INVALID (0u)

/* If the symbols are not available in the firmware */
#define OS_FILE_DEFAULT_CONTROL_ADDR (0x2000000u)
#define OS_FILE_DEFAULT_CONTROL_SIZE (0x400u)
#define OS_FILE_DEFAULT_BUFFER_ADDR  (0x3000000u)
#define OS_FILE_DEFAULT_BUFFER_SIZE  (0x400000u)
#define OS_FILE_DEFAULT_DOMAIN (24u)
#define OS_FILE_DEFAULT_TARGET (7u)

#define OS_FILE_PATH_MAX_LENGTH (256u)

#define PIPE_POLLING_DELAY_MS (10u)
/*
 * Meta structs go into the control block and can only be written
 * by the master. The data structs/types go into the buffer block
 * and may be written by either the master, or the worker.
 *
 * Typically, for raw block transfers from the master to the worker
 * no type is declared here as the user will simply use a pointer
 * to the block. In the same way, in the case where the master
 * request a single transaction request from the worker, no meta
 * block is needed, and only the structure used for interpreting the
 * reply in the buffer.
 */

#define PIPE_OPERATION_FILE_NAME (0x1u)
struct pipe_operation_file_name_meta {
	char file_path[OS_FILE_PATH_MAX_LENGTH];
};

#define PIPE_OPERATION_FILE_WRITE (0x2u)
struct pipe_operation_file_write_meta {
	grub_uint64_t write_size;
	grub_uint32_t transaction_size;
	grub_uint32_t transaction_count;
	grub_uint32_t transaction_total;
};

#define PIPE_OPERATION_FILE_READ  (0x3u)
struct pipe_operation_file_read_meta {
	grub_uint64_t read_size;
	grub_uint32_t transaction_size;
	grub_uint32_t transaction_count;
	grub_uint32_t transaction_total;
};

#define PIPE_OPERATION_FILE_INFO  (0x4u)
struct pipe_operation_file_info_data {
	grub_uint32_t file_valid;
	grub_uint64_t file_size;
};

#define PIPE_OPERATION_FILE_ERASE  (0x5u)
struct pipe_operation_file_erase_data {
	grub_uint32_t file_valid;
};

#define OS_FILE_OPERATION_MAX_META_SIZE (OS_FILE_PATH_MAX_LENGTH)

/*
 * The lower 8 bits of the 32-bit unsigned operation word is the operation opcode, while the upper
 * bits provide custom space for passing info to the worker
 */
#define PIPE_OPERATION_MASK(x) (0xFFu & (x))
#define PIPE_OPERATION_CUSTOM_MASK(y) (0xFFFFFF00u & (y))
#define PIPE_OPERATION_BUILD(x, y) (PIPE_OPERATION_MASK(x) | PIPE_OPERATION_CUSTOM_MASK(y))

#define SZ_CEIL64_TYPE(x) \
			(((sizeof(x) + sizeof(grub_uint64_t) - 1)) & ~(sizeof(grub_uint64_t) - 1))

#define SZ_CEIL64_VAL(x) \
			((((x) + sizeof(grub_uint64_t) - 1)) & ~(sizeof(grub_uint64_t) - 1))

struct nfp_pipe_worker;
struct nfp_pipe_master;

enum nfp_pipe_status {
	PIPE_STATE_UNAVAILABLE = 0, /* Not in a state to receive operations */
	PIPE_STATE_WAITING, /* Worker waiting for setup */
	PIPE_STATE_SETUP, /* Master preparing for operation */
	PIPE_STATE_PROCESSING /* Busy with an operation */
};

enum nfp_pipe_transaction_status {
	PIPE_TRANSACTION_STATUS_NONE = 0,
	PIPE_TRANSACTION_STATUS_START, /* Started processing transaction */
	PIPE_TRANSACTION_STATUS_END /* Completed processing transaction */
};

enum nfp_pipe_endpoint {
	PIPE_ENDPOINT_INVALID = 0, /* Invalid to detect unintialized uses */
	PIPE_ENDPOINT_MASTER, /* Endpoint that initiates an operation */
	PIPE_ENDPOINT_WORKER /* Endpoint that follows an operation request */
};

enum nfp_pipe_option_hash {
	PIPE_OPTION_HASH_OFF = 0, /* Hashing disabled */
	PIPE_OPTION_HASH_ON /* Hashing enabled */
};

struct nfp_pipe_cpp_buffer {
	grub_uint32_t cppid;
	grub_uint64_t addr;
	grub_uint64_t size;
	const char *name;
};

struct nfp_pipe {
	enum nfp_pipe_endpoint type;
	struct nfp_pipe_cpp_buffer os_buffer;
	struct nfp_pipe_cpp_buffer os_control;
	grub_uint8_t *nfp_pipe_buffer;
	struct nfp_pipe_worker *worker_control;
	struct nfp_pipe_master *master_control;
	grub_uint8_t *shared_control;
	struct nfp_cpp *cpp;
};

/**
 * Initialize a pipe endpoint.
 *
 * @param pipe			Handle
 * @param cpp			CPP Handle
 * @param type			Endpoint Type
 * @param os_update_buffer	Buffer NFP Area
 * @param os_update_control	Control NFP Area
 * @param meta_size		Meta block size
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_init(struct nfp_pipe *pipe, struct nfp_cpp *cpp, enum nfp_pipe_endpoint type,
			const struct nfp_pipe_cpp_buffer *os_update_buffer,
			const struct nfp_pipe_cpp_buffer *os_update_control, grub_int32_t meta_size);

/**
 * Free the pipe endpoint.
 *
 * @param pipe			Handle
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_exit(struct nfp_pipe *pipe);

/**
 * Perform a read of the control block from NFP memory
 *
 * @param pipe			Handle
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_control_read(struct nfp_pipe *pipe);

/**
 * Perform a write of the control block into NFP memory
 *
 * @param pipe			Handle
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_control_write(struct nfp_pipe *pipe);

/**
 * Perform a read of the data buffer from NFP memory
 *
 * @param pipe			Handle
 * @param bytes_written		Bytes to read from NFP memory into buffer
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_buffer_read(struct nfp_pipe *pipe, grub_int32_t bytes_written);

/**
 * Perform a write of the data buffer into NFP memory
 *
 * @param pipe			Handle
 * @param bytes_read		Bytes to write from buffer into NFP memory
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_buffer_write(struct nfp_pipe *pipe, grub_int32_t bytes_read);

/**
 * Worker status get
 *
 * @param pipe			Handle
 * @return			Worker Status
 */
enum nfp_pipe_status nfp_pipe_worker_status_get(struct nfp_pipe *pipe);

/**
 * Worker status set
 *
 * @param pipe			Handle
 * @param status		Worker Status
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_worker_status_set(struct nfp_pipe *pipe, enum nfp_pipe_status status);

/**
 * Master status get
 *
 * @param pipe			Handle
 * @return			Master Status
 */
enum nfp_pipe_status nfp_pipe_master_status_get(struct nfp_pipe *pipe);

/**
 * Master status set
 *
 * @param pipe			Handle
 * @param status		Master Status
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_master_status_set(struct nfp_pipe *pipe, enum nfp_pipe_status status);

/**
 * Worker transaction status get
 *
 * @param pipe			Handle
 * @return			Worker Transaction Status
 */
enum nfp_pipe_transaction_status nfp_pipe_worker_transaction_status_get(struct nfp_pipe *pipe);

/**
 * Worker transaction status set
 *
 * @param pipe			Handle
 * @param status		Worker Transaction Status
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_worker_transaction_status_set(struct nfp_pipe *pipe, enum nfp_pipe_transaction_status status);

/**
 * Master transaction status get
 *
 * @param pipe			Handle
 * @return			Master Transaction Status
 */
enum nfp_pipe_transaction_status nfp_pipe_master_transaction_status_get(struct nfp_pipe *pipe);

/**
 * Master transaction status set
 *
 * @param pipe			Handle
 * @param status		Master Transaction Status
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_master_transaction_status_set(struct nfp_pipe *pipe, enum nfp_pipe_transaction_status status);

/**
 * Master operation get
 *
 * @param pipe			Handle
 * @return			Operation
 */
grub_uint32_t nfp_pipe_operation_get(struct nfp_pipe *pipe);

/**
 * Master operation set
 *
 * @param pipe			Handle
 * @param operation		Operation
 * @param meta_owner		Owner
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_operation_set(struct nfp_pipe *pipe, grub_uint32_t operation, enum nfp_pipe_endpoint meta_owner);

/**
 * Master option hash get
 *
 * @param pipe			Handle
 * @return			Hash Enabled
 */
grub_uint32_t nfp_pipe_option_hash_get(struct nfp_pipe *pipe);

/**
 * Master option hash set
 *
 * @param pipe			Handle
 * @param hash			Hash Enable/Disable
 *
 * @return 0 on success
 */
grub_err_t nfp_pipe_option_hash_set(struct nfp_pipe *pipe, grub_uint32_t hash);

/**
 * Get a pointer to the meta mirror
 *
 * @param pipe			Handle
 * @return			Pointer to meta
 */
void *nfp_pipe_operation_meta(struct nfp_pipe *pipe);

/**
 * Get a pointer to the buffer mirror
 *
 * @param pipe			Handle
 * @return			Pointer to buffer
 */
void *nfp_pipe_operation_buffer(struct nfp_pipe *pipe);

/**
 * Debug information to assist with protocol debug
 *
 * @param pipe			Handle
 * @return 0 on success
 */
grub_err_t nfp_pipe_control_debug(struct nfp_pipe *pipe);

grub_err_t grub_pcinet_card_fs_open(struct grub_file *file, const char* file_name, grub_uint64_t timeout_ms);
grub_err_t grub_pcinet_card_fs_read(struct grub_file *file);
grub_err_t grub_pcinet_card_fs_close(struct grub_file *file);

#endif /* NFP_PIPE_H */

