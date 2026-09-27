#ifndef NIMERA_PIPE_H
#define NIMERA_PIPE_H

#include <nimera/types.h>

/* Keep the ring just below one PMM-backed heap page's usable payload. */
#define NIMERA_PIPE_CAPACITY 4000U
#define NIMERA_PIPE_NO_WAITER 0xffffffffU

struct pipe;

struct pipe *pipe_create(void);
/* Release a pipe that has never been attached to a process endpoint. */
void pipe_discard(struct pipe *pipe);
void pipe_reader_open(struct pipe *pipe);
void pipe_writer_open(struct pipe *pipe);
void pipe_reader_close(struct pipe *pipe);
void pipe_writer_close(struct pipe *pipe);
void pipe_wait_reader(struct pipe *pipe, unsigned int thread_id);
void pipe_wait_writer(struct pipe *pipe, unsigned int thread_id);

/* These operations are called with IRQs disabled by the syscall path. */
long long pipe_read_try(struct pipe *pipe, char *buffer, u64 length);
long long pipe_write_try(struct pipe *pipe, const char *buffer, u64 length);
unsigned int pipe_reader_count(const struct pipe *pipe);
unsigned int pipe_writer_count(const struct pipe *pipe);
unsigned int pipe_live_count(void);
u64 pipe_live_bytes(void);

#endif
