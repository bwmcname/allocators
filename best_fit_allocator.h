#pragma once

#include "memory_interface.h"
#include "allocator_interface.h"

#pragma warning(push, 0)
#ifdef USE_STL
#include <unordered_map>
#endif
#pragma warning(pop)

#define NODES_ENABLED 0

#ifdef _MSC_VER
#define BM_RESTRICT __restrict
#endif

using rb_color = bool;

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

    free_block *GetParent();
    void SetParent(free_block *parent);

    rb_color GetColor();
    void SetColor(rb_color color);

private:
    free_block *parent;
};

free_block *free_block::GetParent()
{
    return (free_block *)((size_t)parent & ~1llu);
}

void free_block::SetParent(free_block *newParent)
{
    parent = (free_block *)((size_t)newParent | ((size_t)parent & 1llu));
}

rb_color free_block::GetColor()
{
    return (size_t)parent & 1llu;
}

void free_block::SetColor(rb_color color)
{
    parent = (free_block *)(((size_t)parent & ~1llu) | (size_t)color);
}

static const rb_color Red = true;
static const rb_color Black = false;

#pragma warning(suppress: 4514)
static constexpr bool IsPowerOf2(size_t value)
{
    return value && !(value & (value - 1));
}

#pragma warning(suppress: 4514)
static constexpr size_t SnapUpToPow2Increment(size_t value, size_t increment)
{
    return (value + increment - 1) & ~(increment - 1);
}

#pragma warning(suppress: 4514)
static constexpr size_t SnapUpToIncrement(size_t value, size_t increment)
{
    return increment * ((value / increment) + (value % increment == 0 ? 0 : 1));
}

#pragma warning(suppress: 4514)
static inline void *SnapUpToPow2Increment(void *value, size_t increment)
{
    return (void *)SnapUpToPow2Increment((size_t)value, increment);
}

template <typename memory_interface, size_t max_alignment=16>
struct best_fit_allocator
{
    static_assert(IsPowerOf2(max_alignment), "max_alignment must be a power of 2");

    // The red/black bit is stored in the lsb of the parent pointer of a free_block,
    // so we must ensure that all free_block addresses are aligned by atleast 2.
    // the free bit is also stored in the lsb of the prev pointer in the block_header.
    static_assert(max_alignment > 1, "max_alignment must be atleast 2");
    static constexpr size_t chunk_size = SnapUpToPow2Increment(sizeof(block_header), max_alignment);
    static constexpr size_t free_block_overhead = SnapUpToIncrement(sizeof(free_block), chunk_size);
    static constexpr size_t smallest_valid_free_block = free_block_overhead > (2 * chunk_size) ? free_block_overhead : (2 * chunk_size);

    best_fit_allocator(memory_interface *memoryProvider, size_t minimumReservation);
    best_fit_allocator(const best_fit_allocator &) = delete;
    best_fit_allocator() = delete;

    ~best_fit_allocator();

    DECLARE_ALLOCATOR_INTERFACE_METHODS();
    
    // Corruption detection.
    void DetectCorruption();
    void ValidateBST();
    void ValidateFreeListSize();
    void ValidateFreeListLinks();
    void ValidateBSTNodeLinks();
    void ValidateFreeListAllocatorMembers();
    void ValidateRedBlackProperties();
#ifdef USE_STL
    void ValidateFreeNodesInTree();
    void ValidateBSTUniqueness();
#endif

private:
    memory_interface *memory_provider;

    void *base;
    size_t mem_reserved;
    size_t mem_committed;

    block_header *first;
    block_header *last;
    free_block *root;

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
    void LeftRotate(free_block *node);
    void RightRotate(free_block *node);
    void HeaderIntersectsAny(block_header *header);
};

#if !defined(BM_ASSERT)
#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)
#define BM_ASSERT(val, msg) assert(val)
#endif

