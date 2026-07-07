//
// Created by jamal on 07/07/2026.
//
#include <unistd.h>

#define SBRK_FAILURE (void*)(-1)
#define MAX_SIZE 0x5F5E100

void *smalloc(size_t size) {
    // ok so sbrk(size) returns the previous program break
    // which means to the user start writing from there.
    if (size == 0) {
        return nullptr;
    }
    // 10^8 is 0x5F5E100 in hex
    if (size > MAX_SIZE) {
        return nullptr;
    }
    // we kindly ask sbrk for space in the heap
    void *prev_break = sbrk(size);

    // failure
    if (prev_break == SBRK_FAILURE) {
        return nullptr;
    }

    // otherwise let the user start wrirting from here.
    return prev_break;
}
