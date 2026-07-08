//
// Created by jamal on 07/07/2026.
//

#include <unistd.h>
#include <cstring>
#include <functional>
#include <sys/mman.h>

#define MAX_ORDER 10
#define MMAP_ORDER (MAX_ORDER+1)
#define BLOCK_NUM 32
#define MAX_SIZE 0x5F5E100
#define METADATA_SIZE sizeof(MallocMetaData)
#define SBRK_FAILURE (void*)(-1)

class MallocMetaData {
public:
    size_t order;
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

    static MallocMetaData *init_sbrk_ptr(void *ptr, size_t order) {
        MallocMetaData *metadata = from_sbrk_ptr(ptr);
        return init_ptr(metadata, order);
    }

    static MallocMetaData *init_uptr_ptr(uintptr_t ptr, size_t order) {
        return init_ptr((MallocMetaData *) ptr, order);
    }

    static MallocMetaData *init_ptr(MallocMetaData *metadata, size_t order) {
        metadata->order = order;
        // about to be returned
        metadata->is_free = true;
        // i'm the last so that it's inherntly ordered by sbrk
        metadata->next = nullptr;

        // init anyway
        metadata->prev = nullptr;

        return metadata;
    }

    void *as_user_ptr() {
        return (void *) (this) + METADATA_SIZE;
    }

    bool is_mmap() const {
        return order > MAX_ORDER;
    }
};

class MmapMallocMetaData : public MallocMetaData {
public:
    size_t mmap_size() const {
        return order;
    }

    void deallocate() {
        size_t size = from_parent(this)->mmap_size();
        munmap(
            this,
            size
        );
    }

    static MmapMallocMetaData *from_parent(MallocMetaData *parent) {
        return static_cast<MmapMallocMetaData *>(parent);
    }
};


static MallocMetaData *free_order_array[MAX_ORDER + 1];


// this lives on the bbs so am ok.
static bool has_been_inited = false;
// 2^22
constexpr std::uintptr_t alignment = 0b1 << 22;
constexpr std::size_t MIN_BLOCK_SIZE = 128;

using std::uintptr_t;

void *init_aligned() {
    // so we need to init 32 blocks of size 128kb and we'll make them aligned
    // where are we at?
    void *ptr = sbrk(0);
    if (ptr == SBRK_FAILURE) {
        return nullptr;
    }
    std::uintptr_t addr = (std::uintptr_t) (ptr);

    // 0x3[FFFFF] => b[3] = 0011
    constexpr std::uintptr_t alignment_mask = alignment - 1;

    std::uintptr_t residue = addr & alignment_mask;


    std::uintptr_t padding = 0;
    // must have the lower 22 bits as 0 so
    if (residue != 0) {
        // otherwise how far we are? we need to enlarge to reach a multiple which means we need to zero the lower 22 bits.
        // so x+y = 0 => x=-y we need to add so:
        // so adding padding means we are now at addr of muliple 32.
        // so starting from there we want another 32*128kb
        // so we ask for padding + 2^22
        padding = alignment - residue;
    }

    void *res = sbrk(alignment + padding);
    if (res == SBRK_FAILURE) {
        // sbrk failed
        return nullptr;
    }
    return res + padding;
}

uintptr_t to_uptr(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr);
}

std::size_t block_size(int order) {
    return MIN_BLOCK_SIZE << order;
}

uintptr_t buddy_ptr(uintptr_t ptr, int order) {
    return ptr ^ block_size(order);
}

MallocMetaData *buddy_ptr(void *ptr, int order) {
    return (MallocMetaData *) buddy_ptr(to_uptr(ptr), order);
}

/* I should move these to malloc data wtf is this c-style ?  */
void ptr_unlink(MallocMetaData *current) {
    // first let's remove it from the list
    auto prev = current->prev;
    auto next = current->next;

    if (next != nullptr) {
        next->prev = prev;
    }
    if (prev != nullptr) {
        prev->next = next;
    } else {
        // I was the haed so
        free_order_array[current->order] = next;
    }

    // then those values mean nothing anymore
    current->next = nullptr;
    current->prev = nullptr;
}

