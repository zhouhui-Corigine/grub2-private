#include <grub/file.h>
#include <grub/time.h>
#include <grub/net/netbuff.h>
#include <grub/net.h>
#include <grub/pcinet.h>
#include <grub/pcinet/netronome/nfp_nffw.h>
#include <grub/pcinet/netronome/nfp_cpp.h>
#include <grub/pcinet/netronome/nfp_pipe.h>


#define PIPE_STATE_TIMEOUT_SECONDS_WARNING (2u)
#define PIPE_STATE_TIMEOUT_SECONDS (10u + PIPE_STATE_TIMEOUT_SECONDS_WARNING)

#define PIPE_POLLING_DELAY_MS (10u)
#define PIPE_POLLING_DELAY_MS_MAX (PIPE_STATE_TIMEOUT_SECONDS * 1000)
#define PIPE_POLLING_IDLE_UPPER (10u)
#define PIPE_POLLING_IDLE_LOWER (8u)
#define FILEOP_NONE  (0u)
#define FILEOP_PATH  (1u)
#define FILEOP_INFO  (2u)
#define FILEOP_READ  (4u)
#define FILEOP_WRITE (8u)
#define FILEOP_ERASE (16u)

enum state_machine_states {
  STATE_INIT = 0, STATE_OPERATION_START, STATE_TRANSACTION_START, STATE_TRANSACTION_END, STATE_OPERATION_END, STATE_EXIT
};

struct state_machine_ctrl {
  struct nfp_pipe *pipe;
  struct grub_file *file;
  grub_uint64_t timer;
  grub_uint64_t elapse_ms;
  grub_uint32_t file_op;
  grub_uint32_t file_op_current;
  grub_uint32_t file_chained_operation;
  grub_uint32_t busy;
  grub_uint32_t timeout_warning_once;
  grub_uint32_t poll_delay_ms;
  grub_uint32_t poll_idle_count;
  enum state_machine_states state, prev_state;
  char file_name[OS_FILE_PATH_MAX_LENGTH];
};

extern struct nfp_pipe_cpp_buffer *file_buffer;
extern struct nfp_pipe_cpp_buffer *file_control;
extern struct nfp_cpp *g_cpp;

static grub_int32_t operation_init(struct state_machine_ctrl *s)
{
  grub_int32_t result = 0;

  result = nfp_pipe_master_transaction_status_set(s->pipe, PIPE_TRANSACTION_STATUS_NONE);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  if (!result)
    result = nfp_pipe_master_status_set(s->pipe, PIPE_STATE_SETUP);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  return result;
}

static void operation_next(struct state_machine_ctrl *s)
{
  /* Select the next operation in logical order */
  s->file_op_current = FILEOP_NONE;

  if ((s->file_op & FILEOP_PATH) && (s->file_op_current == FILEOP_NONE)) {
    s->file_op_current = FILEOP_PATH;
    s->file_op = s->file_op & ~(FILEOP_PATH);
  }

  if ((s->file_op & FILEOP_INFO) && (s->file_op_current == FILEOP_NONE)) {
    s->file_op_current = FILEOP_INFO;
    s->file_op = s->file_op & ~(FILEOP_INFO);
  }

  if ((s->file_op & FILEOP_ERASE) && (s->file_op_current == FILEOP_NONE)) {
    s->file_op_current = FILEOP_ERASE;
    s->file_op = s->file_op & ~(FILEOP_ERASE);
  }

  if ((s->file_op & FILEOP_WRITE) && (s->file_op_current == FILEOP_NONE)) {
    s->file_op_current = FILEOP_WRITE;
    s->file_op = s->file_op & ~(FILEOP_WRITE);
  }

  if ((s->file_op & FILEOP_READ) && (s->file_op_current == FILEOP_NONE)) {
    s->file_op_current = FILEOP_READ;
    s->file_op = s->file_op & ~(FILEOP_READ);
  }
}

static grub_int32_t operation_start(struct state_machine_ctrl *s)
{
  grub_int32_t result = 0;

  switch (s->file_op_current) {
  case FILEOP_PATH:
    result = nfp_pipe_operation_set(s->pipe, PIPE_OPERATION_FILE_NAME, PIPE_ENDPOINT_MASTER);
    if (!result) {
      struct pipe_operation_file_name_meta *meta;

      meta = nfp_pipe_operation_meta(s->pipe);
      grub_strncpy(meta->file_path, s->file_name, OS_FILE_PATH_MAX_LENGTH);
    }
    break;

  case FILEOP_READ:
    result = nfp_pipe_operation_set(s->pipe, PIPE_OPERATION_FILE_READ, PIPE_ENDPOINT_WORKER);
    break;

  default:
    result = GRUB_ERR_BUG;
    break;
  }

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  if (!result)
    result = nfp_pipe_master_status_set(s->pipe, PIPE_STATE_PROCESSING);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  return result;
}

