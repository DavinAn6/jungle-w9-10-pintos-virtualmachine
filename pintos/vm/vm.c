/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"

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

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* frame을 할당하는 것이 아니라, 나중에 초기화 될 VM_UNINIT page 객체를 만들어
   현재 thread의 SPT에 등록하는 역할
   
   로직:
   1. 같은 VA의 page가 SPT에 있는지 확인
   2. struct page 메모리 할당
   3. type에 맞는 page initializer 선택
   4. uninit_new()로 VM_UNINIT page 구성
   5. writable 정보 저장
   6. SPT에 삽입
   7. 실패하면 할당한 자원 정리  */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	
	struct page *page;
	struct supplemental_page_table *spt = &thread_current ()->spt;
	bool (*initializer)(struct page *, enum vm_type, void *);

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) != NULL) {
		goto err;
	}

	page = malloc(sizeof(*page));
	if (page == NULL) {
		goto err;
	}

	if (VM_TYPE(type) == VM_ANON) {
		initializer = anon_initializer;
	}
	else if (VM_TYPE(type) == VM_FILE) {
		initializer = file_backed_initializer;
	}
	else {
		free(page);
		goto err;
	}

	uninit_new(page, upage, init, type, aux, initializer);
	page->writable = writable;

	/* 같은 VA를 가진 page가 이미 spt에 존재하는 경우 삽입 실패 edge case 처리
	   중복 삽입을 방지 하는 방어코드 사실 쓸 데 없을지도 모름 */
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
	struct page *page = NULL;
	struct page page_temp;
	struct hash_elem *hash_elem;

	/* 임의의 가상주소를 해당 페이지의 시작 주소로 정렬하여 검색 키로 사용 */
	page_temp.va = pg_round_down(va);

	/* 임시 페이지의 해시 elem으로 동일한 VA를 가진 elem을 검색한다. */
	hash_elem = hash_find(&spt->pages, &page_temp.spt_elem);

	if (hash_elem != NULL) {
		/* 검색된 해시 elem을 포함하는 실제 page를 복원한다. */
		page = hash_entry(hash_elem, struct page, spt_elem);
	}
	else {
		return NULL;
	}
	return page;
}

/* Insert PAGE into spt with validation. */
/* page->spt_elem을 spt 해시에 삽입하고, 같은 가상 주소의 page가 이미 있으면 실패를 반환하는 함수 */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	struct hash_elem *hash_elem;

	hash_elem = hash_insert (&spt->pages, &page->spt_elem);
	if (hash_elem == NULL) {
		succ = true;
	}
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->pages, &page->spt_elem);
	vm_dealloc_page (page);
	//return true;
	return;
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
/* 이 함수는 암튼 무조건 get frame함. 유저 풀 메모리에 자리가 없으면 eviction 하고 
   get frame 해야 한다는 소리 -> 실패 시 NULL 반환 이런거 안 함
   why? 물리 메모리가 찼다는 이유만으로 NULL을 반환하면,
   해결 가능한 page fault인데도 프로세스를 종료하게 되기 때문에 eviction이 필요.
   
   로직:
   1. user pool에서 물리 페이지 확보 시도
   2. 성공하면 해당 물리 페이지를 관리할 struct frame 생성
   3. frame을 아직 어떤 page에도 연결되지 않은 상태로 초기화
   4. 물리 페이지 확보 실패 시 기존 frame을 eviction 해서 재사용
   5. 사용 가능한 frame 반환  */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	void *kva;

	kva = palloc_get_page(PAL_USER);
	if (kva != NULL) {
		frame = malloc(sizeof(*frame));
		/* user pool이 아니라 kernel heap의 메모리가 부족한 상황 */
		if (frame == NULL) {
			palloc_free_page(kva);
			PANIC ("Failed to allocate frame metadata");
		}
		frame->kva = kva;
	}
	else {
		frame = vm_evict_frame(); //evict 함수 아직 미완성이라 나중에 해야함
	}

	ASSERT (frame != NULL);
	frame->page = NULL;
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

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

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
/* VA를 받는 외부용 함수 (그래서 static이 아닌갑다)
   현재 thread의 SPT에서 struct page를 검색
   그렇게 찾은 page를 vm_do_claim_page(page) 호출 
   
   로직:
   현재 thread의 SPT 가져오기
   spt_find_page(spt, va)
   page가 없으면 false
   있으면 vm_do_claim_page(page) */
bool
vm_claim_page (void *va) {
	struct page *page = NULL;

	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* frame 확보
   page와 frame을 연결
   PML4에 VA -> kva 매핑
   swap_in()으로 page 내용 준비 */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
		frame->page = NULL;
		page->frame = NULL;
		palloc_free_page(frame->kva);
		free(frame);

		return false;
	}
	
	/* lazy loading 중 파일 내용을 제대로 읽지 못한 경우 edge case 처리
	   ex) lazy_load_segment() 시,
	   		실행 파일에서 요청한 바이트 수만큼 읽지 못함
			잘못된 file/offeset 정보
			lazy initializer 자체가 실패
			타입별 page initializer가 실패 
			
			이 처리를 안하면,
			pml4 매핑은 했고,
			frame도 있는데,
			page에 데이터는 이상한 데이터라 메모리 누수*/
	if (!swap_in (page, frame->kva)){
		pml4_clear_page(thread_current()->pml4, page->va);
		frame->page = NULL;
		page->frame = NULL;
		palloc_free_page(frame->kva);
		free(frame);
		return false;
	}
	return true;
}

/* hash_init()에 인자로 줄 key의 hash값 계산 및 비교 함수
   각각 page_hash(), page_less() */

static uint64_t page_hash (const struct hash_elem *e_, void *aux UNUSED) {
	const struct page *page = hash_entry(e_, struct page, spt_elem);
	return hash_bytes(&page->va, sizeof(page->va));
}

static bool page_less (const struct hash_elem *a_,
					   const struct hash_elem *b_, void *aux UNUSED) {
	const struct page *a = hash_entry(a_, struct page, spt_elem);
	const struct page *b = hash_entry(b_, struct page, spt_elem);

	return a->va < b->va;
}

/* Initialize new supplemental page table */
/* spt->pages를 비어 있는 해시 테이블로 초기화 */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {

	/* hash_init이 매개변수로 받는 변수들은
	   초기화 할 해시 테이블,
	   key의 hash 값을 계산할 함수(page_hash())
	   두 key를 비교할 함수(page_less())
	   callback에 전달할 추가 정보(일단 aux)
	   
	   따라서 함수 두개 static으로 만드는게 선행 됨.  */
	hash_init(&spt->pages, page_hash, page_less, NULL);

}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
