#pragma once

#pragma warning(push, 0)
#include <stddef.h>

#ifdef USE_STL
#include <unordered_map>
#endif
#pragma warning(pop)

#define NODES_ENABLED 1

struct block_header
{
    block_header *prev;
    block_header *next;
    size_t size;
    bool free;
};

struct free_block
{
    block_header header;

    free_block *left;
    free_block *right;
    free_block *parent;
    bool color;
};

static const bool Red = true;
static const bool Black = false;

#pragma warning(suppress: 4514)
static constexpr bool IsPowerOf2(size_t value)
{
    return value && !(value & (value - 1));
}

static constexpr size_t SnapUpToPow2Increment(size_t value, size_t increment)
{
    return (value + increment - 1) & ~(increment - 1);
}

static constexpr size_t SnapUpToIncrement(size_t value, size_t increment)
{
    return increment * ((value / increment) + (value % increment == 0 ? 0 : 1));
}

#pragma warning(suppress: 4514)
static inline void *SnapUpToPow2Increment(void *value, size_t increment)
{
    return (void *)SnapUpToPow2Increment((size_t)value, increment);
}

template <typename memory_interface, size_t max_alignment=alignof(max_align_t)>
struct free_list_allocator
{
    static_assert(IsPowerOf2(max_alignment), "max_alignment must be a power of 2");
    static constexpr size_t chunk_size = SnapUpToPow2Increment(sizeof(block_header), max_alignment);
    static constexpr size_t free_block_overhead = SnapUpToIncrement(sizeof(free_block), chunk_size);
    static constexpr size_t smallest_valid_free_block = free_block_overhead > (2 * chunk_size) ? free_block_overhead : (2 * chunk_size);

    free_list_allocator(memory_interface *memoryProvider, size_t minimumReservation);
    free_list_allocator(const free_list_allocator &) = delete;
    free_list_allocator() = delete;

    ~free_list_allocator();

    memory_interface *memory_provider;

    void *base;
    size_t mem_reserved;
    size_t mem_committed;

    block_header *first;
    block_header *last;
    free_block *root;

    DECLARE_ALLOCATOR_INTERFACE_METHODS();
    bool IsCommitted(void *addr, size_t size);
    void GetCommitParams(void *requestedAddress, size_t requestedSize, void **paramAddress, size_t *paramSize);
    free_block *FindBestFit(size_t size);
    void *GetAllocationPtr(free_block *block);
    void *GetAllocationPtr(block_header *header);
    free_block *GetBlockHeader(void *addr);
    void RemoveNode(free_block *block);
    void AddNode(free_block *block);
    bool CanSwapNodes(free_block *nodeOld, free_block *nodeNew);
    void SwapNodes(free_block *nodeOld, free_block *nodeNew);
    bool CanChangeNodeSize(free_block *node, size_t newSize);

    // Corruption detection.
    void DetectCorruption();
    void ValidateBST();
    void ValidateFreeListSize();
    void ValidateFreeListLinks();
    void ValidateBSTNodeLinks();
    void ValidateFreeListAllocatorMembers();
#ifdef USE_STL
    void ValidateFreeNodesInTree();
    void ValidateBSTUniqueness();
#endif
    void HeaderIntersectsAny(block_header *header);
};

#if !defined(BM_ASSERT)
#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)
#define BM_ASSERT(val, msg) assert(val)
#endif

template <typename memory_interface, size_t max_alignment>
void *free_list_allocator<memory_interface, max_alignment>::GetAllocationPtr(free_block *block)
{
    return (void *)((uint8_t *)block + chunk_size);
}

template <typename memory_interface, size_t max_alignment>
void *free_list_allocator<memory_interface, max_alignment>::GetAllocationPtr(block_header *block)
{
    return GetAllocationPtr((free_block *)block);
}

template <typename memory_interface, size_t max_alignment>
free_block *free_list_allocator<memory_interface, max_alignment>::GetBlockHeader(void *addr)
{
    return (free_block *)((uint8_t *)addr - chunk_size);
}