static int operation_stop(struct state_machine_ctrl *s)
{
  int result = 0;

  result = nfp_pipe_master_status_set(s->pipe, PIPE_STATE_WAITING);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  return result;
}

static grub_int32_t operation_transaction_start(struct state_machine_ctrl *s)
{
  grub_int32_t result;

  result = nfp_pipe_master_transaction_status_set(s->pipe, PIPE_TRANSACTION_STATUS_START);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  return result;
}

static grub_int32_t operation_transaction_stop(struct state_machine_ctrl *s, enum state_machine_states *next_state)
{
  grub_uint8_t *buffer;
  grub_int32_t result = 0;
  struct grub_net_buff *buf;
  grub_int32_t bytes_written;
  struct pipe_operation_file_read_meta *meta;

  *next_state = STATE_OPERATION_END;

  switch (s->file_op_current) {
  case FILEOP_PATH:
    break;

  case FILEOP_READ:
    meta = nfp_pipe_operation_meta(s->pipe);
    bytes_written = meta->transaction_size;
    if (meta->transaction_count == meta->transaction_total)
      bytes_written = meta->read_size - ((meta->transaction_total - 1) * meta->transaction_size);

    if (meta->transaction_count > meta->transaction_total){
      grub_dprintf("nfp", "File read transaction count > total\n");
      return GRUB_ERR_BUG;
    }

     if(s->file->size == 0)
      s->file->size = meta->read_size;

    result = nfp_pipe_buffer_read(s->pipe, bytes_written);
    if (result)
      return result;

    buffer = (grub_uint8_t *)nfp_pipe_operation_buffer(s->pipe);
    buf = grub_netbuff_alloc(bytes_written);
    if (!buf) {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, "nfp read file out of memory.");
      return GRUB_ERR_OUT_OF_MEMORY;
    }
    grub_netbuff_put (buf, bytes_written);
    grub_memcpy(buf->data, buffer, bytes_written);
    grub_net_put_packet(&s->file->device->pcinet->packs, buf);
    grub_dprintf("nfp", "\rReading (Block Set: %u/%u, Percentage: %u%%, Dynamic Poll Time: %u ms)\n",
                  meta->transaction_count,
                  meta->transaction_total,
                  (meta->transaction_count * 100) / meta->transaction_total,
                  s->poll_delay_ms);
    if (meta->transaction_count != meta->transaction_total)
      *next_state = STATE_TRANSACTION_START;
    else
      s->file->device->pcinet->eof = 1;
    break;

  default:
    result = GRUB_ERR_BUG;
    break;
  }

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  if (!result)
    result = nfp_pipe_master_transaction_status_set(s->pipe, PIPE_TRANSACTION_STATUS_NONE);

  if (!result)
    result = nfp_pipe_control_write(s->pipe);

  return result;
}

