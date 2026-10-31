/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <string.h>
#include <round.h>

static bool
file_backed_swap_in (struct page *page, void *kva);
static bool
file_backed_swap_out (struct page *page);
static void
file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &file_ops;

    struct file_page *file_page = &page->file;

    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
    struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
    struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
    struct file_page *file_page = &page->file;

    if (page->frame == NULL) {
        return;
    }

    if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
        file_write_at (file_page->file,
                       page->frame->kva,
                       file_page->page_read_bytes,
                       file_page->ofs);
    }

    pml4_clear_page (thread_current ()->pml4, page->va);
    palloc_free_page (page->frame->kva);
    free (page->frame);
}

/* do_mmap helper function */
static bool
lazy_load_mmap (struct page *page, void *aux) {
    struct lazy_load_info *file_info = aux;

    page->file.file = file_info->file;
    page->file.ofs = file_info->ofs;
    page->file.page_read_bytes = file_info->page_read_bytes;
    page->file.end_addr = file_info->end_addr;

    off_t bytes_read = file_read_at (page->file.file,
                                     page->frame->kva,
                                     page->file.page_read_bytes,
                                     page->file.ofs);

    if (bytes_read != page->file.page_read_bytes) {
        free (file_info);
        return false;
    }
    memset ((uint8_t *)page->frame->kva + file_info->page_read_bytes,
            0,
            file_info->page_zero_bytes);

    free (file_info);
    return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
    if (addr == NULL || pg_ofs (addr) != 0) {
        return NULL;
    }

    if (length == 0) {
        return NULL;
    }

    if (offset % PGSIZE != 0 || offset < 0) {
        return NULL;
    }

    void *start_addr = addr;
    struct file *mmap_file = file_reopen (file);
    if (mmap_file == NULL) {
        return NULL;
    }

    off_t file_remaining = file_length (mmap_file) - offset;
    if (file_remaining <= 0) {
        file_close (mmap_file);
        return NULL;
    }

    size_t read_bytes = length < file_remaining ? length : file_remaining;
    size_t zero_bytes = ROUND_UP (length, PGSIZE) - read_bytes;

    void *end_addr = (uint8_t *)start_addr + read_bytes + zero_bytes;

    if (!is_user_vaddr (start_addr) || !is_user_vaddr ((uint8_t *)(end_addr)-1) ||
        start_addr >= end_addr) {
        file_close (mmap_file);
        return NULL;
    }

    struct lazy_load_info *info = NULL;

    while (read_bytes > 0 || zero_bytes > 0) {
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        info = malloc (sizeof (*info));
        if (info == NULL) {
            for (uint8_t *remove_addr = start_addr; remove_addr < (uint8_t *)addr;
                 remove_addr += PGSIZE) {
                struct page *found_page = spt_find_page (&thread_current ()->spt, remove_addr);
                if (found_page == NULL) {
                    break;
                }
                spt_remove_page (&thread_current ()->spt, found_page);
            }
            file_close (mmap_file);
            return NULL;
        }
        void *aux = info;

        info->file = mmap_file;
        info->ofs = offset;
        info->page_read_bytes = page_read_bytes;
        info->page_zero_bytes = page_zero_bytes;
        info->end_addr = end_addr;

        if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_mmap, aux)) {
            free (info);
            for (uint8_t *remove_addr = start_addr; remove_addr < (uint8_t *)addr;
                 remove_addr += PGSIZE) {
                struct page *found_page = spt_find_page (&thread_current ()->spt, remove_addr);
                if (found_page == NULL) {
                    break;
                }
                spt_remove_page (&thread_current ()->spt, found_page);
            }
            file_close (mmap_file);
            return NULL;
        }

        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr = (uint8_t *)addr + PGSIZE;
        offset += (off_t)page_read_bytes;
    }
    return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
    struct page *page;
    struct file *mmap_file;
    void *end_addr;
    page = spt_find_page (&thread_current ()->spt, addr);

    if (page == NULL) {
        return;
    }

    if (page->operations->type == VM_UNINIT) {
        if (page->uninit.type != VM_FILE) {
            return;
        }
        struct lazy_load_info *mmap_info = page->uninit.aux;

        mmap_file = mmap_info->file;
        end_addr = mmap_info->end_addr;
    }

    else if (page->operations->type == VM_FILE) {
        mmap_file = page->file.file;
        end_addr = page->file.end_addr;
    }

    else {
        return;
    }

    for (uint8_t *remove_addr = addr; remove_addr < (uint8_t *)end_addr; remove_addr += PGSIZE) {
        struct page *found_page = spt_find_page (&thread_current ()->spt, remove_addr);
        if (found_page == NULL) {
            break;
        }
        spt_remove_page (&thread_current ()->spt, found_page);
    }
    file_close (mmap_file);
}
