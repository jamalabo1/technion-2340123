//
// Created by jamal on 07/07/2026.
//

#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <cstdint>
#include <algorithm>


#define MAX_ORDER 10
#define MMAP_ORDER (MAX_ORDER+1)
#define BLOCK_NUM 32
#define MAX_SIZE 0x5F5E100
#define METADATA_SIZE sizeof(MallocMetaData)
#define SBRK_FAILURE (void*)(-1)

using std::uintptr_t;

// this lives on the bbs so am ok.
static bool has_been_inited = false;
// 2^22
constexpr std::uintptr_t alignment = 0b1 << 22;
constexpr std::size_t MIN_BLOCK_SIZE = 128;

/* helpers */
std::size_t blk_size(const size_t &order) {
    return MIN_BLOCK_SIZE << order;
}

uintptr_t to_uptr(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr);
}

// int ceilLog2(int x) {
//     if (x <= 1) return 0;
//
//     // how many leading zeros
//     int msb = 63 - __builtin_clz(x);
//
//     // if x is an exact power of 2, the MSB is the final answer
//     // otherwise, increment by 1
//     return msb + ((x & (x - 1)) != 0);
// }
//
// size_t blk_order(size_t size) {
//     return std::max(
//         0,
//         ceilLog2(size) - 7
//     );
// }

//TODO: make it bitwise later
size_t blk_order(size_t total_size) {
    size_t order = 0;
    size_t capacity = MIN_BLOCK_SIZE;

    while (capacity < total_size) {
        capacity <<= 1;
        ++order;
    }

    return order;
}

class MallocMetaData {
public:
    // free list
    static MallocMetaData *free_order_array[MAX_ORDER + 1];
    // mmaps list
    static MallocMetaData *mmap_list;


    size_t order;
    bool is_free;
    MallocMetaData *next;
    MallocMetaData *prev;

    /* */


    static MallocMetaData *from_raw_ptr(uintptr_t ptr) {
        return reinterpret_cast<MallocMetaData *>(ptr);
    }

    static MallocMetaData *from_user_ptr(void *ptr) {
        return from_raw_ptr(to_uptr(ptr) - METADATA_SIZE);
    }

    static MallocMetaData *init_raw_ptr(void *ptr, size_t order) {
        return init_uptr_ptr(to_uptr(ptr), order);
    }

    static MallocMetaData *init_uptr_ptr(uintptr_t ptr, size_t order) {
        return init_ptr(from_raw_ptr(ptr), order);
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
        return (void *) (this->as_uptr() + METADATA_SIZE);
    }

    uintptr_t as_uptr() {
        return reinterpret_cast<uintptr_t>(this);
    }

    /* sizes  */
    bool is_mmap() const {
        return order > MAX_ORDER;
    }

    size_t block_size() const {
        if (is_mmap()) {
            return this->order;
        }
        return blk_size(order);
    }

    size_t data_size() const {
        size_t size;
        if (this->is_mmap()) {
            size = order;
        } else {
            size = blk_size(order);
        }

        return size - METADATA_SIZE;
    }

    static MallocMetaData *find_mmap_block(void *ptr) {
        for (MallocMetaData *current = mmap_list;
             current != nullptr;
             current = current->next) {
            if (current->as_user_ptr() == ptr) {
                return current;
            }
        }

        return nullptr;
    }

    static MallocMetaData *get_order_head(size_t order) {
        return free_order_array[order];
    }

    /*
     * list operations
     */
    // remove this block from the list
    void unlink() {
        // first let's remove it from the list
        auto prev = this->prev;
        auto next = this->next;

        if (next != nullptr) {
            next->prev = prev;
        }
        if (prev != nullptr) {
            prev->next = next;
        } else {
            // I was the haed so
            free_order_array[this->order] = next;
        }

        // then those values mean nothing anymore
        this->next = nullptr;
        this->prev = nullptr;
    }