void ptr_insert(MallocMetaData *buddy, int order) {
    // but I must make sure the list is addr ordered.
    MallocMetaData *list_current = free_order_array[order];
    if (list_current == nullptr) {
        // nobody there so put buddy directly
        free_order_array[order] = buddy;
    } else {
        // keep walking till reaching somebody greater than me or tail
        while (list_current->next != nullptr && list_current->next < buddy) {
            list_current = list_current->next;
        }
        // at this point the invariant is buddy <= list_current->next so lets sneak in between list_current and it's next
        buddy->next = list_current->next;
        buddy->prev = list_current;

        if (list_current->next != nullptr) {
            list_current->next->prev = buddy;
        }

        list_current->next = buddy;
    }
}

int ceilLog2(int x) {
    if (x <= 1) return 0;

    // how many leading zeros
    int msb = 63 - __builtin_clz(x);

    // if x is an exact power of 2, the MSB is the final answer
    // otherwise, increment by 1
    return msb + ((x & (x - 1)) != 0);
}

size_t blk_order(size_t size) {
    return std::max(
        0,
        ceilLog2(size) - 7
    );
}

void *smalloc(size_t size) {
    // for now a naive search in O(n). will make it O(log(n)) with AVL implementation on these pointers.
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }

    size_t blk_size = size + METADATA_SIZE;

    // by specs then just use mmap directly
    if (blk_size > block_size(MAX_ORDER)) {
        void *ptr = mmap(
            nullptr,
            blk_size,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_ANONYMOUS,
            -1,
            0
        );
        if (ptr == SBRK_FAILURE) {
            return nullptr;
        }

        // setting order is mmap
        MallocMetaData *metadata = MallocMetaData::init_sbrk_ptr(ptr, blk_size);
        metadata->is_free = false;

        return metadata->as_user_ptr();
    }

    // "lazy init"
    if (has_been_inited == false) {
        auto base_ptr = init_aligned();
        // then create all the blocks in the order 10 bucket
        uintptr_t current_ptr = to_uptr(base_ptr);

        for (int i = 0; i < BLOCK_NUM; i++) {
            auto *block = MallocMetaData::init_uptr_ptr(current_ptr, MAX_ORDER);

            ptr_insert(block, MAX_ORDER);

            current_ptr += block_size(MAX_ORDER);
        }

        has_been_inited = true;
    }


    // the difference now is that I want to search orders for something
    // the user wants size bytes, the least I can give him is block_size(0), so I migh as well ask for max(block_size(0), x)
    // and add the metadata size
    // then do the power 2 clipping since I want x <= 2^k where k is the upper bound order to include x.
    // int min_order = std::max(
    //     ceilLog2(
    //         std::max(
    //             size,
    //             block_size(0)
    //         ) + METADATA_SIZE
    //     ) - 7,
    //     0
    // );
    int min_order = blk_order(blk_size);
    // since order is 128= 2^7 based, max is just a gurad but is counted for in min block_size
    for (int search_order = min_order + 1; search_order <= MAX_ORDER; search_order++) {
        MallocMetaData *current = free_order_array[search_order];

        // the list maintains free blocks so hitting something were done
        if (current != nullptr) {
            // it's not free anymore
            ptr_unlink(current);

            // cut if you can. the assumption is that sub-buddy is free because we already had the whole thing
            int m = search_order;
            // keep cutting
            while (m > min_order) {
                // ok so we got something from the higher order which means x<= 2^k < 2^{k+1}
                // so we are safe to cut in half. we have something of order m=k+1.
                // let's treat it as k=m-1 order and ask for it's buddy starting addr

                MallocMetaData *buddy = buddy_ptr(
                    current,
                    m - 1
                );

                // this guy at buddy is not a free valid block yet so lets init it and add it to the array of order m-1
                // first put the metadata there
                MallocMetaData::init_sbrk_ptr(buddy, m - 1);
                buddy->is_free = true;

                // we have a new block free of order m-1
                ptr_insert(buddy, m - 1);

                // move to the next order
                m--;
            }


            current->is_free = false;

            // what is your new order
            current->order = min_order;

            // return as user ptr
            return current->as_user_ptr();
        }
    }

    // found  nothing :(
    return nullptr;
}


