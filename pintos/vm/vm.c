/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "hash.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
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
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

static uint64_t
page_hash (const struct hash_elem *e, void *aux UNUSED) {
	const struct page *p = hash_entry (e, struct page, hash_elem);
	return hash_bytes (&p->va, sizeof(p->va));
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	const struct page *pa = hash_entry (a, struct page, hash_elem);
	const struct page *pb = hash_entry (b, struct page, hash_elem);
	return pa->va < pb->va;	
}






/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. 
 */
/* 
 * type : 이 페이지가 최종적으로 될 종류 (VM_ANON 또는 VM_FILE)
 * upage : 유저 프로그램의 가상주소 (VA)
 * writable : 쓰기 가능 여부 
 * init : 2단계에서 내용을 채울 함수. 없으면 NULL
 * aux : init이 쓸 정보 (무얼 넣을지는 구현 선택)
 */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT);
	
	struct page *page;
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. 
	 * VA가 이미 SPT에 있으면 안됨
	 */
	if (spt_find_page (spt, upage) != NULL) {
		goto err;
	}

	page = malloc(sizeof(*page));
	if (page == NULL) {
		goto err;
	}

	/* 
	 * initializer 선택용 함수 포인터
	 */
	bool (*initializer)(struct page *, enum vm_type, void *);

	/* 
	 * 페이지 타입에 따라 initializer 선택
	 */
	switch (VM_TYPE(type)) {
		case VM_ANON : 
			initializer = anon_initializer;
			break;
		case VM_STACK :
			initializer = anon_initializer;
			break;
		case VM_FILE :
			initializer = file_backed_initializer;
			break;
		default :
			free(page);
			goto err;
	}

	//* VM_ANON 또는 VM_FILE 초기화
	uninit_new(page, upage, init, type, aux, initializer);
	//* Writable 설정
	page->writable = writable;

	if (!spt_insert_page(spt, page)) {
		free(page);
		goto err;
	}
	return true;

err:
	return false;
}






/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page key;		/* 찾을 키 담기 */
	struct hash_elem *e;	/* hash_find 결과 담기 */ 
	
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
	/* TODO: Fill this function. */
	return hash_insert (&spt->pages, &page->hash_elem) == NULL;
}


/* 
 * 1. 페이지의 해시 항목을 SPT에서 제거
 * 2. 페이지에 할당된 자원 해제 (frame 등)
 * 3. 페이지 구조체 해제
*/
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->pages, &page->hash_elem);
	vm_dealloc_page (page);
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
	/* TODO: Fill this function. */ 
	void *kva = palloc_get_page (PAL_USER);

	if (kva == NULL) {
		PANIC ("todo");
	}

	frame = malloc (sizeof *frame);
	frame->kva = kva;
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}





/* 
 * page_fault 함수가 호출. Return true on success
 * PARAM
 * 		- bool write : 메모리 접근이 쓰기 작업인가 
 * 		- bool not_present : 페이지 테이블에 주소 매핑이 있는가
 * 			true = 페이지 매핑 없음
 * 			false = 페이지 매핑 있지만 접근 위반
*/

bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {

	/* 
	 * 1. 유효성 검사
	 *		- 주소 NULL check
	 *		- kernel virtual address가 아닌지
	 *		- 페이지 테이블에 
	*/
	if ((addr == NULL) || (is_kernel_vaddr(addr)) || (not_present == false)) {
		return false;
	}

	/* 
	 * 2. Page Fault가 난 주소에 대응하는 페이지를 SPT에서 찾기
	 *		- 페이지가 SPT에 없을 경우,
	 *			- 유효한 주소면 스택 확장
	 *			- 유효하지 않은 주소면 실패 반환
	 *		- 페이지가 SPT에 있을 경우, 
	 *			- 쓰기 작업을 요청했으면 쓰기가 가능한 페이지인지 확인
	*/
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL) {
		//* 나중에 Stack growth 추가
		return false;
	}

	if ((write == true) && (page->writable == false)) {
		return false;
	}

	/*
	 * 3. 페이지 클레임
	*/
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





/* Copy supplemental page table from src to dst 
 * 하는 일 : 페이지마다 필요한 정보를 새로 만들어서 SPT 구성
 * 
 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {
	//* Source SPT를 한 페이지씩 순회
	struct hash_iterator iterator;
	hash_first(&iterator, &src->pages);

	while (hash_next(&iterator) != NULL) {
		//* 현재 원본 페이지, 페이지 타입, 가상주소, 쓰기 권한 가져옴
		struct page *p = hash_entry(hash_cur(&iterator), struct page, hash_elem);
		enum vm_type type = page_get_type(p);
		void *va = p->va;
		bool writable = p->writable;

		//* 페이지 종류에 맞게 복제 (UNINT, ANON, FILE)
		if (p->operations->type == VM_UNINIT) {
			/* 
			 * 자식 SPT에 등록
			 * 주의 : 부모와 자식이 같은 load_aux를 가리키면 안됨
			*/
			struct load_aux *child_la;
			if (p->uninit.aux == NULL) {
				child_la = NULL;
			} else {
				struct load_aux *parent_la = p->uninit.aux;
				if ((child_la = malloc(sizeof(struct load_aux))) == NULL) {
					return false;
				}
				if ((child_la->file = file_reopen(parent_la->file)) == NULL) {
					free(child_la);
					return false;
				}
				child_la->ofs = parent_la->ofs;
				child_la->page_read_bytes = parent_la->page_read_bytes;
				child_la->page_zero_bytes = parent_la->page_zero_bytes;
			}
			if (!vm_alloc_page_with_initializer(type, va, writable, p->uninit.init, child_la)) {
				file_close(child_la->file);
				free(child_la);
				return false;
			}
		
		} else if (p->operations->type == VM_ANON) {
			//* 자식 SPT에 등록
			if (!vm_alloc_page_with_initializer(type, va, writable, NULL, NULL)) {
				return false;
			}
			//* 자식 페이지 프레임 확보
			if (!vm_claim_page(va)) {
				return false;
			}
			struct page *new_page = spt_find_page(dst, va);
			//* 부모 페이지의 내용 복사
			memcpy(new_page->frame->kva, p->frame->kva, PGSIZE);
		} else if (p->operations->type == VM_FILE) {
			return false;
		}
	}
	//* 모든 페이지 복사 성공
	return true;
}



static void
spt_kill_helper (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(page);
}


/* Free the resource hold by the supplemental page table 
 * 하는 일 : 페이지마다 자원을 정리한 뒤 SPT 전체를 파괴
 * 연관 
 * 		destructor : 해시 항목 하나를 전달받아, 그 항목에 연결된 페이지를 정리하는 함수 (spt_kill_helper)
 * 		hash_clear() : 해시 테이블의 모든 항목을 하나씩 꺼내서, 각 항목에 destructor를 호출
 * 		hash_destroy() : hash_clear()로 항목들을 정리한 뒤, 해시 테이블의 bucket 배열까지 해제
 * 				두번째 인자가 hash_elem 포인터를 받는 함수여야함. 헬퍼 함수 생성
 * 		vm_dealloc_page() : 페이지의 destroy()를 실행하고 struct page 메모리를 해제
 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->pages, spt_kill_helper);
}
