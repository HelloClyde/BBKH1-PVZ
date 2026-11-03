/* Cache reclamation only suggests that an allocation might now fit.  The
   firmware allocator is the authority on actual contiguous space.  This
   routine is shared verbatim with the host fragmentation/reentry tests. */
#include <stddef.h>
#include <stdint.h>

#define PRESSURE_RETRY_LIMIT 65536u

static int allocation_failed(const void *memory)
{
    return !memory || (uintptr_t)memory == UINT32_MAX;
}

void *pvz_runtime_allocate_with_pressure(size_t requested, size_t native_requested,
    void *(*native_allocate)(size_t,size_t,uintptr_t *), size_t (*reclaim_one)(size_t),
    volatile uint32_t diagnostic[6], int *running, uintptr_t *allocation_span)
{
    void *raw;
    unsigned int steps;
    *allocation_span = 0;
    raw = native_allocate(requested,native_requested,allocation_span);
    if (!allocation_failed(raw) || !reclaim_one || *running)
        return raw;

    /* Keep the guard through callbacks AND native retries.  A callback must
       not allocate, but an accidental nested failure must still terminate. */
    *running = 1;
    diagnostic[4] = (uint32_t)requested;
    for (steps = 0; steps < PRESSURE_RETRY_LIMIT; ++steps) {
        size_t released;
        uint32_t before;
        ++diagnostic[0];
        released = reclaim_one(requested);
        if (!released)
            break;
        before = diagnostic[3];
        diagnostic[3] = released > UINT32_MAX - before
            ? UINT32_MAX : before + (uint32_t)released;
        ++diagnostic[1];
        *allocation_span = 0;
        raw = native_allocate(requested,native_requested,allocation_span);
        if (!allocation_failed(raw)) {
            ++diagnostic[2];
            break;
        }
    }
    *running = 0;
    return raw;
}