MallocMetaData *buddy_merge(MallocMetaData *blk) {
    size_t order = blk->order;


    // don't merge max order
    while (order < MAX_ORDER) {
        // otherwise let keep merging if we can,
        auto *buddy = buddy_ptr(blk, order);
        if (buddy->is_free && buddy->order == order) {
            // first we remove it from free array
            ptr_unlink(buddy);

            // lets pick the "left" one always
            blk = std::min(buddy, blk);

            // then when I increase my order its like I have "swallowed the buddy" the metadata there will "invalid"
            // once I virtually increase my size
            blk->order = ++order;
        } else {
            // not free stop there.
            break;
        }
    }

    return blk;
}

void sfree(void *p) {
    if (p == nullptr) return;

    // first lets get the metadata, since it's me who allocated this ptr I know I have put before it the metadata block
    auto *blk = MallocMetaData::from_user_ptr(p);
    if (blk->is_free) {
        // if was already free
        return;
    }
    if (blk->is_mmap()) {
        MmapMallocMetaData::from_parent(blk)->deallocate();
        return;
    }
    //otherwise is sbrk block

    // buddy merge
    blk = buddy_merge(blk);

    blk->is_free = true;
    // and lets make it available
    ptr_insert(blk, blk->order);
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
    if (block_size(blk->order) >= size) {
        // no need to do anything
        return blk->as_user_ptr();
    }

    if (blk->is_mmap()) {
        // then simply give up the current blk and smalloc a new
        auto mmap_blk = MmapMallocMetaData::from_parent(blk);

        // free then let smmaloc create a new one
        mmap_blk->deallocate();

        return smalloc(size);
    }

    size_t target_order = blk_order(size);

    MallocMetaData *cur = blk;
    int order = cur->order;

    // now lets check, can I use buddies?
    while (order < target_order) {
        // otherwise let keep merging if we can,
        auto *buddy = buddy_ptr(cur, order);
        if (buddy->is_free && buddy->order == order) {
            // pick left
            cur = std::min(buddy, cur);

            order++;
            // we don't do anything to the block, we just check can we reach the target order?
        } else {
            // not free stop there.
            break;
        }
    }
    // then yes I could've merged so lets use it
    if (order == target_order) {
        // this swallows or retroactily "merges" every block but we need to remove them from the list

        // in a sane model I would've keept the buddy list but we are limited on memory so we'll just merge again
        cur = buddy_merge(blk);

        cur->is_free = false;
        cur->order = target_order;


        return cur->as_user_ptr();
    }

    size_t target_size = block_size(target_order);

    // If I could've merge buddies then it is what it is let smalloc do the job
    void *nptr = smalloc(target_size);
    if (nptr == nullptr) {
        // failed
        return nullptr;
    }

    // nptr to me here is as if I was a user, not aware of metadata
    std::memmove(nptr, oldp, target_size);

    // then free the older blk
    sfree(blk);

    // user ptr
    return nptr;
}

void traverse_blocks(const std::function<void(MallocMetaData *)> &predicate) {
    for (int i = 0; i < MAX_ORDER; i++) {
        MallocMetaData *current = free_order_array[i];
        while (current != nullptr) {
            predicate(current);
            current = current->next;
        }
    }
    for (int i = 0; )

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
            return acc_pair{current->is_free, block_size(current->order)};
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
        if (current->is_mmap()) {
            return acc_pair{true, MmapMallocMetaData::from_parent(current)->mmap_size()};
        }
        return acc_pair{true, block_size(current->order)};
    });
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * METADATA_SIZE;
}

size_t _size_meta_data() {
    return METADATA_SIZE;
}


int main() {
    //TODO: DUMMY FOR COMPILE
}
