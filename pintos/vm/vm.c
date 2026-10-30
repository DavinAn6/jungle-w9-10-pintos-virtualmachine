/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "hash.h"
#include "threads/vaddr.h"
#include <string.h>

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
    vm_anon_init ();
    vm_file_init ();
#ifdef EFILESYS /* For project 4 */
    pagecache_init ();
#endif
    register_inspect_intr ();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
    int ty = VM_TYPE (page->operations->type);
    switch (ty) {
    case VM_UNINIT:
        return VM_TYPE (page->uninit.type);
    default:
        return ty;
    }
}

/* Helpers */
static struct frame *
vm_get_victim (void);
static bool
vm_do_claim_page (struct page *page);
static struct frame *
vm_evict_frame (void);

static uint64_t
page_hash (const struct hash_elem *e, void *aux UNUSED) {
    const struct page *p = hash_entry (e, struct page, hash_elem);
    return hash_bytes (&p->va, sizeof (p->va));
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    const struct page *pa = hash_entry (a, struct page, hash_elem);
    const struct page *pb = hash_entry (b, struct page, hash_elem);
    return pa->va < pb->va;
}

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (
    enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {
    ASSERT (VM_TYPE (type) != VM_UNINIT);

    struct page *page;
    struct supplemental_page_table *spt = &thread_current ()->spt;
    bool (*initializer) (struct page *, enum vm_type, void *);

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page (spt, upage) != NULL) {
        goto err;
    }

    /* 여기서 할당하는 것은 실제 4kb 물리 페이지가 아니라, 페이지 정보를 기록하는 메타 데이터
	   struct page이기 때문 */
    page = malloc (sizeof (*page));
    if (page == NULL) {
        goto err;
    }

    if (VM_TYPE (type) == VM_ANON) {
        initializer = anon_initializer;
    } else if (VM_TYPE (type) == VM_FILE) {
        initializer = file_backed_initializer;
    } else {
        free (page);
        goto err;
    }

    uninit_new (page, upage, init, type, aux, initializer);
    page->writable = writable;

    /* 같은 VA를 가진 page가 이미 spt에 존재하는 경우 삽입 실패 edge case 처리
	   중복 삽입을 방지 하는 방어코드 사실 쓸 데 없을지도 모름 */
    if (!spt_insert_page (spt, page)) {
        free (page);
        goto err;
    }
    return true;

err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
    struct page key;     /* 찾을 키 담기 */
    struct hash_elem *e; /* hash_find 결과 담기 */

    key.va = pg_round_down (va);
    e = hash_find (&spt->pages, &key.hash_elem);

    if (e == NULL) {
        return NULL;
    }
    return hash_entry (e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
    return hash_insert (&spt->pages, &page->hash_elem) == NULL;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
    hash_delete (&spt->pages, &page->hash_elem);
    vm_dealloc_page (page);
    // return true; 반환형이 void인데 기본 주어진 코드는 true 반환. 왜?
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
    struct frame *victim UNUSED = vm_get_victim ();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
    struct frame *frame = NULL;

    void *kva = palloc_get_page (PAL_USER);

    if (kva == NULL) {
        PANIC ("todo");
    }

    frame = malloc (sizeof *frame);
    if (frame != NULL) {
        frame->kva = kva;
        frame->page = NULL;
    }

    ASSERT (frame != NULL);
    ASSERT (frame->page == NULL);
    return frame;
}

/* stack growth helper */
#define STACK_LIMIT (1 << 20) /* 스택최대크기 = 1MB */

static bool
vm_is_stack_growth (void *addr, uintptr_t rsp) {
    uintptr_t addr_val = (uintptr_t)addr;

    if (addr_val < USER_STACK - STACK_LIMIT || addr_val >= USER_STACK) {
        return false;
    }
    if (addr_val < rsp - 8) {
        return false;
    }
    return true;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr) {
    void *stack_bottom = pg_round_down (addr);

    return vm_alloc_page (VM_ANON | VM_MARKER_0, stack_bottom, true) &&
           vm_claim_page (stack_bottom);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {}

/* fault가 발생한 주소가 lazy page인지 판단하고, 해당 page를 claim 하는 것 */
/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {
    struct supplemental_page_table *spt = &thread_current ()->spt;
    struct page *page = NULL;
    /* TODO: Validate the fault */
    if (addr == NULL || is_kernel_vaddr (addr)) {
        return false;
    }

    if (not_present == false) {
        return false;
    }

    page = spt_find_page (spt, addr);
    if (page == NULL) {
        uintptr_t rsp = user ? f->rsp : thread_current ()->user_rsp;
        if (vm_is_stack_growth (addr, rsp)) {
            return vm_stack_growth (addr);
        }
        return false;
    }

    if (write && !page->writable) {
        return false;
    }

    return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
    destroy (page);
    free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
    struct page *page = NULL;
    /* TODO: Fill this function */
    page = spt_find_page (&thread_current ()->spt, va);
    if (page == NULL) {
        return false;
    }
    return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
    struct frame *frame = vm_get_frame ();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    if (!pml4_set_page (thread_current ()->pml4, page->va, frame->kva, page->writable)) {
        palloc_free_page (frame->kva);
        free (frame);
        page->frame = NULL;
        return false;
    }
    return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
    hash_init (&spt->pages, page_hash, page_less, NULL);
}

/* supplemental_page_table_copy helper function 1 */
static bool
copy_uninit_page (struct page *src_page) {
    struct lazy_load_info *dst_info;
    struct lazy_load_info *src_info;

    src_info = (struct lazy_load_info *)src_page->uninit.aux;

    if (src_info == NULL) {
        if (!vm_alloc_page_with_initializer (src_page->uninit.type,
                                             src_page->va,
                                             src_page->writable,
                                             src_page->uninit.init,
                                             NULL)) {
            return false;
        } else {
            return true;
        }
    }

    dst_info = malloc (sizeof (*dst_info));
    if (dst_info == NULL) {
        return false;
    }

    dst_info->file = src_info->file;
    dst_info->ofs = src_info->ofs;
    dst_info->page_read_bytes = src_info->page_read_bytes;
    dst_info->page_zero_bytes = src_info->page_zero_bytes;

    if (!vm_alloc_page_with_initializer (src_page->uninit.type,
                                         src_page->va,
                                         src_page->writable,
                                         src_page->uninit.init,
                                         dst_info)) {
        free (dst_info);
        return false;
    }

    return true;
}

/* supplemental_page_table_copy helper function 2 */
static bool
copy_initialized_page (struct supplemental_page_table *dst, struct page *src_page) {
    if (!vm_alloc_page (src_page->operations->type, src_page->va, src_page->writable)) {
        return false;
    }

    if (!vm_claim_page (src_page->va)) {
        return false;
    }

    struct page *dst_page = spt_find_page (dst, src_page->va);
    memcpy (dst_page->frame->kva, src_page->frame->kva, PGSIZE);

    return true;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
                              struct supplemental_page_table *src) {
    struct hash_iterator iterator;
    struct hash_elem *e;
    struct page *src_page;

    hash_first (&iterator, &src->pages);
    e = hash_next (&iterator);

    while (e != NULL) {
        src_page = hash_entry (e, struct page, hash_elem);
        e = hash_next (&iterator);

        if (src_page->operations->type == VM_UNINIT) {
            if (!copy_uninit_page (src_page)) {
                return false;
            }
        } else {
            if (!copy_initialized_page (dst, src_page)) {
                return false;
            }
        }
    }
    return true;
}

/* callback function for spt_kill below */
static void
spt_page_destroy (struct hash_elem *e, void *aux UNUSED) {
    struct page *page = hash_entry (e, struct page, hash_elem);
    vm_dealloc_page (page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
    hash_destroy (&spt->pages, spt_page_destroy);
}
