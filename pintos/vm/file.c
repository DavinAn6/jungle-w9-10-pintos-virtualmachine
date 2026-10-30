/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

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
    struct file_page *file_page UNUSED = &page->file;
}

/* do_mmap helper function */
static bool
lazy_load_mmap (struct page *page, void *aux) {
    struct lazy_load_info *file_info = aux;

    page->file.file = file_info->file;
    page->file.ofs = file_info->ofs;
    page->file.page_read_bytes = file_info->page_read_bytes;

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
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {}

/* Do the munmap */
void
do_munmap (void *addr) {}