grub_err_t grub_pcinet_card_fs_open(struct grub_file *file, const char* file_name, grub_uint64_t timeout_ms)
{
  grub_err_t result;
  struct nfp_pipe *p;
  grub_int32_t detect_next_operation_ready;
  struct state_machine_ctrl *s;

  s = grub_zalloc(sizeof(struct state_machine_ctrl));
  if (!s)
    return GRUB_ERR_OUT_OF_MEMORY;
  p = grub_zalloc(sizeof(struct nfp_pipe));
  if(!p) {
    grub_free(s);
    return GRUB_ERR_OUT_OF_MEMORY;
  }
  s->pipe = p;
  file->data = (void*)s;
  result = nfp_pipe_init(p, g_cpp, PIPE_ENDPOINT_MASTER,
                         file_buffer, file_control, OS_FILE_OPERATION_MAX_META_SIZE);
  if (result)
    return result;
  grub_dprintf("nfp",
               "Control Block:Address=0x%" PRIxGRUB_UINT64_T ", Size=0x%" PRIxGRUB_UINT64_T "\n",
               file_control->addr,
               file_control->size);
  grub_dprintf("nfp",
               "Buffer Block:Address=0x%" PRIxGRUB_UINT64_T ", Size=0x%" PRIxGRUB_UINT64_T "\n",
                file_buffer->addr,
                file_buffer->size);

  /* Initialize the state machine */
  s->poll_delay_ms = PIPE_POLLING_DELAY_MS;
  s->poll_idle_count = 0;
  s->busy = 1;
  s->state = STATE_INIT;
  s->prev_state = STATE_INIT;
  s->elapse_ms = 0;
  s->pipe = p;
  s->file_chained_operation = 0;
  s->timeout_warning_once = 0;
  s->timer = grub_get_time_ms();
  s->file = file;
  grub_strncpy(s->file_name, file_name, OS_FILE_PATH_MAX_LENGTH);

  /* Make sure we reset the master state before we start */
  result = nfp_pipe_control_write(s->pipe);
  if (result)
    return result;
  s->file_op = FILEOP_PATH | FILEOP_READ;
  operation_next(s);

  while (s->busy) {
    /* State machine edge detect */
    if (s->state != s->prev_state) {
      /* We detected a state change. Take a timer snapshot */
      s->prev_state = s->state;
      s->elapse_ms = 0;
      s->timeout_warning_once = 0;
      s->timer = grub_get_time_ms();
      /* Make sure the poll delay is not too long */
      if (s->poll_idle_count <= PIPE_POLLING_IDLE_LOWER) {
        if (s->poll_delay_ms)
          s->poll_delay_ms--;
      }
      /* Reset the idle counter */
      s->poll_idle_count = 0;
      grub_dprintf("nfp", "State machine change state to: %d\n", s->state);
    } else {
      s->elapse_ms = grub_get_time_ms() - s->timer;
      s->poll_idle_count++;
      /* Make sure the poll delay is not too short */
      if ((s->poll_idle_count > PIPE_POLLING_IDLE_UPPER)
          && (s->state != STATE_INIT)) {
        if (s->poll_delay_ms < PIPE_POLLING_DELAY_MS_MAX)
          s->poll_delay_ms++;
      }
      if (s->elapse_ms > PIPE_STATE_TIMEOUT_SECONDS_WARNING*1000) {
        if (!s->timeout_warning_once) {
          s->timeout_warning_once = 1;
          grub_dprintf("nfp", "Waiting for worker endpoint (Time Left: %"PRIuGRUB_UINT64_T"s)\n",
                       timeout_ms/1000 - PIPE_STATE_TIMEOUT_SECONDS_WARNING);
        }
      }
      if (s->elapse_ms > timeout_ms) {
        grub_error (GRUB_ERR_TIMEOUT, "Worker pipe endpoint is not reponding\n");
        return GRUB_ERR_TIMEOUT;
      }
    }
    /* Get the latest state of the control block e.g. read worker state changes*/
    result = nfp_pipe_control_read(s->pipe);
    if (result)
      return result;

    switch (s->state) {
    case STATE_INIT:
      if (nfp_pipe_worker_status_get(s->pipe) == PIPE_STATE_WAITING) {
        result = operation_init(s);
        if (result)
          return result;
        s->state = STATE_OPERATION_START;
      }
      break;

    case STATE_OPERATION_START:
      /*
       * In the common single operation mode, the worker will be in PIPE_STATE_SETUP state, as it just came out of initialization. However
       * when multiple operations are chained, the worker will PIPE_STATE_WAITING, after it completed the previous operation. We can take this
       * as a sign that it is ready to receive the next operation.
       */
      detect_next_operation_ready = 0;
      if ((nfp_pipe_worker_status_get(s->pipe) == PIPE_STATE_SETUP)
         || ((s->file_chained_operation) && (nfp_pipe_worker_status_get(s->pipe) == PIPE_STATE_WAITING)))
        detect_next_operation_ready = 1;

      if ((detect_next_operation_ready)
          && (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_NONE)) {
        /* If we just dealt with the chained operation reset it */
        s->file_chained_operation = 0;
        /* We can now prepare the control block with operation information. This depends on the operation type */
        result = operation_start(s);
        if (result){
          grub_dprintf("nfp","Failed to start to operation for %d\n", s->file_op_current);
          return result;
        }
        s->state = STATE_TRANSACTION_START;
      }
      break;

    case STATE_TRANSACTION_START:
      if ((nfp_pipe_worker_status_get(s->pipe) == PIPE_STATE_PROCESSING)
          && (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_NONE)) {
        result = operation_transaction_start(s);
        if (result) {
          grub_dprintf("nfp","Failed to start to transaction for %d\n", s->file_op_current);
          return result;
        }
        s->state = STATE_TRANSACTION_END;
      }
      break;

    case STATE_TRANSACTION_END:
      if (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_END) {
        enum state_machine_states next_state = STATE_EXIT;

        /* This stage has to determine if all the transactions for the operation is complete */
        result = operation_transaction_stop(s, &next_state);

        if (result) {
          grub_dprintf("nfp","Failed to start to operation for %d\n", s->file_op_current);
          return result;
        }
        /* We can go back to transaction start from here if more is needed */
        s->state = next_state;
        if (s->state == STATE_TRANSACTION_START)
          return GRUB_ERR_NONE;
      }
      break;

    case STATE_OPERATION_END:

      if (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_NONE) {
        result = operation_stop(s);

        if (result)
          return result;

        /* Load the next operation */
        operation_next(s);

        if (s->file_op_current == FILEOP_NONE) {
          s->state = STATE_EXIT;
        } else {
          s->state = STATE_OPERATION_START;
          s->file_chained_operation = 1;
        }
      }
      break;

    case STATE_EXIT:
      s->busy = 0;
      break;
    }
    grub_millisleep(s->poll_delay_ms);
  }
  return GRUB_ERR_NONE;
}