    void insert_sbrk() {
        // but I must make sure the list is addr ordered.
        auto * &head = free_order_array[order];

        this->prev = nullptr;
        this->next = nullptr;

        if (head == nullptr || this->as_uptr() < head->as_uptr()) {
            // wethere its there or ntoe
            this->next = head;

            if (head != nullptr) {
                // if actually there im it prev
                head->prev = this;
            }
            head = this;
            return;
        }

        MallocMetaData *current = head;
        // keep walking till reaching somebody greater than me or tail
        while (current->next != nullptr && current->next < this) {
            current = current->next;
        }
        // at this point the invariant is buddy <= current->next so lets sneak in between current and it's next
        this->next = current->next;
        this->prev = current;

        if (current->next != nullptr) {
            current->next->prev = this;
        }

        current->next = this;
    }

    /*
     * mmap operations
     */
    void insert_mmap() {
        // order does not matter on mmap list so:
        if (mmap_list != nullptr) {
            this->next = mmap_list;
            mmap_list->prev = this;
        }

        // im the new head
        mmap_list = this;
    }


    /* buddy */
    static uintptr_t buddy_ptr(uintptr_t ptr, size_t morder) {
        return ptr ^ blk_size(morder);
    }

    // as if it was of this order
    MallocMetaData *buddy_ptr(size_t morder) {
        return from_raw_ptr(
            buddy_ptr(this->as_uptr(), morder)
        );
    }

    MallocMetaData *buddy_ptr() {
        return buddy_ptr(order);
    }
};

MallocMetaData *MallocMetaData::free_order_array[MAX_ORDER + 1];
MallocMetaData *MallocMetaData::mmap_list = nullptr;

static uintptr_t buddy_start = 0;
static uintptr_t buddy_end = 0;

void *init_aligned() {
    // so we need to init 32 blocks of size 128kb and we'll make them aligned
    // where are we at?
    void *ptr = sbrk(0);
    if (ptr == SBRK_FAILURE) {
        return nullptr;
    }
    auto addr = to_uptr(ptr);

    // 0x3[FFFFF] => b[3] = 0011
    constexpr uintptr_t alignment_mask = alignment - 1;

    const uintptr_t residue = addr & alignment_mask;

    uintptr_t padding = 0;
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
    return (void *) (to_uptr(res) + padding);
}


