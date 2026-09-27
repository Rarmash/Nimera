#include <nimera/abi/syscall.h>
#include <nimera/heap.h>
#include <nimera/irq.h>
#include <nimera/pipe.h>
#include <nimera/scheduler.h>

struct pipe {
	char *buffer;
	unsigned int read_position;
	unsigned int write_position;
	unsigned int count;
	unsigned int readers;
	unsigned int writers;
	unsigned int reader_waiter;
	unsigned int writer_waiter;
};

static unsigned int live_count;
static u64 live_bytes;

static void wake(unsigned int *waiter)
{
	if (*waiter == NIMERA_PIPE_NO_WAITER) return;
	scheduler_wake_thread(*waiter);
	*waiter = NIMERA_PIPE_NO_WAITER;
}

struct pipe *pipe_create(void)
{
	struct pipe *pipe = (struct pipe *)kmalloc(sizeof(*pipe));
	if (pipe == (struct pipe *)0) return (struct pipe *)0;
	pipe->buffer = (char *)kmalloc(NIMERA_PIPE_CAPACITY);
	if (pipe->buffer == (char *)0) {
		kfree(pipe);
		return (struct pipe *)0;
	}
	pipe->read_position = 0U;
	pipe->write_position = 0U;
	pipe->count = 0U;
	pipe->readers = 0U;
	pipe->writers = 0U;
	pipe->reader_waiter = NIMERA_PIPE_NO_WAITER;
	pipe->writer_waiter = NIMERA_PIPE_NO_WAITER;
	++live_count;
	live_bytes += (u64)NIMERA_PIPE_CAPACITY + sizeof(*pipe);
	return pipe;
}

static void pipe_maybe_free(struct pipe *pipe)
{
	if (pipe != (struct pipe *)0 && pipe->readers == 0U && pipe->writers == 0U)
	{
		kfree(pipe->buffer);
		kfree(pipe);
		--live_count;
		live_bytes -= (u64)NIMERA_PIPE_CAPACITY + sizeof(*pipe);
	}
}

void pipe_discard(struct pipe *pipe)
{
	if (pipe == (struct pipe *)0) return;
	if (pipe->readers != 0U || pipe->writers != 0U) return;
	kfree(pipe->buffer);
	kfree(pipe);
	--live_count;
	live_bytes -= (u64)NIMERA_PIPE_CAPACITY + sizeof(*pipe);
}

void pipe_reader_open(struct pipe *pipe) { if (pipe != (struct pipe *)0) ++pipe->readers; }
void pipe_writer_open(struct pipe *pipe) { if (pipe != (struct pipe *)0) ++pipe->writers; }

void pipe_reader_close(struct pipe *pipe)
{
	if (pipe == (struct pipe *)0) return;
	if (pipe->readers != 0U) --pipe->readers;
	wake(&pipe->writer_waiter);
	pipe_maybe_free(pipe);
}

void pipe_writer_close(struct pipe *pipe)
{
	if (pipe == (struct pipe *)0) return;
	if (pipe->writers != 0U) --pipe->writers;
	wake(&pipe->reader_waiter);
	pipe_maybe_free(pipe);
}

void pipe_wait_reader(struct pipe *pipe, unsigned int thread_id)
{
	if (pipe != (struct pipe *)0) pipe->reader_waiter = thread_id;
}

void pipe_wait_writer(struct pipe *pipe, unsigned int thread_id)
{
	if (pipe != (struct pipe *)0) pipe->writer_waiter = thread_id;
}

long long pipe_read_try(struct pipe *pipe, char *buffer, u64 length)
{
	u64 count;
	if (pipe == (struct pipe *)0 || buffer == (char *)0) return NIMERA_NERR_INVALID;
	if (length == 0ULL) return 0LL;
	if (pipe->count == 0U)
		return pipe->writers == 0U ? 0LL : NIMERA_NERR_WOULD_BLOCK;
	count = length < (u64)pipe->count ? length : (u64)pipe->count;
	for (u64 index = 0ULL; index < count; ++index) {
		buffer[index] = pipe->buffer[pipe->read_position];
		pipe->read_position = (pipe->read_position + 1U) % NIMERA_PIPE_CAPACITY;
	}
	pipe->count -= (unsigned int)count;
	wake(&pipe->writer_waiter);
	return (long long)count;
}

long long pipe_write_try(struct pipe *pipe, const char *buffer, u64 length)
{
	u64 count;
	if (pipe == (struct pipe *)0 || buffer == (const char *)0) return NIMERA_NERR_INVALID;
	if (pipe->readers == 0U) return NIMERA_NERR_BROKEN_PIPE;
	if (length == 0ULL) return 0LL;
	if (pipe->count == NIMERA_PIPE_CAPACITY) return NIMERA_NERR_WOULD_BLOCK;
	count = length < (u64)(NIMERA_PIPE_CAPACITY - pipe->count) ?
		length : (u64)(NIMERA_PIPE_CAPACITY - pipe->count);
	for (u64 index = 0ULL; index < count; ++index) {
		pipe->buffer[pipe->write_position] = buffer[index];
		pipe->write_position = (pipe->write_position + 1U) % NIMERA_PIPE_CAPACITY;
	}
	pipe->count += (unsigned int)count;
	wake(&pipe->reader_waiter);
	return (long long)count;
}

unsigned int pipe_reader_count(const struct pipe *pipe)
{
	return pipe == (const struct pipe *)0 ? 0U : pipe->readers;
}

unsigned int pipe_writer_count(const struct pipe *pipe)
{
	return pipe == (const struct pipe *)0 ? 0U : pipe->writers;
}

unsigned int pipe_live_count(void) { return live_count; }
u64 pipe_live_bytes(void) { return live_bytes; }