grub_err_t grub_pcinet_card_fs_read(struct grub_file *file)
{
  grub_err_t result;
  struct state_machine_ctrl *s;
  s = (struct state_machine_ctrl *)file->data;
  if (!s)
  {
    grub_error (GRUB_ERR_BUG, "pcinet read file err.");
    return GRUB_ERR_BUG;
  }

  do
  {
     /* State machine edge detect */
    if (s->state != s->prev_state) {
      /* We detected a state change. Take a timer snapshot */
      s->prev_state = s->state;
      s->elapse_ms = 0;
      s->timeout_warning_once = 0;
      s->timer = grub_get_time_ms();
      /* Make sure the poll delay is not too long */
      if (s->poll_idle_count <= PIPE_POLLING_IDLE_LOWER) {
        if (s->poll_delay_ms)
          s->poll_delay_ms--;
      }
      /* Reset the idle counter */
      s->poll_idle_count = 0;
      grub_dprintf("nfp", "State machine change state to: %d\n", s->state);
    } else {
      s->elapse_ms = grub_get_time_ms() - s->timer;
      s->poll_idle_count++;
      /* Make sure the poll delay is not too short */
      if ((s->poll_idle_count > PIPE_POLLING_IDLE_UPPER)
          && (s->state != STATE_INIT)) {
        if (s->poll_delay_ms < PIPE_POLLING_DELAY_MS_MAX)
          s->poll_delay_ms++;
      }
      if (s->elapse_ms > PIPE_STATE_TIMEOUT_SECONDS*1000) {
        grub_error (GRUB_ERR_TIMEOUT, "pcinet read file timeout.");
        return GRUB_ERR_TIMEOUT;
      }
    }
    /* Get the latest state of the control block e.g. read worker state changes*/
    result = nfp_pipe_control_read(s->pipe);
    if (result)
      return result;

    switch (s->state) {
      case STATE_TRANSACTION_START:
        if ((nfp_pipe_worker_status_get(s->pipe) == PIPE_STATE_PROCESSING)
            && (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_NONE)) {
          result = operation_transaction_start(s);
          if (result) {
            grub_dprintf("nfp","Failed to start to transaction for %d\n", s->file_op_current);
            return result;
          }
          s->state = STATE_TRANSACTION_END;
        }
        break;

      case STATE_TRANSACTION_END:
        if (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_END) {
          enum state_machine_states next_state = STATE_EXIT;

          /* This stage has to determine if all the transactions for the operation is complete */
          result = operation_transaction_stop(s, &next_state);

          if (result) {
            grub_dprintf("nfp","Failed to start to operation for %d\n", s->file_op_current);
            return result;
          }
          /* We can go back to transaction start from here if more is needed */
          s->state = next_state;
          if (s->state == STATE_TRANSACTION_START)
            return GRUB_ERR_NONE;
        }
        break;
      case STATE_OPERATION_END:
      if (nfp_pipe_worker_transaction_status_get(s->pipe) == PIPE_TRANSACTION_STATUS_NONE) {
        result = operation_stop(s);

        if (result)
          return result;

        s->state = STATE_EXIT;
        return GRUB_ERR_NONE;
      }
      break;
      default:
        return GRUB_ERR_BUG;
    }
    if (s->prev_state == s->state)
      grub_millisleep(s->poll_delay_ms);
  }while(1);

  return GRUB_ERR_NONE;
}

grub_err_t grub_pcinet_card_fs_close(struct grub_file *file)
{
  struct state_machine_ctrl *s;

  s = (struct state_machine_ctrl *)file->data;
  if (!s)
    return GRUB_ERR_NONE;

  if(s->pipe)
    grub_free(s->pipe);

  grub_free(s);
  return GRUB_ERR_NONE;
}