void *smalloc(size_t size) {
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }

    // "lazy init"
    if (has_been_inited == false) {
        auto base_ptr = init_aligned();
        if (base_ptr == nullptr) return nullptr;

        // this is where it starts
        buddy_start = to_uptr(base_ptr);

        // then create all the blocks in the order 10 bucket
        uintptr_t current_ptr = buddy_start;

        for (int i = 0; i < BLOCK_NUM; i++) {
            auto *block = MallocMetaData::init_uptr_ptr(current_ptr, MAX_ORDER);

            // insert the block into the list
            block->insert_sbrk();

            current_ptr += blk_size(MAX_ORDER);
        }

        // this is where it ends
        buddy_end = current_ptr;

        has_been_inited = true;
    }


    size_t rblk_size = size + METADATA_SIZE;

    // by specs then just use mmap directly
    if (rblk_size > blk_size(MAX_ORDER)) {
        void *ptr = mmap(
            nullptr,
            rblk_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        );
        if (ptr == MAP_FAILED) {
            return nullptr;
        }

        // setting order is mmap
        MallocMetaData *metadata = MallocMetaData::init_raw_ptr(ptr, rblk_size);
        metadata->is_free = false;

        // insert to mmap
        metadata->insert_mmap();

        return metadata->as_user_ptr();
    }


    // the difference now is that I want to search orders for something
    // the user wants size bytes, the least I can give him is block_size(0), so I migh as well ask for max(block_size(0), x)
    // and add the metadata size
    // then do the power 2 clipping since I want x <= 2^k where k is the upper bound order to include x.

    size_t min_order = blk_order(rblk_size);
    // since order is 128= 2^7 based, max is just a gurad but is counted for in min block_size
    for (size_t search_order = min_order; search_order <= MAX_ORDER; search_order++) {
        MallocMetaData *current = MallocMetaData::get_order_head(search_order);

        // the list maintains free blocks so hitting something were done
        if (current != nullptr) {
            // it's not free anymore
            current->unlink();

            // cut if you can. the assumption is that sub-buddy is free because we already had the whole thing
            size_t m = search_order;
            // keep cutting
            while (m > min_order) {
                // ok so we got something from the higher order which means x<= 2^k < 2^{k+1}
                // so we are safe to cut in half. we have something of order m=k+1.
                // let's treat it as k=m-1 order and ask for it's buddy starting addr

                MallocMetaData *buddy = current->buddy_ptr(
                    m - 1
                );

                // this guy at buddy is not a free valid block yet so lets init it and add it to the array of order m-1
                // first put the metadata there
                MallocMetaData::init_ptr(buddy, m - 1);
                buddy->is_free = true;

                // we have a new block free of order m-1
                buddy->insert_sbrk();

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


MallocMetaData *buddy_merge(MallocMetaData *blk, size_t limit = MAX_ORDER) {
    size_t order = blk->order;


    // don't merge limit
    while (order < limit) {
        // otherwise let keep merging if we can,
        auto *buddy = blk->buddy_ptr();
        if (buddy->is_free && buddy->order == order) {
            // first we remove it from free array
            buddy->unlink();

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

    // if this was not a mmap then from_usr_ptr is safe, otherwise, we are accessing a freed area, so what to do?
    // we already have the mmaps list, so lets check it
    if (auto *blk = MallocMetaData::find_mmap_block(p)) {
        // then it's mmap
        if (blk->is_free) {
            // if was already free, this should'nt be the case since we remove mmap regions from the list
            return;
        }
        // just to make sure
        if (blk->is_mmap()) {
            // let's keep prev and next since the memory is soon to be freed
            MallocMetaData *prev = blk->prev;
            MallocMetaData *next = blk->next;

            int result = munmap(
                blk,
                blk->data_size()
            );
            // TODO: failure what to do?
            if (result == -1) {
                return;
            }

            //it seems I should've had a double linked list ds
            // if successfull then remove from list
            if (prev != nullptr) {
                prev->next = next;
            } else {
                // i was head
                MallocMetaData::mmap_list = next;
            }
            if (next != nullptr) {
                next->prev = prev;
            }

            return;
        }
    }


    // this is a user addr, so its valid range is [buddy+meta, buddy-meta] which is like
    uintptr_t address = to_uptr(p);


    if (!(buddy_start + METADATA_SIZE <= address &&
          address < buddy_end)) {
        return;
    }

    // otherwise, lets get the sbrk metadata
    auto blk = MallocMetaData::from_user_ptr(p);
    if (blk->is_free) return;

    // why I don't have to update the right buddy metadata? because if blk returned to me the left buddy for example
    // it means merge updated the order of this left block, so in terms of what the block represent in now includes all
    // the buddy data with in it, so now the right buddy metadata is garbage data from the system pov,
    // because now we have a block of double the size so now it's internal data, so we don't worry about updating it
    // but for the user prespective it can do sfree(x) sfree(x) so we need to update the supplied ptr
    blk->is_free = true;

    // buddy merge
    blk = buddy_merge(blk);

    blk->is_free = true;
    // and lets make it available
    blk->insert_sbrk();
}

void *scalloc(size_t num, size_t size) {
    // because here we are in a modulo space where we are over Z/R where R is the maximal size ring,
    // so in the quotient ring [x] = 0 implies x in R, not nessarily that x=0, so...
    if (num == 0 || size == 0) {
        return nullptr;
    }
    if (num > (MAX_SIZE / size)) {
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

void *srealloc_user(void *oldp, size_t size, size_t copy_size) {
    // If I could've merge buddies then it is what it is let smalloc do the job
    void *nptr = smalloc(size);
    if (nptr == nullptr) {
        // failed
        return nullptr;
    }

    // nptr to me here is as if I was a user, not aware of metadata
    std::memmove(nptr, oldp, copy_size);

    // then free the older blk
    sfree(oldp);

    // user ptr
    return nptr;
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

    // this the previous clipped size but without the metadata
    size_t old_size = blk->data_size();
    size_t copy_size = std::min(old_size, size);

    if (blk->is_mmap()) {
        // if same size don't do anything
        if (blk->data_size() == size) {
            return blk->as_user_ptr();
        }
        // then simply give up the current blk and smalloc a new
        // then as a regular allocation we ask for realloc, the return val is pointer
        return srealloc_user(
            oldp,
            size,
            copy_size
        );
    }

    size_t rsize = size + METADATA_SIZE;

    // the user wants rsize, which would live in this minimal target order
    size_t target_order = blk_order(rsize);

    // if this block already can fit the requested size, so don't do anything
    //TODO: check shrinking requirement (do we split?)
    if (blk->order >= target_order) {
        // no need to do anything
        return blk->as_user_ptr();
    }

    // although I'm doing a minimum on the copy size, which is always if reaches here has size > old_size for the
    // non mmap case, i'll keep it for clarity.

    // this shouldnt be the case since it's provided no type crossing but anyway this is the solution
    if (target_order > MAX_ORDER) {
        return srealloc_user(oldp, size, copy_size);
    }


    MallocMetaData *cur = blk;
    size_t order = cur->order;

    // now lets check, can I use buddies?
    while (order < target_order) {
        // otherwise let keep merging if we can,
        auto *buddy = cur->buddy_ptr(order);
        if (
            buddy->is_free
            &&
            //TODO: check is buddy formally defined as someone with the same size?
            buddy->order == order
        ) {
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
        // in which buddy_merge will do


        // in a sane model I would've keept the buddy list but we are limited on memory so we'll just merge again
        cur = buddy_merge(blk, target_order);

        // now after merging i'm going to return maybe a different base pointer, so I need to copy the original data
        auto nptr = cur->as_user_ptr();

        if (nptr != oldp) {
            // copy as if I was a user.
            std::memmove(nptr, oldp, old_size);
        }

        cur->is_free = false;
        // since we defined target_order as the minimum of possible size, and we know we can merge
        // this field shoud've been at this value even before we set it because that what buddy merge shouldve done.
        cur->order = target_order;


        return nptr;
    }


    // otherwise like a regular srealloc
    return srealloc_user(oldp, size, copy_size);
}

struct AllocatorStats {
    size_t free_blocks = 0;
    size_t free_bytes = 0;
    size_t allocated_blocks = 0;
    size_t allocated_bytes = 0;
};

// instead of duplicating the traversal logic everywhere
AllocatorStats collect_stats() {
    AllocatorStats stats;

    if (has_been_inited) {
        uintptr_t current_address = buddy_start;

        while (current_address < buddy_end) {
            auto *block =
                    MallocMetaData::from_raw_ptr(current_address);

            const size_t total_size = block->block_size();
            const size_t usable_size = block->data_size();

            stats.allocated_blocks++;
            stats.allocated_bytes += usable_size;

            if (block->is_free) {
                stats.free_blocks++;
                stats.free_bytes += usable_size;
            }

            current_address += total_size;
        }
    }

    for (
        MallocMetaData *block = MallocMetaData::mmap_list;
        block != nullptr;
        block = block->next
    ) {
        stats.allocated_blocks++;
        stats.allocated_bytes += block->data_size();

        // no free bytes for mmap I guess
    }

    return stats;
}

size_t _num_free_blocks() {
    return collect_stats().free_blocks;
}

size_t _num_free_bytes() {
    return collect_stats().free_bytes;
}

size_t _num_allocated_blocks() {
    return collect_stats().allocated_blocks;
}

size_t _num_allocated_bytes() {
    return collect_stats().allocated_bytes;
}

size_t _size_meta_data() {
    return METADATA_SIZE;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * _size_meta_data();
}
