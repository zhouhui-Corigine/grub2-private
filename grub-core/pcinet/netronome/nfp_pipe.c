/*
 * Copyright (C) 2010-2011 Netronome Systems, Inc.  All rights reserved.
 *
 */
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/pcinet/netronome/nfp_cpp.h>
#include <grub/pcinet/netronome/nfp_nffw.h>
#include <grub/pcinet/netronome/nfp_pipe.h>

/*
 * Only the master may write to these fields, but the
 * worker may read them
 */
struct nfp_pipe_master {
  grub_uint32_t master_status; /* enum nfp_pipe_status */
  grub_uint32_t master_transaction_req; /* enum nfp_pipe_master_transaction */
  grub_uint32_t master_operation; /* User defined */
  grub_uint32_t master_operation_meta_size; /* Meta block size*/
  grub_uint32_t master_operation_meta_owner; /* Who may write to meta block */
  grub_uint32_t master_option_md5; /* Enable MD5 checksum used for sending and receiving */
};

/*
 * Only the worker may write to these fields, but the
 * master may read them
 */
struct nfp_pipe_worker {
  grub_uint32_t worker_status; /* enum nfp_pipe_status */
  grub_uint32_t worker_transaction_ack; /* enum nfp_pipe_worker_transaction */

};

grub_err_t nfp_pipe_init(struct nfp_pipe *pipe,
                         struct nfp_cpp *cpp,
                         enum nfp_pipe_endpoint type,
                         const struct nfp_pipe_cpp_buffer *os_update_buffer,
                         const struct nfp_pipe_cpp_buffer *os_update_control,
                         grub_int32_t meta_size)
{
  grub_err_t result = 0;
  grub_uint64_t buffer_size;

  if (!cpp || !pipe || !os_update_buffer || !os_update_control)
    return GRUB_ERR_BAD_ARGUMENT;

  if (pipe->type != PIPE_ENDPOINT_INVALID || type == PIPE_ENDPOINT_INVALID)
    return GRUB_ERR_BAD_ARGUMENT;

  pipe->type = type;
  pipe->cpp = cpp;
  pipe->os_buffer = *os_update_buffer;
  pipe->os_control = *os_update_control;

  if ((SZ_CEIL64_TYPE(struct nfp_pipe_worker))
      + (SZ_CEIL64_TYPE(struct nfp_pipe_master))
      + (SZ_CEIL64_VAL(meta_size)) > pipe->os_control.size)
  {
    grub_dprintf("nfp", "The size of the control block is too small:%"PRIuGRUB_UINT64_T"!\n", pipe->os_control.size);
    return GRUB_ERR_OUT_OF_MEMORY;
  }
  /* Check minimum buffer size */
  if (pipe->os_buffer.size < PIPE_MINIMUM_BUFFER_SIZE)
  {
    grub_dprintf("nfp", "The size of the buffer block is too small:%"PRIuGRUB_UINT64_T"!\n", pipe->os_buffer.size);
    return GRUB_ERR_OUT_OF_MEMORY;
  }

  buffer_size = (SZ_CEIL64_VAL(pipe->os_buffer.size)) & ~((grub_uint64_t)PIPE_MINIMUM_BUFFER_SIZE - 1ULL);
  pipe->nfp_pipe_buffer = grub_malloc(buffer_size);
  if (!pipe->nfp_pipe_buffer)
    return GRUB_ERR_OUT_OF_MEMORY;

  /* Mirror memory spaces for the firmware blocks accessed over CPP*/
  pipe->worker_control = grub_zalloc(SZ_CEIL64_TYPE(struct nfp_pipe_worker));
  pipe->master_control = grub_zalloc(SZ_CEIL64_TYPE(struct nfp_pipe_master));
  pipe->shared_control = grub_zalloc(SZ_CEIL64_VAL(meta_size));
  if (!pipe->worker_control || !pipe->master_control || !pipe->shared_control)
  {
    result = GRUB_ERR_OUT_OF_MEMORY;
    goto memory_free;
  }

  pipe->master_control->master_operation_meta_size = SZ_CEIL64_VAL(meta_size);
  pipe->master_control->master_operation_meta_owner = PIPE_ENDPOINT_INVALID;

  return GRUB_ERR_NONE;

memory_free:
  if (pipe->nfp_pipe_buffer)
    grub_free(pipe->nfp_pipe_buffer);
  if (pipe->worker_control)
    grub_free(pipe->worker_control);
  if (pipe->master_control)
    grub_free(pipe->master_control);
  if (pipe->shared_control)
    grub_free(pipe->shared_control);
  return result;
}

