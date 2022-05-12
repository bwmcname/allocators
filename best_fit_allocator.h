#pragma once

#include "memory_interface.h"
#include "allocator_interface.h"

#pragma warning(push, 0)
#ifdef USE_STL
#include <unordered_map>
#endif
#pragma warning(pop)

#ifdef _MSC_VER
#define BM_RESTRICT __restrict
#endif

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

// MI = Memory interface type.
// MA = Minimum allowed alignment. Must be atleast 2 and a power of 2.
template <typename MI, size_t MA=16>
struct best_fit_allocator
{
    static_assert(IsPowerOf2(MA), "MA must be a power of 2");

    // The red/black bit is stored in the lsb of the parent pointer of a free_block,
    // so we must ensure that all free_block addresses are aligned by atleast 2.
    // the free bit is also stored in the lsb of the prev pointer in the block_header.
    static_assert(MA > 1, "MA must be atleast 2");

    best_fit_allocator(MI *memoryProvider, size_t minimumReservation);
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
    struct block_header
    {
        block_header *next;

        bool GetFree()
        {
            return (size_t)prev & 1llu;
        }

        void SetFree(bool free)
        {
            prev = (block_header *)(((size_t)prev & ~1llu) | (size_t)free);
        }

        block_header *GetPrev()
        {
            return (block_header *)((size_t)prev & ~1llu);
        }

        void SetPrev(block_header *newPrev)
        {
            prev = (block_header *)((size_t)newPrev | ((size_t)prev & 1llu));
        }

        size_t GetSize(best_fit_allocator<MI, MA> *owner)
        {
            size_t end;
            if (next)
            {
                end = (size_t)next;
            }
            else
            {
                end = owner->mem_committed + (size_t)owner->base;
            }

            return end - ((size_t)this + owner->chunk_size);
        }

    private:
        block_header *prev;
    };

    using rb_color = bool;
    static const rb_color Red = true;
    static const rb_color Black = false;

    struct free_block
    {
        block_header header;

        free_block *left;
        free_block *right;

        free_block *GetParent()
        {
            return (free_block *)((size_t)parent & ~1llu);
        }
        
        void SetParent(free_block *newParent)
        {
            parent = (free_block *)((size_t)newParent | ((size_t)parent & 1llu));
        }
        
        rb_color GetColor()
        {
            return (size_t)parent & 1llu;
        }
        
        void SetColor(rb_color color)
        {
            parent = (free_block *)(((size_t)parent & ~1llu) | (size_t)color);
        }

    private:
        free_block *parent;
    };

    static constexpr size_t chunk_size = SnapUpToPow2Increment(sizeof(block_header), MA);
    static constexpr size_t free_block_overhead = SnapUpToIncrement(sizeof(free_block), chunk_size);
    static constexpr size_t smallest_valid_free_block = free_block_overhead > (2 * chunk_size) ? free_block_overhead : (2 * chunk_size);

    MI *memory_provider;

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
    void LeftRotate(free_block *node);
    void RightRotate(free_block *node);
    void HeaderIntersectsAny(block_header *header);
    void ValidateBSTInternal(free_block *block, size_t min, size_t max);
    int ValidateRedBlackNodeInternal(free_block *node);
    void ValidateBSTNodeLinksInternal(free_block *node);
    #ifdef USE_STL
    void ValidateFreeNodesInTreeInternal(std::unordered_set<block_header *> *freeBlocks, free_block *node);
    void ValidateBSTUniquenessInternal(std::unordered_set<free_block *> *set, free_block *node);
    #endif
};

#if !defined(BM_ASSERT)
#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)
#define BM_ASSERT(val, msg) assert(val)
#endif

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBSTInternal(free_block *block, size_t min, size_t max)
{
    // BM_ASSERT(block->header.free, "Non-free block in BST");

    if (block->left)
    {
        BM_ASSERT(block->left->header.GetSize(this) <= block->header.GetSize(this), "BST rules broken");
        BM_ASSERT(block->left->header.GetSize(this) >= min, "BST rules broken");
        BM_ASSERT(block->left->header.GetSize(this) <= max, "BST rules broken");

        size_t childMax = block->header.GetSize(this);
        ValidateBSTInternal(block->left, min, childMax);
    }
    
    if (block->right)
    {
        BM_ASSERT(block->right->header.GetSize(this) >= block->header.GetSize(this), "BST rules broken");
        BM_ASSERT(block->right->header.GetSize(this) >= min, "BST rules broken");
        BM_ASSERT(block->right->header.GetSize(this) <= max, "BST rules broken");

        size_t childMin = block->header.GetSize(this);
        ValidateBSTInternal(block->right, childMin, max);
    }
}

