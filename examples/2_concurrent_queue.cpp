#include <coio/core.h>
#include <coio/utils/conqueue.h>


auto main() -> int {
    coio::conqueue<int> conqueue(1);
    coio::inplace_ring_buffer<int, 12> ring_buffer(12);
}