grub_err_t nfp_pipe_exit(struct nfp_pipe *pipe)
{
  /* Free mirror memory spaces for the firmware blocks accessed over CPP*/
  if (pipe->type == PIPE_ENDPOINT_INVALID)
    return GRUB_ERR_BUG;

  if (pipe->worker_control)
    grub_free(pipe->worker_control);

  if (pipe->master_control)
    grub_free(pipe->master_control);

  if (pipe->shared_control)
    grub_free(pipe->shared_control);

  if (pipe->nfp_pipe_buffer)
    grub_free(pipe->nfp_pipe_buffer);

  return GRUB_ERR_NONE;
}

grub_err_t nfp_pipe_control_read(struct nfp_pipe *pipe)
{
  grub_uint32_t actual_read;

  /*
   * The control block is broken up into separate structures so we need to read
   * them individually from the firmware control block, where they are located
   * sequentially.
   *
   * NOTE: The meta section must be read last to ensure the observer can assume
   * meta data is completely updated once a change is observed in the worker or master
   * status.
   *
   * Layout in firmware memory (emem):
   *   master_control
   *   worker_control
   *   shared_control
   */

  grub_uint32_t id = pipe->os_control.cppid;
  grub_uint32_t read_offset = 0;
  grub_uint32_t read_size = SZ_CEIL64_TYPE(struct nfp_pipe_master);
  if (pipe->type == PIPE_ENDPOINT_WORKER) {
    actual_read = nfp_cpp_read(pipe->cpp, id, pipe->os_control.addr + read_offset,
                               pipe->master_control, read_size);
    if (actual_read != read_size) {
      grub_dprintf("nfp", "Failed to read the control blok for worker!\n");
      return GRUB_ERR_IO;
    }
  }

  read_offset += read_size;
  read_size = SZ_CEIL64_TYPE(struct nfp_pipe_worker);
  if (pipe->type == PIPE_ENDPOINT_MASTER) {
    actual_read = nfp_cpp_read(pipe->cpp, id, pipe->os_control.addr + read_offset,
                               pipe->worker_control, read_size);
    if (actual_read != read_size) {
      grub_dprintf("nfp", "Failed to read the control blok for master!\n");
      return GRUB_ERR_IO;
    }
  }

  read_offset += read_size;
  read_size = SZ_CEIL64_VAL(pipe->master_control->master_operation_meta_size);

  if ((pipe->master_control->master_operation_meta_owner != pipe->type)
      && (pipe->master_control->master_operation_meta_owner != PIPE_ENDPOINT_INVALID)) {
    actual_read = nfp_cpp_read(pipe->cpp, id, pipe->os_control.addr + read_offset,
                               pipe->shared_control, read_size);
    if (actual_read != read_size)
    {
      grub_dprintf("nfp", "Failed to read the mete block!\n");
      return GRUB_ERR_IO;
    }
  }

  return GRUB_ERR_NONE;
}