template <typename MI, size_t MA>
int best_fit_allocator<MI, MA>::ValidateRedBlackNodeInternal(free_block *node)
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

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBSTNodeLinksInternal(free_block *node)
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

#ifdef USE_STL

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateFreeNodesInTreeInternal(std::unordered_set<block_header *> *freeBlocks, free_block *node)
{
    if (!node)
    {
        return;
    }

    if (node->header.GetFree())
    {
        auto it = freeBlocks->find(&node->header);
        BM_ASSERT(it != freeBlocks->end(), "Free block found in BST but not in the allocation list");
        freeBlocks->erase(it);
    }

    ValidateFreeNodesInTreeInternal(freeBlocks, node->left);
    ValidateFreeNodesInTreeInternal(freeBlocks, node->right);
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBSTUniquenessInternal(std::unordered_set<free_block *> *set, free_block *node)
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

template <typename MI, size_t MA>
void *best_fit_allocator<MI, MA>::GetAllocationPtr(free_block *block)
{
    return (void *)((uint8_t *)block + chunk_size);
}

template <typename MI, size_t MA>
void *best_fit_allocator<MI, MA>::GetAllocationPtr(block_header *block)
{
    return GetAllocationPtr((free_block *)block);
}

template <typename MI, size_t MA>
typename best_fit_allocator<MI, MA>::free_block *best_fit_allocator<MI, MA>::GetBlockHeader(void *addr)
{
    return (free_block *)((uint8_t *)addr - chunk_size);
}

#pragma warning(suppress: 4514)
inline size_t GetAlignment(void *addr)
{
    size_t data = (size_t)addr;
    return ((data - 1) & ~data) + 1;
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBST()
{
    if (!root)
    {
        return;
    }

    ValidateBSTInternal(root, 0, SIZE_MAX);
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateRedBlackProperties()
{
    if (root)
    {
        BM_ASSERT(root->GetColor() == Black, "Root node of a red black tree must be black.");
    }

    ValidateRedBlackNodeInternal(root);
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBSTNodeLinks()
{
    ValidateBSTNodeLinksInternal(root);
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateFreeListSize()
{
    size_t mem = mem_committed;

    for (block_header *header = first;
         header != nullptr;
         header = header->next)
    {
        mem -= (header->GetSize(this) + chunk_size);
    }

    if (mem)
    {
        for (block_header *current = first;
             current != nullptr;
             current = current->next)
        {
            printf("%zu\n", current->GetSize(this));
        }
    }

    BM_ASSERT(mem == 0, "Internal allocation list leak detected");
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateFreeListLinks()
{
    block_header *prev = nullptr;
    for (block_header *header = first;
         header != nullptr;
         header = header->next)
    {
        BM_ASSERT(prev == header->GetPrev(), "Internal allocation list links broken");
        
        prev = header;
    }
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateFreeListAllocatorMembers()
{
    BM_ASSERT(root == nullptr || root->GetParent() == nullptr, "Root node of internal BST can't have a parent");
    BM_ASSERT(last == nullptr || last->next == nullptr, "Last node in list has a non-null next pointer");
    BM_ASSERT(first == nullptr || first->GetPrev() == nullptr, "First node in list has a non-null prev pointer");
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::HeaderIntersectsAny(block_header *header)
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

#ifdef USE_STL


template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateFreeNodesInTree()
{
    std::unordered_set<block_header *> freeBlocks;
    for (block_header *current = first;
         current;
         current = current->next)
    {
        if (current->GetFree())
        {
            freeBlocks.insert(current);
        }
    }

    ValidateFreeNodesInTreeInternal(&freeBlocks, root);

    BM_ASSERT(freeBlocks.empty(), "Free blocks found in the allocation list but not in the BST");
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::ValidateBSTUniqueness()
{
    std::unordered_set<free_block *> set;
    ValidateBSTUniquenessInternal(&set, root);
}

#endif

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::DetectCorruption()
{
    ValidateFreeListLinks();
    ValidateFreeListSize();
    ValidateFreeListAllocatorMembers();
    ValidateBSTNodeLinks();
    ValidateBST();
    ValidateRedBlackProperties();

#ifdef USE_STL
    ValidateFreeNodesInTree();
    ValidateBSTUniqueness();
#endif
}

template <typename MI, size_t MA>
best_fit_allocator<MI, MA>::best_fit_allocator(
    MI *memoryProvider,
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
    root->header.SetPrev(nullptr);
    root->header.next = nullptr;
    root->header.SetFree(true);
    root->left = nullptr;
    root->right = nullptr;
    root->SetParent(nullptr);
    root->SetColor(Black);

    first = &root->header;
    last = first;
}

template<typename MI, size_t MA>
best_fit_allocator<MI, MA>::~best_fit_allocator()
{
    memory_provider->DeCommit(base, mem_committed);
    memory_provider->Release(base);
}

template <typename MI, size_t MA>
bool best_fit_allocator<MI, MA>::IsCommitted(void *addr, size_t size)
{
    size_t addrEnd = (size_t)addr + size;
    size_t maxCommitted = (size_t)base + mem_committed;

    return addrEnd <= maxCommitted;
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::GetCommitParams(void *requestedAddress, size_t requestedSize, void **paramAddress, size_t *paramSize)
{
    uint8_t *commitEnd = (uint8_t *)base + mem_committed;
    size_t difference = commitEnd - (uint8_t *)base;

    size_t unCommitted = requestedSize - difference;

    *paramAddress = (void *)commitEnd;
    *paramSize = unCommitted;
}
 
template <typename MI, size_t MA>
void *best_fit_allocator<MI, MA>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    (void)line;
    (void)file;
    BM_ASSERT(alignment <= MA, "Tried to allocate with an alignment greater than the maximum supported alignment");
    BM_ASSERT(size > 0, "Tried to allocate 0 bytes.");

    // Make sure the requested size is in chunk_size incremements
    // Also make sure the requested size it atleast as large as free_block_overhead
    // If we allocate less than that, then we risk overwriting members from the next block.
    size_t sizeToChunks = SnapUpToIncrement(size, chunk_size);
    size = sizeToChunks > free_block_overhead ? sizeToChunks : free_block_overhead;

    // find the smallest chunk that can fit this allocation.
    free_block *bestFit = FindBestFit(size);
    if (bestFit == nullptr)
    {
        // No suitable block!
        // We have to commit more pages for this allocation
        
        uint8_t *unCommitted = (uint8_t *)base + mem_committed;

        if (last->GetFree())
        {
            free_block *lastFree = (free_block *)last;
            size_t requiredSize = size - last->GetSize(this);

            // Can't commit more than we have reserved.
            BM_ASSERT((requiredSize + mem_committed) <= mem_reserved, "Tried to commit more memory than reserved");

            // Add the new committed pages to the last block.
            size_t actualCommit;
            memory_provider->Commit(unCommitted, requiredSize, &actualCommit);

            mem_committed += actualCommit;
            RemoveNode(lastFree);
            AddNode(lastFree);

            // Must do this after checking if we can change the node size.
            // the node size of the last item in the list is dependant on mem_committed.

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
            newBlock->header.SetFree(true);
            newBlock->header.SetPrev(last);
            newBlock->header.next = nullptr;
            last->next = &newBlock->header;
            last = &newBlock->header;

            AddNode(newBlock);

            bestFit = newBlock;
        }
    }

    void *allocation = GetAllocationPtr(&bestFit->header);

    // Mark this block as used.
    bestFit->header.SetFree(false);

    size_t leftover = bestFit->header.GetSize(this) - size;

    // Only add a new node if we have enough space for a free_block struct and
    // atleast one chunk.
    if (leftover >= smallest_valid_free_block)
    {
        // add a new node to the tree and a new header to the list for the leftover memory        
        free_block *newBlock = (free_block *)((uint8_t *)allocation + size);

        newBlock->header.SetFree(true);
        newBlock->header.SetPrev(&bestFit->header);
        if (bestFit->header.next && bestFit->header.next->GetFree())
        {
            // coalesce the two blocks
            free_block *toCombine = (free_block *)bestFit->header.next;
            newBlock->header.next = toCombine->header.next;
            if (toCombine->header.next) toCombine->header.next->SetPrev((block_header *)newBlock);

            RemoveNode(toCombine);
            RemoveNode(bestFit);
            AddNode(newBlock);
        }
        else
        {
            RemoveNode(bestFit);
            newBlock->header.next = bestFit->header.next;
            AddNode(newBlock);

            if (bestFit->header.next) bestFit->header.next->SetPrev((block_header *)newBlock);
        }

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

template <typename MI, size_t MA>
void *best_fit_allocator<MI, MA>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    (void)line;
    (void)file;
    block_header *header = (block_header *)((uint8_t *)addr - chunk_size);
    size = SnapUpToIncrement(size, chunk_size);

    if (size <= header->GetSize(this))
    {
        return GetAllocationPtr(header);
    }

    block_header *current = header->next;
    size_t total = header->GetSize(this);
    int needed = 0;
    for (;;)
    {
        if (current == nullptr || !current->GetFree())
        {
            return nullptr;
        }

        total += current->GetSize(this) + chunk_size;

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
            size_t required = size - (total - (current->GetSize(this) + chunk_size));
            size_t leftover = (current->GetSize(this) + chunk_size) - required;

            if (leftover >= smallest_valid_free_block)
            {
                // There is enough leftover memory that we should add a new free node for it,
                // or move back the next free node.

                free_block *newBlock = (free_block *)((uint8_t *)current + required);
                
                newBlock->header.SetFree(true);
                newBlock->header.SetPrev(header);
                newBlock->header.next = current->next;
                header->next = &newBlock->header;
                if (current->next) current->next->SetPrev(&newBlock->header);

                if (newBlock->header.next == nullptr)
                {
                    last = &newBlock->header;
                }

                AddNode((free_block *)newBlock);
            }
            else
            {
                // nothing to add.
                header->next = current->next;
                if (current->next) current->next->SetPrev(header);

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

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::FreeInternal(void *addr, int line, const char *file)
{
    (void)line;
    (void)file;
    block_header *header = (block_header *)((uint8_t *)addr - chunk_size);
    BM_ASSERT(!header->GetFree(), "Trying to free an already free block.");
    header->SetFree(true);

    if (header->GetPrev() && header->GetPrev()->GetFree())
    {
        if (header->next && header->next->GetFree())
        {
            // Coalesce all 3 blocks.
            block_header *prevHeader = header->GetPrev();
            block_header *nextHeader = header->next;

            free_block *prevBlock = (free_block *)prevHeader;
            free_block *nextBlock = (free_block *)nextHeader;

            RemoveNode(prevBlock);
            prevHeader->next = nextHeader->next;

            RemoveNode(nextBlock);
            AddNode(prevBlock);

            if (nextHeader->next) nextHeader->next->SetPrev(prevHeader);

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
            block_header *prevHeader = header->GetPrev();
            free_block *prevBlock = (free_block *)prevHeader;

            RemoveNode(prevBlock);
            prevHeader->next = header->next;
            AddNode(prevBlock);

            if (header->next) header->next->SetPrev(prevHeader);

            if (prevHeader->next == nullptr)
            {
                // The new coalesced block is the last in the list.
                last = prevHeader;
            }
        }
    }
    else if (header->next && header->next->GetFree())
    {
        // Coalesce the two blocks
        free_block *block = (free_block *)header;
        block_header *nextHeader = header->next;
        free_block *nextBlock = (free_block *)nextHeader;

        RemoveNode(nextBlock);
        header->next = nextHeader->next;
        if (nextHeader->next) nextHeader->next->SetPrev(header);
        AddNode(block);

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

template <typename MI, size_t MA>
typename best_fit_allocator<MI, MA>::free_block *best_fit_allocator<MI, MA>::FindBestFit(size_t size)
{
    free_block *current = root;
    free_block *lastValid = nullptr;
    for (;;)
    {
        if (current == nullptr)
        {
            return lastValid;
        }

        if (current->header.GetSize(this) < size)
        {
            current = current->right;
        }
        else
        {
            lastValid = current;
            current = current->left;
        }
    }
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::LeftRotate(free_block *node)
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

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::RightRotate(free_block *node)
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

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::AddNode(free_block *block)
{
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
        if (block->header.GetSize(this) >= current->header.GetSize(this))
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
    free_block *parent = current->GetParent();
    while (current && parent && parent->GetColor() == Red)
    {
        BM_ASSERT(current->GetColor() == Red, "Expected Red Node");

        free_block *uncle;
        bool parentIsLeft;
        free_block *grandparent = parent->GetParent();
        if (grandparent)
        {
            parentIsLeft = grandparent->left == parent;
            if (parentIsLeft)
            {
                uncle = grandparent->right;
            }
            else
            {
                uncle = grandparent->left;
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
            parent->SetColor(Black);
            uncle->SetColor(Black);
            if (grandparent != root) grandparent->SetColor(Red);
            current = grandparent;
        }
        else // uncle is black
        {
            bool newIsLeft = parent->left == current;
            current = parent;

            // Due to rotation operations below, "parent" should no longer be assumed to be
            // the parent of the current.
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

        parent = current->GetParent();
    }
}

template <typename MI, size_t MA>
void best_fit_allocator<MI, MA>::RemoveNode(free_block *block)
{
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
}
