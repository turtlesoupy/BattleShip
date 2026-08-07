/**
 * coroutine_emscripten.cpp — Emscripten Fiber (Asyncify) coroutine backend.
 *
 * WASM has no user-controllable stack switching, so ucontext/fibers don't
 * exist natively. Emscripten's fiber API (emscripten/fiber.h) provides the
 * same shape on top of Asyncify: each fiber owns a C stack plus an
 * "asyncify stack" that stores unwound call frames during a switch.
 * Requires linking with -sASYNCIFY=1.
 *
 * Semantics mirror coroutine_posix.cpp: resume() transfers to the coroutine
 * until it yields or its entry returns; nested resumes (main -> Thread5 ->
 * GObj coroutine) restore the previous current-coroutine on return.
 */

#ifdef __EMSCRIPTEN__

#include "coroutine.h"

#include <emscripten/fiber.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_STACK_SIZE 32768
/* Frames unwound at a switch point are copied to the asyncify stack. The
 * game coroutine yields from DEEP call chains (render submit inside
 * syMainLoop), and overflowing this buffer corrupts adjacent heap silently
 * — scale it with the requested C stack instead of a fixed size. */
#define ASYNCIFY_STACK_MIN (256 * 1024)
#define ASYNCIFY_STACK_MAX (1024 * 1024)

struct PortCoroutine {
	emscripten_fiber_t fiber;
	emscripten_fiber_t *caller_fiber; /* who to return to on yield/finish */
	void (*entry)(void *);
	void *arg;
	int finished;
	char *c_stack;
	char *asyncify_stack;
	size_t asyncify_size;
};

/* The fiber representing the main thread's original context. */
static emscripten_fiber_t sMainFiber;
static char sMainAsyncifyStack[ASYNCIFY_STACK_MIN];
static int sMainInitialized = 0;

/* Currently executing coroutine (NULL = main context). */
static PortCoroutine *sCurrentCoroutine = NULL;

/* Fiber owning the CPU right now (main fiber when sCurrentCoroutine==NULL). */
static emscripten_fiber_t *current_fiber(void)
{
	return sCurrentCoroutine ? &sCurrentCoroutine->fiber : &sMainFiber;
}

/*
 * Fiber entry functions must never return (emscripten aborts if they do).
 * Run the game entry, mark finished, then hand control back to the caller
 * forever — resume() guards on `finished`, so control never comes back,
 * but the loop keeps us safe if it somehow does.
 */
static void fiber_entry(void *vco)
{
	PortCoroutine *co = (PortCoroutine *)vco;

	co->entry(co->arg);

	co->finished = 1;
	for (;;) {
		sCurrentCoroutine = NULL;
		emscripten_fiber_swap(&co->fiber, co->caller_fiber);
	}
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

void port_coroutine_init_main(void)
{
	if (!sMainInitialized) {
		emscripten_fiber_init_from_current_context(&sMainFiber, sMainAsyncifyStack,
		                                           sizeof(sMainAsyncifyStack));
		sMainInitialized = 1;
	}
}

PortCoroutine *port_coroutine_create(void (*entry)(void *), void *arg,
                                     size_t stack_size)
{
	PortCoroutine *co;

	if (!sMainInitialized) {
		port_coroutine_init_main();
	}

	if (stack_size < MIN_STACK_SIZE) {
		stack_size = MIN_STACK_SIZE;
	}

	co = (PortCoroutine *)calloc(1, sizeof(PortCoroutine));
	if (co == NULL) {
		return NULL;
	}

	co->asyncify_size = stack_size;
	if (co->asyncify_size < ASYNCIFY_STACK_MIN) {
		co->asyncify_size = ASYNCIFY_STACK_MIN;
	}
	if (co->asyncify_size > ASYNCIFY_STACK_MAX) {
		co->asyncify_size = ASYNCIFY_STACK_MAX;
	}

	co->c_stack = (char *)malloc(stack_size);
	co->asyncify_stack = (char *)malloc(co->asyncify_size);
	if (co->c_stack == NULL || co->asyncify_stack == NULL) {
		free(co->c_stack);
		free(co->asyncify_stack);
		free(co);
		return NULL;
	}

	co->entry = entry;
	co->arg = arg;
	co->finished = 0;

	emscripten_fiber_init(&co->fiber, fiber_entry, co, co->c_stack, stack_size,
	                      co->asyncify_stack, co->asyncify_size);

	return co;
}

void port_coroutine_destroy(PortCoroutine *co)
{
	if (co == NULL) {
		return;
	}
	if (co == sCurrentCoroutine) {
		fprintf(stderr, "SSB64: port_coroutine_destroy on current coroutine\n");
		abort();
	}
	free(co->c_stack);
	free(co->asyncify_stack);
	free(co);
}

void port_coroutine_resume(PortCoroutine *co)
{
	if (co == NULL || co->finished) {
		return;
	}

	/* Save the current coroutine so nested resumes restore correctly
	 * (see coroutine_posix.cpp for the Thread5/GObj example). */
	PortCoroutine *prev = sCurrentCoroutine;
	emscripten_fiber_t *from = current_fiber();

	co->caller_fiber = from;
	sCurrentCoroutine = co;
	emscripten_fiber_swap(from, &co->fiber);

	sCurrentCoroutine = prev;
}

void port_coroutine_yield(void)
{
	PortCoroutine *co = sCurrentCoroutine;
	if (co == NULL) {
		fprintf(stderr, "SSB64: port_coroutine_yield called outside coroutine\n");
		return;
	}

	sCurrentCoroutine = NULL;
	emscripten_fiber_swap(&co->fiber, co->caller_fiber);
	/* Returns here when resumed. */
}

int port_coroutine_is_finished(PortCoroutine *co)
{
	if (co == NULL) {
		return 1;
	}
	return co->finished;
}

int port_coroutine_in_coroutine(void)
{
	return sCurrentCoroutine != NULL;
}

#endif /* __EMSCRIPTEN__ */