grub_err_t nfp_pipe_control_write(struct nfp_pipe *pipe)
{
  grub_uint32_t actual_write;
  /*
   * The control block is broken up into separate structures so we need to write
   * them individually from the firmware control block, where they are located
   * sequentially.
   *
   * NOTE: The meta section must be written first to ensure the observer can assume
   * meta data is completely updated once a change is observed in the worker or master
   * status.
   *
   * Layout in firmware memory (emem):
   *   master_control
   *   worker_control
   *   shared_control
   */
  grub_uint32_t write_offset = (SZ_CEIL64_TYPE(struct nfp_pipe_worker))
                                + (SZ_CEIL64_TYPE(struct nfp_pipe_master));
  grub_uint32_t id = pipe->os_control.cppid;
  grub_uint32_t write_size = SZ_CEIL64_VAL(pipe->master_control->master_operation_meta_size);

  grub_dprintf("nfp", "WRITE:: Meta Size: %u, Meta Owner: %u, Pipe Type: %u\n",
               write_size, pipe->master_control->master_operation_meta_owner, pipe->type);

  if (pipe->master_control->master_operation_meta_owner == pipe->type) {
    actual_write = nfp_cpp_write(pipe->cpp, id, pipe->os_control.addr + write_offset,
                                 pipe->shared_control, write_size);
    if (actual_write != write_size) {
      if (pipe->type == PIPE_ENDPOINT_WORKER)
        grub_dprintf("nfp", "Work failed to write meta block!\n");
      else
        grub_dprintf("nfp", "Master failed to write meta block!\n");
      return GRUB_ERR_IO;
    }
  }

  write_size = SZ_CEIL64_TYPE(struct nfp_pipe_worker);
  write_offset -= write_size;
  if (pipe->type == PIPE_ENDPOINT_WORKER) {
    actual_write = nfp_cpp_write(pipe->cpp, id, pipe->os_control.addr + write_offset,
                                 pipe->worker_control, write_size);
    if (actual_write != write_size) {
      grub_dprintf("nfp", "Work failed to write control block!\n");
      return GRUB_ERR_IO;
    }
  }

  write_size = SZ_CEIL64_TYPE(struct nfp_pipe_master);
  write_offset -= write_size;
  if (pipe->type == PIPE_ENDPOINT_MASTER) {
    actual_write = nfp_cpp_write(pipe->cpp, id, pipe->os_control.addr + write_offset,
                                 pipe->master_control, write_size);
    if (actual_write != write_size) {
      grub_dprintf("nfp", "Master failed to write control block!\n");
      return GRUB_ERR_IO;
    }
  }

  return GRUB_ERR_NONE;
}

grub_err_t nfp_pipe_buffer_read(struct nfp_pipe *pipe, grub_int32_t bytes_written)
{
  grub_uint32_t actual_read;
  grub_uint32_t read_offset = 0;
  grub_uint32_t read_size = SZ_CEIL64_VAL(bytes_written);
  grub_uint32_t id = pipe->os_buffer.cppid;

  actual_read = nfp_cpp_read(pipe->cpp, id, pipe->os_buffer.addr + read_offset,
                             pipe->nfp_pipe_buffer, read_size);
  if (actual_read != read_size) {
    grub_dprintf("nfp", "Failed to read buffer block!\n");
    return GRUB_ERR_IO;
  }
  return GRUB_ERR_NONE;
}

grub_err_t nfp_pipe_buffer_write(struct nfp_pipe *pipe, grub_int32_t bytes_read)
{
  grub_uint32_t actual_write;

  grub_uint32_t write_offset = 0;
  grub_uint32_t write_size = SZ_CEIL64_VAL(bytes_read);
  grub_uint32_t id = pipe->os_buffer.cppid;

  actual_write = nfp_cpp_write(pipe->cpp, id, pipe->os_buffer.addr + write_offset,
                               pipe->nfp_pipe_buffer, write_size);

  if (actual_write != write_size) {
    grub_dprintf("nfp", "Failed to write buffer block!\n");
    return GRUB_ERR_IO;
  }

  return GRUB_ERR_NONE;
}

enum nfp_pipe_status nfp_pipe_worker_status_get(struct nfp_pipe *pipe)
{
  enum nfp_pipe_status status = PIPE_STATE_UNAVAILABLE;

  if (pipe->type == PIPE_ENDPOINT_MASTER)
    status = pipe->worker_control->worker_status;