#ifdef BM_BEST_FIT_ALLOCATOR_IMPLEMENTATION
void ValidateBSTInternal(free_block *block, size_t min, size_t max)
{
    // BM_ASSERT(block->header.free, "Non-free block in BST");

    if (block->left)
    {
        BM_ASSERT(block->left->header.size <= block->header.size, "BST rules broken");
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

int ValidateRedBlackNodeInternal(free_block *node)
{
    if (node == nullptr)
    {
        return 0;
    }

    int crossedBlack;

    if (node->GetColor() == Red)
    {
        BM_ASSERT(!node->left || node->left->GetColor() == Black, "Red node's children not black.");
        BM_ASSERT(!node->right || node->right->GetColor() == Black, "Red node's children not black.");
        crossedBlack = 0;
    }
    else
    {
        crossedBlack = 1;
    }

    int leftBlacks = ValidateRedBlackNodeInternal(node->left);
    int rightBlacks = ValidateRedBlackNodeInternal(node->right);

    BM_ASSERT(leftBlacks == rightBlacks, "Number of black nodes in path from leafs not equal.");
    return leftBlacks + crossedBlack;
}

#ifdef USE_STL

void ValidateFreeNodesInTreeInternal(std::unordered_set<block_header *> *freeBlocks, free_block *node)
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


void ValidateBSTUniquenessInternal(std::unordered_set<free_block *> *set, free_block *node)
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

#endif
#endif

template <typename memory_interface, size_t max_alignment>
void *best_fit_allocator<memory_interface, max_alignment>::GetAllocationPtr(free_block *block)
{
    return (void *)((uint8_t *)block + chunk_size);
}

template <typename memory_interface, size_t max_alignment>
void *best_fit_allocator<memory_interface, max_alignment>::GetAllocationPtr(block_header *block)
{
    return GetAllocationPtr((free_block *)block);
}

template <typename memory_interface, size_t max_alignment>
free_block *best_fit_allocator<memory_interface, max_alignment>::GetBlockHeader(void *addr)
{
    return (free_block *)((uint8_t *)addr - chunk_size);
}

#pragma warning(suppress: 4514)
inline size_t GetAlignment(void *addr)
{
    size_t data = (size_t)addr;
    return ((data - 1) & ~data) + 1;
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateBST()
{
    if (!root)
    {
        return;
    }

    ValidateBSTInternal(root, 0, SIZE_MAX);
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateRedBlackProperties()
{
    BM_ASSERT(root->color == Black, "Root node of a red black tree must be black.");

    ValidateRedBlackNodeInternal(root);
}

void ValidateBSTNodeLinksInternal(free_block *node)
{
    if (node == nullptr)
    {
        return;
    }

    BM_ASSERT(!node->left || node->left->GetParent() == node, "BST node is a child but the parent pointer is null");
    BM_ASSERT(!node->right || node->right->GetParent() == node, "BST node is a child but the parent pointer is null");

    ValidateBSTNodeLinksInternal(node->left);
    ValidateBSTNodeLinksInternal(node->right);
}


template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateBSTNodeLinks()
{
    ValidateBSTNodeLinksInternal(root);
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateFreeListSize()
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
void best_fit_allocator<memory_interface, max_alignment>::ValidateFreeListLinks()
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
void best_fit_allocator<memory_interface, max_alignment>::ValidateFreeListAllocatorMembers()
{
#if NODES_ENABLED
    BM_ASSERT(root == nullptr || root->GetParent() == nullptr, "Root node of internal BST can't have a parent");
#endif
    BM_ASSERT(last == nullptr || last->next == nullptr, "Last node in list has a non-null next pointer");
    BM_ASSERT(first == nullptr || first->prev == nullptr, "First node in list has a non-null prev pointer");
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::HeaderIntersectsAny(block_header *header)
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


template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateFreeNodesInTree()
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

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::ValidateBSTUniqueness()
{
    std::unordered_set<free_block *> set;
    ValidateBSTUniquenessInternal(&set, root);
}

#endif

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::DetectCorruption()
{
    ValidateFreeListLinks();
    ValidateFreeListSize();
    ValidateFreeListAllocatorMembers();

#if NODES_ENABLED
    ValidateBSTNodeLinks();
    ValidateBST();
    ValidateRedBlackProperties();

#ifdef USE_STL
    ValidateFreeNodesInTree();
    ValidateBSTUniqueness();
#endif
#endif
}

template <typename memory_interface, size_t max_alignment>
best_fit_allocator<memory_interface, max_alignment>::best_fit_allocator(
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
    root->SetParent(nullptr);
    root->SetColor(Black);

    first = &root->header;
    last = first;
}

template<typename memory_interface, size_t max_alignment>
best_fit_allocator<memory_interface, max_alignment>::~best_fit_allocator()
{
    memory_provider->DeCommit(base, mem_committed);
    memory_provider->Release(base);
}

template <typename memory_interface, size_t max_alignment>
bool best_fit_allocator<memory_interface, max_alignment>::IsCommitted(void *addr, size_t size)
{
    size_t addrEnd = (size_t)addr + size;
    size_t maxCommitted = (size_t)base + mem_committed;

    return addrEnd <= maxCommitted;
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::GetCommitParams(void *requestedAddress, size_t requestedSize, void **paramAddress, size_t *paramSize)
{
    uint8_t *commitEnd = (uint8_t *)base + mem_committed;
    size_t difference = commitEnd - (uint8_t *)base;

    size_t unCommitted = requestedSize - difference;

    *paramAddress = (void *)commitEnd;
    *paramSize = unCommitted;
}

template <typename memory_interface, size_t max_alignment>
bool best_fit_allocator<memory_interface, max_alignment>::CanSwapNodes(free_block *nodeOld, free_block *nodeNew)
{
    // returns true if nodeNew can be swapped with nodeOld in the red black tree without
    // breaking the BST rules.
    
    return CanChangeNodeSize(nodeOld, nodeNew->header.size);
}

template <typename memory_interface, size_t max_alignment>
bool best_fit_allocator<memory_interface, max_alignment>::CanChangeNodeSize(free_block *node, size_t newSize)
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
    
    if (node->GetParent())
    {
        free_block *parent = node->GetParent();
        
        if (parent->left == node) // left node
        {
            if (newSize > parent->header.size)
            {
                return false;
            }

            if (parent->GetParent())
            {
                if (parent->GetParent()->left == parent)
                {
                    return newSize > parent->GetParent()->header.size;
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

            if (parent->GetParent())
            {
                if (parent->GetParent()->right == parent)
                {
                    return newSize < parent->GetParent()->header.size;
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
void best_fit_allocator<memory_interface, max_alignment>::SwapNodes(free_block *nodeOld, free_block *nodeNew)
{
#if NODES_ENABLED
    if (nodeOld->GetParent())
    {
        bool isLeft = nodeOld->GetParent()->left == nodeOld;
        if (isLeft)
        {
            nodeOld->GetParent()->left = nodeNew;
        }
        else
        {
            nodeOld->GetParent()->right = nodeNew;
        }
    }

    if (root == nodeOld)
    {
        root = nodeNew;
    }

    nodeNew->SetParent(nodeOld->GetParent());
    nodeNew->left = nodeOld->left;
    nodeNew->right = nodeOld->right;
    nodeNew->SetColor(nodeOld->GetColor());
#else
    (void)nodeOld;
    (void)nodeNew;
    BM_ASSERT(false, "");
#endif
}

template <typename memory_interface, size_t max_alignment>
void *best_fit_allocator<memory_interface, max_alignment>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
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
        }
        else
        {
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
void *best_fit_allocator<memory_interface, max_alignment>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
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
void best_fit_allocator<memory_interface, max_alignment>::FreeInternal(void *addr, int line, const char *file)
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
free_block *best_fit_allocator<memory_interface, max_alignment>::FindBestFit(size_t size)
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
    block_header *lowest = nullptr;
    for (block_header *current = first;
         current;
         current = current->next)
    {
        if (size <= current->size)
        {
            if (lowest)
            {
                if (current->size < lowest->size)
                {
                    lowest = current;
                }
            }
            else
            {
                lowest = current;
            }
        }
    }

    return (free_block *)lowest;
    #endif
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::LeftRotate(free_block *node)
{
    free_block *rotator = node->right;
    node->right = rotator->left;
    if (rotator->left) rotator->left->SetParent(node);
    rotator->SetParent(node->GetParent());
    if (node == root)
    {
        root = rotator;
    }
    else if (node == node->GetParent()->left)
    {
        node->GetParent()->left = rotator;
    }
    else // node is right child
    {
        node->GetParent()->right = rotator;
    }

    rotator->left = node;
    node->SetParent(rotator);
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::RightRotate(free_block *node)
{
    free_block *rotator = node->left;
    node->left = rotator->right;
    if (rotator->right) rotator->right->SetParent(node);
    rotator->SetParent(node->GetParent());
    if (node == root)
    {
        root = rotator;
    }
    else if (node == node->GetParent()->right)
    {
        node->GetParent()->right = rotator;
    }
    else // node is left child
    {
        node->GetParent()->left = rotator;
    }
    rotator->right = node;
    node->SetParent(rotator);
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::AddNode(free_block *block)
{
#if NODES_ENABLED
    // Add the node
    block->left = nullptr;
    block->right = nullptr;

    if (!root)
    {
        root = block;
        block->SetParent(nullptr);
        block->SetColor(Black);
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
                block->SetParent(current);
                break;
            }

            current = current->right;
        }
        else
        {
            if (current->left == nullptr)
            {
                current->left = block;
                block->SetParent(current);
                break;
            }

            current = current->left;
        }
    }

    // Do rb tree balance operations
    block->SetColor(Red);
    current = block;
    while (current && current->GetParent() && current->GetParent()->GetColor() == Red)
    {
        BM_ASSERT(current->GetColor() == Red, "Expected Red Node");

        free_block *uncle;
        bool parentIsLeft;
        if (current->GetParent()->GetParent())
        {
            parentIsLeft = current->GetParent()->GetParent()->left == current->GetParent();
            if (parentIsLeft)
            {
                uncle = current->GetParent()->GetParent()->right;
            }
            else
            {
                uncle = current->GetParent()->GetParent()->left;
            }
        }
        else
        {
            // no grandparent, no rebalancing needed
            return;
        }

        if (uncle && uncle->GetColor() == Red)
        {
            // color flip case
            // Mark both the parent and uncle as black
            // The grandparent is now red
            current->GetParent()->SetColor(Black);
            uncle->SetColor(Black);
            if (current->GetParent()->GetParent() != root) current->GetParent()->GetParent()->SetColor(Red);
            current = current->GetParent()->GetParent();
        }
        else // uncle is black
        {
            bool newIsLeft = current->GetParent()->left == current;
            current = current->GetParent();
            if (parentIsLeft)
            {
                if (!newIsLeft)
                {
                    LeftRotate(current);
                    current = current->GetParent();
                }

                RightRotate(current->GetParent());
            }
            else // parent is a right  child
            {
                if (newIsLeft)
                {
                    RightRotate(current);
                    current = current->GetParent();
                }

                LeftRotate(current->GetParent());
            }

            if (current->left) current->left->SetColor(Red);
            if (current->right) current->right->SetColor(Red);
            current->SetColor(Black);
            return;
        }
    }

#else
    (void)block;
#endif
}

template <typename memory_interface, size_t max_alignment>
void best_fit_allocator<memory_interface, max_alignment>::RemoveNode(free_block *block)
{
#if NODES_ENABLED

    // Will need these later for rebalancing
    free_block *doubleBlack = nullptr;
    free_block *dbParent = nullptr;
    bool dbIsLeft = false;
    bool toReplaceWasLeaf = false;

    // Perform the standard BST removal
    free_block *toReplace;
    if (block->left && block->right)
    {
        toReplace = block->left;
        if (toReplace->right)
        {
            do
            {
                toReplace = toReplace->right;
            }
            while (toReplace->right);

            toReplaceWasLeaf = !toReplace->left && !toReplace->right;

            // Set the double black position before we reposition the toReplace node.
            dbParent = toReplace->GetParent();
            dbIsLeft = false;

            toReplace->GetParent()->right = toReplace->left;
            if (toReplace->left)
            {
                toReplace->left->SetParent(toReplace->GetParent());
            }

            toReplace->SetParent(block->GetParent());
            toReplace->right = block->right;
            block->right->SetParent(toReplace);
            toReplace->left = block->left;
            block->left->SetParent(toReplace);
        }
        else
        {
            toReplaceWasLeaf = !toReplace->left && !toReplace->right;

            // Set the double black position before we reposition the toReplace node.
            dbParent = toReplace;
            dbIsLeft = true;

            toReplace->right = block->right;
            block->right->SetParent(toReplace);
            toReplace->SetParent(block->GetParent());
        }
    }
    else if (block->left)
    {
        dbParent = block->GetParent();
        dbIsLeft = true;

        toReplace = block->left;
        toReplaceWasLeaf = !toReplace->left && !toReplace->right;
        block->left->SetParent(block->GetParent());
    }
    else if (block->right)
    {
        dbParent = block->GetParent();
        dbIsLeft = false;

        toReplace = block->right;
        toReplaceWasLeaf = !toReplace->left && !toReplace->right;
        block->right->SetParent(block->GetParent());
    }
    else
    {
        dbParent = block->GetParent();
        if (dbParent)
        {
            dbIsLeft = dbParent->left == block;
        }
        
        toReplace = nullptr;
    }

    if (root == block)
    {
        root = toReplace;
    }
    else
    {
        if (block->GetParent()->left == block)
        {
            block->GetParent()->left = toReplace;
        }
        else
        {
            block->GetParent()->right = toReplace;
        }
    }

    if (root == nullptr)
    {
        // We must have removed the root.
        return;
    }

    // rb tree fixup
    if ((!toReplace && block->GetColor() == Red) ||
        (toReplace && toReplace->GetColor() == Red && toReplaceWasLeaf))
    {
        if (block->GetColor() == Black)
        {
            toReplace->SetColor(Black);
        }

        // case 1.
        // Deleting a red leaf, no fixup needed
        return;
    }

    if (doubleBlack && !doubleBlack->left && !doubleBlack->right)
    {
        doubleBlack->SetColor(Black);
        return;
    }

    if (toReplace) toReplace->SetColor(block->GetColor());

    for (;;)
    {
        if (doubleBlack == root)
        {
            // case 2.
            doubleBlack->SetColor(Black);
            break;
        }
        
        free_block *sibling = dbIsLeft ? dbParent->right : dbParent->left;

        if (!sibling ||
            (sibling->GetColor() == Black &&
             (!sibling->left || sibling->left->GetColor() == Black) &&
             (!sibling->right || sibling->right->GetColor() == Black)))
        {
            // case 3.
            if (sibling) sibling->SetColor(Red);
            if (dbParent->GetColor() == Black)
            {
                doubleBlack = dbParent;
                dbParent = doubleBlack->GetParent();
                if (dbParent)
                {
                    dbIsLeft = dbParent->left == doubleBlack;
                }
                
                continue;
            }
            else
            {
                dbParent->SetColor(Black);
                break;
            }
        }

        if (sibling->GetColor() == Red)
        {
            // case 4.
            rb_color temp = sibling->GetColor();
            sibling->SetColor(dbParent->GetColor());
            dbParent->SetColor(temp);

            // Because we rotated in the direction of the db, only the sibling has changed.

            if (dbIsLeft)
            {
                LeftRotate(dbParent);
                sibling = dbParent->right;
            }
            else
            {
                RightRotate(dbParent);
                sibling = dbParent->right;
            }
            
            continue;
        }

        free_block *farNiece;
        free_block *nearNiece;
        if (dbIsLeft)
        {
            farNiece = sibling->right;
            nearNiece = sibling->left;
        }
        else
        {
            farNiece = sibling->left;
            nearNiece = sibling->right;
        }

        if (sibling->GetColor() == Black &&
            (!farNiece || farNiece->GetColor() == Black) &&
            (nearNiece && nearNiece->GetColor() == Red))
        {
            // case 5
            nearNiece->SetColor(sibling->GetColor());
            sibling->SetColor(Red);

            if (dbIsLeft)
            {
                RightRotate(sibling);
                sibling = sibling->GetParent();
                farNiece = sibling->right;
            }
            else
            {
                LeftRotate(sibling);
                sibling = sibling->GetParent();
                farNiece = sibling->left;
            }

            goto case6;
        }

        if (sibling->GetColor() == Black &&
            (farNiece && farNiece->GetColor() == Red))
        {
            // case 6
          case6:
            rb_color temp = sibling->GetColor();
            sibling->SetColor(dbParent->GetColor());
            dbParent->SetColor(temp);

            if (dbIsLeft)
            {
                LeftRotate(dbParent);
            }
            else
            {
                RightRotate(dbParent);
            }
            
            if (doubleBlack) doubleBlack->SetColor(Black);
            if (farNiece) farNiece->SetColor(Black);
            break;
        }   
    }

#else
    (void)block;
#endif
}
