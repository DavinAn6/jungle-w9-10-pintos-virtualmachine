/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"
#include "threads/malloc.h"

static bool
uninit_initialize (struct page *page, void *kva);
static void
uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
    .swap_in = uninit_initialize,
    .swap_out = NULL,
    .destroy = uninit_destroy,
    .type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page,
            void *va,
            vm_initializer *init,
            enum vm_type type,
            void *aux,
            bool (*initializer) (struct page *, enum vm_type, void *)) {
    ASSERT (page != NULL);

    *page = (struct page){.operations = &uninit_ops,
                          .va = va,
                          .frame = NULL, /* no frame for now */
                          .uninit = (struct uninit_page){
                              .init = init,
                              .type = type,
                              .aux = aux,
                              .page_initializer = initializer,
                          }};
}

/* Initalize the page on first fault */
/* VM_UNINIT page를 최종 타입인 VM_ANON 또는 VM_FILE로 전환하고,
   필요하면 lazy-loading callback을 실행하는 것*/
static bool
uninit_initialize (struct page *page, void *kva) {
    struct uninit_page *uninit = &page->uninit;

    /* Fetch first, page_initialize may overwrite the values */
    vm_initializer *init = uninit->init;
    void *aux = uninit->aux;

    /* TODO: You may need to fix this function. */
    return uninit->page_initializer (page, uninit->type, kva) && (init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
    struct uninit_page *uninit = &page->uninit;

    free (uninit->aux); /* 끝까지 초기화되지 않은 page가 갖고 있는 aux(lazy_load_info) 정리 */
}