inline size_t GetAlignment(void *addr)
{
    size_t data = (size_t)addr;
    return ((data - 1) & ~data) + 1;
}

static void ValidateBSTInternal(free_block *block, size_t min, size_t max)
{
    // BM_ASSERT(block->header.free, "Non-free block in BST");

    if (block->left)
    {
        BM_ASSERT(block->left->header.size < block->header.size, "BST rules broken");
        BM_ASSERT(block->left->header.size >= min, "BST rules broken");
        BM_ASSERT(block->left->header.size <= max, "BST rules broken");

        size_t childMax = block->header.size;
        ValidateBSTInternal(block->left, min, childMax);
    }

    if (block->right)
    {
        BM_ASSERT(block->right->header.size >= block->header.size, "BST rules broken");
        BM_ASSERT(block->right->header.size >= min, "BST rules broken");
        BM_ASSERT(block->right->header.size <= max, "BST rules broken");

        size_t childMin = block->header.size;
        ValidateBSTInternal(block->right, childMin, max);
    }
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateBST()
{
    if (!root)
    {
        return;
    }

    ValidateBSTInternal(root, 0, SIZE_MAX);
}

void ValidateBSTNodeLinksInternal(free_block *node)
{
    if (node == nullptr)
    {
        return;
    }

    BM_ASSERT(!node->left || node->left->parent == node, "BST node is a child but the parent pointer is null");
    BM_ASSERT(!node->right || node->right->parent == node, "BST node is a child but the parent pointer is null");

    ValidateBSTNodeLinksInternal(node->left);
    ValidateBSTNodeLinksInternal(node->right);
}


template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateBSTNodeLinks()
{
    ValidateBSTNodeLinksInternal(root);
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateFreeListSize()
{
    size_t mem = mem_committed;

    for (block_header *header = first;
         header != nullptr;
         header = header->next)
    {
        mem -= (header->size + chunk_size);
    }

    if (mem)
    {
        for (block_header *current = first;
             current != nullptr;
             current = current->next)
        {
            printf("%zu\n", current->size);
        }
    }

    BM_ASSERT(mem == 0, "Internal allocation list leak detected");
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateFreeListLinks()
{
    block_header *prev = nullptr;
    for (block_header *header = first;
         header != nullptr;
         header = header->next)
    {
        BM_ASSERT(prev == header->prev, "Internal allocation list links broken");
        
        prev = header;
    }
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateFreeListAllocatorMembers()
{
#if NODES_ENABLED
    BM_ASSERT(root == nullptr || root->parent == nullptr, "Root node of internal BST can't have a parent");
#endif
    BM_ASSERT(last == nullptr || last->next == nullptr, "Last node in list has a non-null next pointer");
    BM_ASSERT(first == nullptr || first->prev == nullptr, "First node in list has a non-null prev pointer");
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::HeaderIntersectsAny(block_header *header)
{
    size_t begin = (size_t)header;
    size_t end = begin + sizeof(block_header);
    for (block_header *current = first;
         current;
         current = current->next)
    {
        size_t currentBegin = (size_t)current;
        size_t currentEnd = currentBegin + sizeof(block_header);

        if (currentBegin >= begin && currentBegin < end)
        {
            BM_ASSERT(false, "header takes up the space of another header");
        }

        if (currentEnd >= begin && currentEnd < end)
        {
            BM_ASSERT(false, "header takes up the space of another header");
        }
    }
}

void ValidateHeaderIntersection(free_block *a, free_block *b)
{
    size_t beginA = (size_t)a;
    size_t endA = beginA + (a->header.free ? sizeof(free_block) : sizeof(block_header));
    size_t beginB = (size_t)b;
    size_t endB = beginB + (b->header.free ? sizeof(free_block) : sizeof(block_header));

    if (beginB >= beginA && beginB < endA)
    {
        BM_ASSERT(false, "block takes up the space of another block");
    }

    if (endB > beginA && endB <= endA)
    {
        BM_ASSERT(false, "block takes up the space of another block");
    }
}

#ifdef USE_STL

static void ValidateFreeNodesInTreeInternal(std::unordered_set<block_header *> *freeBlocks, free_block *node)
{
    if (!node)
    {
        return;
    }

    if (node->header.free)
    {
        auto it = freeBlocks->find(&node->header);
        BM_ASSERT(it != freeBlocks->end(), "Free block found in BST but not in the allocation list");
        freeBlocks->erase(it);
    }

    ValidateFreeNodesInTreeInternal(freeBlocks, node->left);
    ValidateFreeNodesInTreeInternal(freeBlocks, node->right);
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateFreeNodesInTree()
{
    std::unordered_set<block_header *> freeBlocks;
    for (block_header *current = first;
         current;
         current = current->next)
    {
        if (current->free)
        {
            freeBlocks.insert(current);
        }
    }

    ValidateFreeNodesInTreeInternal(&freeBlocks, root);

    BM_ASSERT(freeBlocks.empty(), "Free blocks found in the allocation list but not in the BST");
}

static void ValidateBSTUniquenessInternal(std::unordered_set<free_block *> *set, free_block *node)
{
    if (!node)
    {
        return;
    }

    BM_ASSERT(set->find(node) == set->end(), "Duplicate node found in BST");
    set->insert(node);
    ValidateBSTUniquenessInternal(set, node->left);
    ValidateBSTUniquenessInternal(set, node->right);
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::ValidateBSTUniqueness()
{
    std::unordered_set<free_block *> set;
    ValidateBSTUniquenessInternal(&set, root);
}

#endif

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::DetectCorruption()
{
    ValidateFreeListLinks();
    ValidateFreeListSize();
    ValidateFreeListAllocatorMembers();

#if NODES_ENABLED
    ValidateBSTNodeLinks();
    ValidateBST();

#ifdef USE_STL
    ValidateFreeNodesInTree();
    ValidateBSTUniqueness();
#endif
#endif
}

template <typename memory_interface, size_t max_alignment>
free_list_allocator<memory_interface, max_alignment>::free_list_allocator(
    memory_interface *memoryProvider,
    size_t minimumReservation) :
    memory_provider(memoryProvider)
{
    base = memory_provider->Reserve(minimumReservation, &mem_reserved);
    BM_ASSERT(base, "Failed to reserve memory");

    size_t pageSize = memory_provider->GetPageSize();

    // Commit the first page.
    memory_provider->Commit(base, pageSize, &mem_committed);

    BM_ASSERT(pageSize >= sizeof(block_header), "The OS page size is smaller than a link in the internal list. The memory interface is probably not reporting an accurate page size");
    BM_ASSERT(GetAlignment(base) >= alignof(block_header), "");

    // Initialize the first block.
    root = (free_block *)base;
    root->header.prev = nullptr;
    root->header.next = nullptr;
    root->header.size = mem_committed - chunk_size;
    root->header.free = true;
    root->left = nullptr;
    root->right = nullptr;
    root->parent = nullptr;
    root->color = Red;

    first = &root->header;
    last = first;
}

template<typename memory_interface, size_t max_alignment>
free_list_allocator<memory_interface, max_alignment>::~free_list_allocator()
{
    memory_provider->DeCommit(base, mem_committed);
    memory_provider->Release(base);
}

template <typename memory_interface, size_t max_alignment>
bool free_list_allocator<memory_interface, max_alignment>::IsCommitted(void *addr, size_t size)
{
    size_t addrEnd = (size_t)addr + size;
    size_t maxCommitted = (size_t)base + mem_committed;

    return addrEnd <= maxCommitted;
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::GetCommitParams(void *requestedAddress, size_t requestedSize, void **paramAddress, size_t *paramSize)
{
    uint8_t *commitEnd = (uint8_t *)base + mem_committed;
    size_t difference = commitEnd - (uint8_t *)base;

    size_t unCommitted = requestedSize - difference;

    *paramAddress = (void *)commitEnd;
    *paramSize = unCommitted;
}

template <typename memory_interface, size_t max_alignment>
bool free_list_allocator<memory_interface, max_alignment>::CanSwapNodes(free_block *nodeOld, free_block *nodeNew)
{
    // returns true if nodeNew can be swapped with nodeOld in the red black tree without
    // breaking the BST rules.
    
    return CanChangeNodeSize(nodeOld, nodeNew->header.size);
}

template <typename memory_interface, size_t max_alignment>
bool free_list_allocator<memory_interface, max_alignment>::CanChangeNodeSize(free_block *node, size_t newSize)
{
#if NODES_ENABLED
    if (node->left)
    {
        return false;
    }

    if (node->right)
    {
        return false;
    }
    
    if (node->parent)
    {
        free_block *parent = node->parent;
        
        if (parent->left == node) // left node
        {
            if (newSize > parent->header.size)
            {
                return false;
            }

            if (parent->parent)
            {
                if (parent->parent->left == parent)
                {
                    return newSize > parent->parent->header.size;
                }
                else
                {
                    return false;
                }
            }
        }
        else // right node
        {
            if (newSize <= parent->header.size)
            {
                return false;
            }

            if (parent->parent)
            {
                if (parent->parent->right == parent)
                {
                    return newSize < parent->parent->header.size;
                }
                else
                {
                    return false;
                }
            }
        }
    }

    return true;
#else
    (void)node;
    (void)newSize;
    return false;
#endif
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::SwapNodes(free_block *nodeOld, free_block *nodeNew)
{
#if NODES_ENABLED
    if (nodeOld->parent)
    {
        bool isLeft = nodeOld->parent->left == nodeOld;
        if (isLeft)
        {
            nodeOld->parent->left = nodeNew;
        }
        else
        {
            nodeOld->parent->right = nodeNew;
        }
    }

    if (root == nodeOld)
    {
        root = nodeNew;
    }

    nodeNew->parent = nodeOld->parent;
    nodeNew->left = nodeOld->left;
    nodeNew->right = nodeOld->right;
#else
    (void)nodeOld;
    (void)nodeNew;
    BM_ASSERT(false, "");
#endif
}

template <typename memory_interface, size_t max_alignment>
void *free_list_allocator<memory_interface, max_alignment>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    (void)line;
    (void)file;
    BM_ASSERT(alignment <= max_alignment, "Tried to allocate with an alignment greater than the maximum supported alignment");
    BM_ASSERT(size > 0, "Tried to allocate 0 bytes.");

    size = SnapUpToIncrement(size, chunk_size);

    // find the smallest chunk that can fit this allocation.
    free_block *bestFit = FindBestFit(size);
    if (bestFit == nullptr)
    {
        // No suitable block!
        // We have to commit more pages for this allocation
        
        uint8_t *unCommitted = (uint8_t *)base + mem_committed;

        if (last->free)
        {
            free_block *lastFree = (free_block *)last;
            size_t requiredSize = size - last->size;

            // Can't commit more than we have reserved.
            BM_ASSERT((requiredSize + mem_committed) <= mem_reserved, "Tried to commit more memory than reserved");

            // Add the new committed pages to the last block.
            size_t actualCommit;
            memory_provider->Commit(unCommitted, requiredSize, &actualCommit);
            mem_committed += actualCommit;

            size_t newSize = actualCommit + last->size;
            if (CanChangeNodeSize(lastFree, newSize))
            {
                lastFree->header.size = newSize;
            }
            else
            {
                RemoveNode(lastFree);
                lastFree->header.size = newSize;
                AddNode(lastFree);
            }

            bestFit = lastFree;
        }
        else
        {
            size_t requiredSize = size + chunk_size; // One chunk for the block_header struct.

            // Can't commit more than we have reserved.
            BM_ASSERT((requiredSize + mem_committed) <= mem_reserved, "Tried to commit more memory than reserved");

            size_t actualCommit;
            memory_provider->Commit(unCommitted, requiredSize, &actualCommit);
            mem_committed += actualCommit;

            free_block *newBlock = (free_block *)unCommitted;
            newBlock->header.size = actualCommit - chunk_size;
            newBlock->header.free = true;
            newBlock->header.prev = last;
            newBlock->header.next = nullptr;
            last->next = &newBlock->header;
            last = &newBlock->header;

            AddNode(newBlock);

            bestFit = newBlock;
        }
    }

    void *allocation = GetAllocationPtr(&bestFit->header);

    // Mark this block as used.
    bestFit->header.free = false;

    size_t leftover = bestFit->header.size - size;

    // Only add a new node if we have enough space for a free_block struct and
    // atleast one chunk.
    if (leftover >= smallest_valid_free_block)
    {
        // add a new node to the tree and a new header to the list for the leftover memory        
        free_block *newBlock = (free_block *)((uint8_t *)allocation + size);

        newBlock->header.free = true;
        newBlock->header.prev = &bestFit->header;
        if (bestFit->header.next && bestFit->header.next->free)
        {
            // coalesce the two blocks
            free_block *toCombine = (free_block *)bestFit->header.next;
            newBlock->header.next = toCombine->header.next;
            if (toCombine->header.next) toCombine->header.next->prev = (block_header *)newBlock;
            newBlock->header.size = leftover + toCombine->header.size;

            // Try swapping in the new node with the old coalesced blocks node
            if (!CanSwapNodes(toCombine, newBlock))
            {
                RemoveNode(toCombine);

                // now try swapping with the node that we just allocated from.
                if (!CanSwapNodes(bestFit, newBlock))
                {
                    // Nothing could be swapped :(
                    RemoveNode(bestFit);
                    AddNode(newBlock);
                }
                else
                {
                    SwapNodes(bestFit, newBlock);
                }
            }
            else
            {
                SwapNodes(toCombine, newBlock);
                RemoveNode(bestFit);
            }

            ValidateHeaderIntersection(bestFit, newBlock);
            ValidateHeaderIntersection(bestFit, toCombine);
            ValidateHeaderIntersection(newBlock, toCombine);
        }
        else
        {
            ValidateHeaderIntersection(newBlock, bestFit);

            if (bestFit->header.next) bestFit->header.next->prev = (block_header *)newBlock;
            newBlock->header.next = bestFit->header.next;
            newBlock->header.size = leftover - chunk_size;
            if (CanSwapNodes(bestFit, newBlock))
            {
                SwapNodes(bestFit, newBlock);
            }
            else
            {
                RemoveNode(bestFit);
                AddNode(newBlock);
            }
        }

        // reduce the size of the allocation
        bestFit->header.size = size;
        bestFit->header.next = (block_header *)newBlock;

        if (newBlock->header.next == nullptr)
        {
            // The new block is the last in the list.
            last = &newBlock->header;
        }
    }
    else
    {
        // Remove the free_block from the red-black tree.
        RemoveNode(bestFit);
    }

    return allocation;
}

template <typename memory_interface, size_t max_alignment>
void *free_list_allocator<memory_interface, max_alignment>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    (void)line;
    (void)file;
    block_header *header = (block_header *)((uint8_t *)addr - chunk_size);
    size = SnapUpToIncrement(size, chunk_size);

    if (size <= header->size)
    {
        return GetAllocationPtr(header);
    }

    block_header *current = header->next;
    size_t total = header->size;
    int needed = 0;
    for (;;)
    {
        if (current == nullptr || !current->free)
        {
            return nullptr;
        }

        total += current->size + chunk_size;

        if (total >= size)
        {
            // There are enough free blocks to satisfy this request.
            // Remove all nodes up to this one.
            for (block_header *toRemove = header->next;
                 toRemove && (size_t)toRemove <= (size_t)current;
                 toRemove = toRemove->next)
            {
                RemoveNode((free_block *)toRemove);
            }

            // calculate the required amount of bytes that we need from this block.
            size_t required = size - (total - (current->size + chunk_size));
            size_t leftover = (current->size + chunk_size) - required;

            if (leftover >= smallest_valid_free_block)
            {
                // There is enough leftover memory that we should add a new free node for it,
                // or move back the next free node.

                header->size = size;
                free_block *newBlock = (free_block *)((uint8_t *)current + required);
                
                newBlock->header.free = true;
                newBlock->header.prev = header;
                newBlock->header.next = current->next;
                header->next = &newBlock->header;
                if (current->next) current->next->prev = &newBlock->header;

                if (current->next && current->next->free)
                {
                    // The next block was free, so we should increase it's size.
                    free_block *nextBlock = (free_block *)current->next;
                    size_t newSize = leftover + nextBlock->header.size;
                    newBlock->header.size = newSize;
                }
                else
                {
                    newBlock->header.size = leftover - chunk_size;
                }

                if (newBlock->header.next == nullptr)
                {
                    last = &newBlock->header;
                }

                AddNode((free_block *)newBlock);
            }
            else
            {
                // nothing to add.
                header->size = total;
                header->next = current->next;
                if (current->next) current->next->prev = header;

                if (current == last)
                {
                    last = header;
                }
            }

            break;
        }

        current = current->next;
        ++needed;
    }

    return addr;
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::FreeInternal(void *addr, int line, const char *file)
{
    (void)line;
    (void)file;
    block_header *header = (block_header *)((uint8_t *)addr - chunk_size);
    BM_ASSERT(!header->free, "Trying to free an already free block.");
    header->free = true;

    if (header->prev && header->prev->free)
    {
        if (header->next && header->next->free)
        {
            // Coalesce all 3 blocks.
            block_header *prevHeader = header->prev;
            block_header *nextHeader = header->next;

            free_block *prevBlock = (free_block *)prevHeader;
            free_block *nextBlock = (free_block *)nextHeader;
            
            size_t newSize = prevHeader->size + header->size + nextHeader->size + (2 * chunk_size); // Reclaiming two chunks from the header and the nextHeader.
            if (CanChangeNodeSize(prevBlock, newSize))
            {
                // Update node in place if possible, and remove nextBlock from the tree.
                prevHeader->size = newSize;
                RemoveNode(nextBlock);
            }
            else
            {
                // Couldn't update prevBlock in place. Remove it from the tree.
                RemoveNode(prevBlock);
                prevHeader->size = newSize;

                // nextBlock can be removed from the tree, so try to swap it with the block that we just added.
                if (CanSwapNodes(nextBlock, prevBlock))
                {
                    SwapNodes(nextBlock, prevBlock);
                }
                else
                {
                    // Could not swap with the nextBlock.
                    RemoveNode(nextBlock);
                    AddNode(prevBlock);
                }
            }

            prevHeader->size = newSize;
            prevHeader->next = nextHeader->next;
            if (nextHeader->next) nextHeader->next->prev = prevHeader;

            if (prevHeader->next == nullptr)
            {
                // The new coalesced block is the last in the list.
                last = prevHeader;
            }

            return;
        }
        else
        {
            // Coalesce the two blocks
            block_header *prevHeader = header->prev;
            free_block *prevBlock = (free_block *)prevHeader;

            // try to update the current nodes size without having to re-insert into the tree.
            size_t newSize = prevHeader->size + header->size + chunk_size;
            if (CanChangeNodeSize(prevBlock, newSize))
            {
                prevBlock->header.size = newSize;
            }
            else
            {
                RemoveNode(prevBlock);
                prevBlock->header.size = newSize;
                AddNode(prevBlock);
            }

            prevHeader->next = header->next;
            if (header->next) header->next->prev = prevHeader;

            if (prevHeader->next == nullptr)
            {
                // The new coalesced block is the last in the list.
                last = prevHeader;
            }
        }
    }
    else if (header->next && header->next->free)
    {
        // Coalesce the two blocks
        free_block *block = (free_block *)header;
        block_header *nextHeader = header->next;
        free_block *nextBlock = (free_block *)nextHeader;

        // try to swap nodes in the tree with nextBlock to avoid re-inserting into the tree.
        size_t newSize = nextHeader->size + header->size + chunk_size;
        header->size = newSize;
        if (CanSwapNodes(nextBlock, block))
        {
            SwapNodes(nextBlock, block);
        }
        else
        {
            RemoveNode(nextBlock);
            AddNode(block);
        }

        header->next = nextHeader->next;
        if (nextHeader->next) nextHeader->next->prev = header;

        if (header->next == nullptr)
        {
            last = header;
        }
    }
    else
    {
        // Couldn't coalesce any blocks.
        // Add the new free block to the tree.
        AddNode((free_block *)header);
    }  
}

template <typename memory_interface, size_t max_alignment>
free_block *free_list_allocator<memory_interface, max_alignment>::FindBestFit(size_t size)
{
    #if NODES_ENABLED
    free_block *current = root;
    free_block *lastValid = nullptr;
    for (;;)
    {
        if (current == nullptr)
        {
            return lastValid;
        }

        if (current->header.size < size)
        {
            current = current->right;
        }
        else
        {
            lastValid = current;
            current = current->left;
        }
    }
    #else
    for (block_header *current = first;
         current;
         current = current->next)
    {
        if (size <= current->size)
        {
            return (free_block *)current;
        }
    }

    return nullptr;
    #endif
}

static int counter = 0;

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::AddNode(free_block *block)
{
#if NODES_ENABLED
    block->left = nullptr;
    block->right = nullptr;

    if (!root)
    {
        root = block;
        block->parent = nullptr;
        return;
    }

    free_block *current = root;
    for (;;)
    {
        if (block->header.size >= current->header.size)
        {
            if (current->right == nullptr)
            {
                current->right = block;
                block->parent = current;
                return;
            }

            current = current->right;
        }
        else
        {
            if (current->left == nullptr)
            {
                current->left = block;
                block->parent = current;
                return;
            }

            current = current->left;
        }
    }
#else
    (void)block;
#endif
}

template <typename memory_interface, size_t max_alignment>
void free_list_allocator<memory_interface, max_alignment>::RemoveNode(free_block *block)
{
#if NODES_ENABLED
    free_block **ptrFromParent;
    if (root == block)
    {
        ptrFromParent = &root;
    }
    else
    {
        if (block->parent->left == block)
        {
            ptrFromParent = &block->parent->left;
        }
        else
        {
            ptrFromParent = &block->parent->right;
        }
    }

    free_block *toReplace;
    if (block->left && block->right)
    {
        toReplace = block->right;
        if (toReplace->left)
        {
            do
            {
                toReplace = toReplace->left;
            }
            while (toReplace->left);

            toReplace->parent->left = toReplace->right;
            if (toReplace->right)
            {
                toReplace->right->parent = toReplace->parent;
            }

            toReplace->parent = block->parent;
            toReplace->left = block->left;
            block->left->parent = toReplace;
            toReplace->right = block->right;
            block->right->parent = toReplace;
        }
        else
        {
            toReplace->left = block->left;
            block->left->parent = toReplace;
            toReplace->parent = block->parent;
        }
    }
    else if (block->left)
    {
        toReplace = block->left;
        block->left->parent = block->parent;
    }
    else if (block->right)
    {
        toReplace = block->right;
        block->right->parent = block->parent;
    }
    else
    {
        toReplace = nullptr;
    }

    if (root == block)
    {
        root = toReplace;
    }
    else
    {
        if (block->parent->left == block)
        {
            block->parent->left = toReplace;
        }
        else
        {
            block->parent->right = toReplace;
        }
    }
#else
    (void)block;
#endif
}