  return status;
}

grub_err_t nfp_pipe_worker_status_set(struct nfp_pipe *pipe, enum nfp_pipe_status status)
{
  if (pipe->type == PIPE_ENDPOINT_WORKER)
    pipe->worker_control->worker_status = status;

  return GRUB_ERR_NONE;
}

enum nfp_pipe_status nfp_pipe_master_status_get(struct nfp_pipe *pipe)
{
  enum nfp_pipe_status status = PIPE_STATE_UNAVAILABLE;

  if (pipe->type == PIPE_ENDPOINT_WORKER)
    status = pipe->master_control->master_status;

  return status;
}

grub_err_t nfp_pipe_master_status_set(struct nfp_pipe *pipe, enum nfp_pipe_status status)
{
  if (pipe->type == PIPE_ENDPOINT_MASTER)
    pipe->master_control->master_status = status;

  return GRUB_ERR_NONE;
}

enum nfp_pipe_transaction_status nfp_pipe_worker_transaction_status_get(struct nfp_pipe *pipe)
{
  enum nfp_pipe_transaction_status status = PIPE_TRANSACTION_STATUS_NONE;

  if (pipe->type == PIPE_ENDPOINT_MASTER)
    status = pipe->worker_control->worker_transaction_ack;

  return status;
}

grub_err_t nfp_pipe_worker_transaction_status_set(struct nfp_pipe *pipe, enum nfp_pipe_transaction_status status)
{
  if (pipe->type == PIPE_ENDPOINT_WORKER)
    pipe->worker_control->worker_transaction_ack = status;

  return GRUB_ERR_NONE;
}

enum nfp_pipe_transaction_status nfp_pipe_master_transaction_status_get(struct nfp_pipe *pipe)
{
  enum nfp_pipe_transaction_status status = PIPE_TRANSACTION_STATUS_NONE;

  if (pipe->type == PIPE_ENDPOINT_WORKER)
    status = pipe->master_control->master_transaction_req;

  return status;
}

grub_err_t nfp_pipe_master_transaction_status_set(struct nfp_pipe *pipe, enum nfp_pipe_transaction_status status)
{
  if (pipe->type == PIPE_ENDPOINT_MASTER)
    pipe->master_control->master_transaction_req = status;

  return GRUB_ERR_NONE;
}

grub_uint32_t nfp_pipe_operation_get(struct nfp_pipe *pipe)
{
  grub_uint32_t operation = PIPE_OPERATION_INVALID;

  if (pipe->type == PIPE_ENDPOINT_WORKER)
    operation = pipe->master_control->master_operation;

  return operation;
}

grub_err_t nfp_pipe_operation_set(struct nfp_pipe *pipe, grub_uint32_t operation, enum nfp_pipe_endpoint meta_owner)
{
  if ((operation != PIPE_OPERATION_INVALID)
      && (pipe->type == PIPE_ENDPOINT_MASTER)) {
    pipe->master_control->master_operation = operation;
    pipe->master_control->master_operation_meta_owner = meta_owner;
  }

  return GRUB_ERR_NONE;
}

grub_uint32_t nfp_pipe_option_hash_get(struct nfp_pipe *pipe)
{
  grub_uint32_t hash = PIPE_OPTION_HASH_OFF;

  if (pipe->type == PIPE_ENDPOINT_WORKER)
    hash = pipe->master_control->master_option_md5;

  return hash;
}

grub_err_t nfp_pipe_option_hash_set(struct nfp_pipe *pipe, grub_uint32_t hash)
{
  if (pipe->type == PIPE_ENDPOINT_MASTER)
    pipe->master_control->master_option_md5 = hash;

  return GRUB_ERR_NONE;
}

void *nfp_pipe_operation_meta(struct nfp_pipe *pipe)
{
  return pipe->shared_control;
}

void *nfp_pipe_operation_buffer(struct nfp_pipe *pipe)
{
  return pipe->nfp_pipe_buffer;
}

