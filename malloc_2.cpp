//
// Created by jamal on 07/07/2026.
//

#include <unistd.h>
#include <cstring>
#include <functional>

#define MAX_SIZE 0x5F5E100
#define METADATA_SIZE sizeof(MallocMetaData)
#define SBRK_FAILURE (void*)(-1)

class MallocMetaData {
public:
    size_t size;
    bool is_free;
    MallocMetaData *next;
    MallocMetaData *prev;


    static MallocMetaData *from_user_ptr(void *ptr) {
        return static_cast<MallocMetaData *>(ptr - METADATA_SIZE);
    }

    static MallocMetaData *from_sbrk_ptr(void *ptr) {
        // let's treat this pointer as mallocmeta ptr this is like static cast but c syntax style i'll use both for fun
        return (MallocMetaData *) (ptr);
    }

    static MallocMetaData *init_sbrk_ptr(void *ptr, size_t size) {
        MallocMetaData *metadata = from_sbrk_ptr(ptr);

        metadata->size = size;
        // about to be returned
        metadata->is_free = true;
        // i'm the last so that it's inherntly ordered by sbrk
        metadata->next = nullptr;

        // init anyway
        metadata->prev = nullptr;

        return metadata;
    }

    static MallocMetaData *allocate(size_t size) {
        // we want the size and this metadata size
        void *ptr = sbrk(size + METADATA_SIZE);
        if (ptr == SBRK_FAILURE) return nullptr;

        // then init it
        return init_sbrk_ptr(ptr, size);
    }

    void *as_user_ptr() {
        return (void *) (this) + METADATA_SIZE;
    }
};


static MallocMetaData *first_block = nullptr;

void *smalloc(size_t size) {
    // for now a naive search in O(n). will make it O(log(n)) with AVL implementation on these pointers.
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }

    // "lazy init"
    if (first_block == nullptr) {
        // if no block was allocated then I need to sbrk allocate it. and since I cannot use new i'll cast the ptr on it
        MallocMetaData *ptr = MallocMetaData::allocate(size);

        if (ptr == nullptr) {
            // failure
            return nullptr;
        }

        first_block = ptr;

        // everything is inited so let's skip the loop which will work anyway
        return first_block->as_user_ptr();
    }

    // now search for a free block with at least the requested size
    MallocMetaData *current = first_block;
    // we need this for the tail
    MallocMetaData *prev = nullptr;
    while (current != nullptr) {
        if (current->size >= size && current->is_free) {
            // we have a free block

            // lets denote it as used and return it
            current->is_free = false;
            //  the user wants a place to start writing.
            return current->as_user_ptr();
        }

        prev = current;
        current = current->next;
    }

    // otherwise no block exists so let's sbrk it with our metadata
    auto *blk = MallocMetaData::allocate(size);
    if (blk == nullptr) {
        // failure
        return nullptr;
    }

    // the blk comes inited but as free so let's mark it
    blk->is_free = false;

    //  let's update the tail
    blk->prev = prev;
    prev->next = blk;

    // skip the metadata size
    return blk->as_user_ptr();
}

void *scalloc(size_t num, size_t size) {
    // any is none (just for flexing: because Z is an integral domain then ab=0 <=> a = 0 or b=0)
    if (size * num == 0) {
        return nullptr;
    }
    if (size * num > MAX_SIZE) {
        return nullptr;
    }

    // now instead of reinventing the wheel w'll just call smalloc with num*size and init the data
    size_t rsize = size * num;

    // in this func will be treated as pure ptr no metadata involved.
    void *ptr = smalloc(rsize);
    if (ptr == nullptr) {
        return nullptr;
    }

    // then init to 0
    std::memset(ptr, 0, rsize);

    return ptr;
}

void sfree(void *p) {
    if (p == nullptr) return;

    // first lets get the metadata, since it's me who allocated this ptr I know I have put before it the metadata block
    auto *blk = MallocMetaData::from_user_ptr(p);
    if (blk->is_free) {
        // if was already free
        return;
    }

    // and mark as free
    blk->is_free = true;
}

void *srealloc(void *oldp, size_t size) {
    if (oldp == nullptr) {
        // regular allocate
        return smalloc(size);
    }
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }
    // otherwise pull up the metadata
    MallocMetaData *blk = MallocMetaData::from_user_ptr(oldp);
    if (blk->size >= size) {
        // no need to do anything
        return blk->as_user_ptr();
    }
    // if not then let's ask for something bigger from smalloc, no need to handle metadata. let it do it
    // it may return a fresh sbrked ptr or search for available block, let it figure it out.
    void *nptr = smalloc(size);
    if (nptr == nullptr) {
        // failed
        return nullptr;
    }
    // although I know for sure there is no overlap, because I'm currently holding this block (have not freed it yet)
    // so smalloc cannot return this block to me, it has to find something else. so I don't see what prevents me from
    // using memcopy but i'll use memmove anyway as the 6'th note.

    // we need to copy blk->size bytes not size bytes!!.
    std::memmove(nptr, oldp, blk->size);

    // then "free"
    blk->is_free = true;

    return nptr;
}

void traverse_blocks(const std::function<void(MallocMetaData *)> &predicate) {
    MallocMetaData *current = first_block;
    while (current != nullptr) {
        predicate(current);
        current = current->next;
    }
}


// this is where years of javascript comes into play
using acc_pair = std::pair<bool, long>; // this i owe to typescript
size_t accumulator(const std::function<acc_pair(MallocMetaData *)> &predicate) {
    size_t result = 0;
    traverse_blocks([&result, &predicate](MallocMetaData *metadata) {
        auto pred = predicate(metadata);
        if (pred.first) {
            result += pred.second;
        }
    });

    return result;
}


size_t _num_free_blocks() {
    // since no optimizations are required we'll just check everything to completion
    return accumulator([](auto current) {
        return acc_pair{current->is_free, 1};
    });
}


size_t _num_free_bytes() {
    return accumulator(
        [](auto current) {
            return acc_pair{current->is_free, current->size};
        }
    );
}

size_t _num_allocated_blocks() {
    return accumulator([](auto current) {
        return acc_pair{true, 1};
    });
}

size_t _num_allocated_bytes() {
    return accumulator([](auto current) {
        return acc_pair{true, current->size};
    });
}

size_t _num_meta_data_bytes() {
    return accumulator([](auto current) {
        return acc_pair{true, METADATA_SIZE};
    });
}

size_t _size_meta_data() {
    return METADATA_SIZE;
}


int main() {
    //TODO: DUMMY FOR COMPILE
}
