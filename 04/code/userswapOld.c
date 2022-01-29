#include "userswap.h"
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

// page size
#define PAGE 4096
#define MAX_PAGE 2106

typedef struct {
  void* start;
  size_t size;
} alloc_t;

/* typedef struct page_t { */
/*   void* start; */
/*   struct page_t* next; */
/*   struct page_t* prev; */
/* } page_t; */

// doubly linked list; FIFO queue of resident pages
/* typedef struct { */
/*   page_t* pages; */
/*   size_t size; */
/* } ll_pages; */

typedef struct node {
  alloc_t* info;
  struct node* next;
  struct node* prev;
} node;

size_t LORM = MAX_PAGE;
// linked list of allocations
node* allocs = NULL;

// doubly linked list; FIFO queue of resident pages
node* resident_pages = NULL;

// linked list of free swap file locations

void append_allocs(alloc_t* new_alloc) {
  node* new_node = malloc(sizeof(node));
  new_node->info = new_alloc;
  new_node->next = NULL;
  new_node->prev = NULL;
  // no node yet
  if (!allocs) {
    allocs = new_node;
    return;
  }
  node* cur = allocs;
  while (cur->next) {
    cur = cur->next;
  }
  cur->next = new_node;
  new_node->prev = cur;
}

void append_resident_pages(alloc_t* new_page) {
  node* new_node = malloc(sizeof(node));
  new_node->info = new_page;
  new_node->next = NULL;
  new_node->prev = NULL;
  // no node yet
  if (!resident_pages) {
    resident_pages = new_node;
    return;
  }
  node* cur = resident_pages;
  while (cur->next) {
    cur = cur->next;
  }
  cur->next = new_node;
  new_node->prev = cur;
}

void unlink_allocs(node* node) {
  if (!node) return;
  // this is the first node
  if (!node->prev) {
    allocs = node->next;
  }
  if (node->next) {
    node->next->prev = node->prev;
  }
  if (node->prev) {
    node->prev->next = node->next;
  }
}

void unlink_resident_page(node* node) {
  if (!node) return;
  // this is the first node
  if (!node->prev) {
    resident_pages = node->next;
  }
  if (node->next) {
    node->next->prev = node->prev;
  }
  if (node->prev) {
    node->prev->next = node->next;
  }
}

size_t get_length(node* cur) {
  size_t len = 0;
  while (cur) {
    len++;
    cur = cur->next;
  }
  return len;
}

// sets first pages in fifo_queue to PROT_NONE
void handle_exceed_lorm(bool is_adding_page) {
  size_t num_pages = get_length(resident_pages);
  size_t max_pages = is_adding_page ? LORM - 1 : LORM;
  while (num_pages > max_pages) { // cannot be equal as we are adding a new page immediately after
    node* head = resident_pages;
    if (!head) return; // edge case where lorm = pages = 0
    resident_pages = head->next;
    if (resident_pages) {
      resident_pages->prev = NULL;
    }
    // set oldest page to prot_none (back to disk)
    // printf("kicking %p...\n", head->info->start);
    mprotect(head->info->start, head->info->size, PROT_NONE);
    free(head->info);
    free(head);
    num_pages--;
  }
};

// checks if addr (multiple of PAGE) exists in resident pages list
// if exists, returns pointer to the node with page
node* is_resident(char* addr) {
  node* head = resident_pages;
  while (head) {
    if (head->info->start == addr) {
      return head;
    }
    head = head->next;
  }
  return NULL;
}

// removes all pages in range start to start + block_size
void remove_resident(char* start, size_t block_size) {
  node* head = resident_pages;
  while (head) {
    node* cur_head = head;
    head = head->next;

    char* cur_addr = cur_head->info->start;
    if (start <= cur_addr && cur_addr < (start + block_size)) {
      // page is within the range! we need to remove it
      unlink_resident_page(cur_head);

      // free the data structure
      free(cur_head->info);
      free(cur_head);
    }
  }
}

// returns address of first page start address <= size
char* align_to_page(char* size) {
  size_t idx = (size_t) size / PAGE;
  return (char*) (idx * PAGE);
}

void page_fault_handler(char* addr) {
  // align read address to nearest start of page
  /* printf("before address: %p\n", addr); */
  addr = align_to_page(addr);
  /* printf("after address: %p\n", addr); */

  // if not resident, we bring to mem
  if (!is_resident(addr)) {
    // if next write exceeds lorm, evict with FIFO
    handle_exceed_lorm(true);

    // bring page to mem
    mprotect(addr, PAGE, PROT_READ);

    // add page to fifo queue
    alloc_t* new_page = malloc(sizeof(alloc_t));
    new_page->start = addr;
    new_page->size = PAGE;
    append_resident_pages(new_page);

    return;
  }

  // else we have written to a readonly page, set to PROT_READ | PROT_WRITE
  mprotect(addr, PAGE, PROT_READ | PROT_WRITE);
}

void sigsegv_handler(int sig, siginfo_t *info, void *ucontext) {
  // check if faulting mem address is within controlled region, else unregister
  bool is_controlled = false;
  node* head = allocs;
  char* start_add = NULL;
  char* end_add = NULL;
  char* cur_pos = (char*) info->si_addr; // Memory location which caused fault
  /* printf("fault at %p\n", cur_pos); */
  while (head) {
    start_add = (char*) head->info->start;
    end_add = (char*) head->info->start + head->info->size;
    /* printf("is %p in (%p, %p)\n", cur_pos, start_add, end_add); */
    if (start_add <= cur_pos && cur_pos <= end_add) {
      is_controlled = true;
      break;
    }
    head = head->next;
  }
  if (!is_controlled) {
    /* printf("goodbye!\n"); */
    signal(SIGSEGV, SIG_DFL);
    return;
  }

  // call page fault handler with the info of the page
  page_fault_handler(cur_pos);
}

// rounds up size to next multiple of page sz
size_t round_up(size_t size) {
  size_t res = size / PAGE;
  size -= res * PAGE;
  if (size) res++;
  return res * PAGE;
}

// sets LORM to size
void userswap_set_size(size_t size) {
  // round up size to next multiple of page sz
  size = round_up(size);
  LORM = size / PAGE; // since LORM stores unit of num_pages
  // if resident mem tot > size, evict min num of pages (FIFO)
  handle_exceed_lorm(false);
}

// allocates size bytes in controlled mem region and returns pointer
// to start
void *userswap_alloc(size_t size) {
  // install sigsegv handler, if not done yet
  struct sigaction sa;
  sa.sa_sigaction = sigsegv_handler;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);

  // round up size to next multiple of page sz
  /* printf("old size is %zu\n", size); */
  size = round_up(size);
  /* printf("new size is %zu\n", size); */

  // allocate new mmap
  void* start = mmap(NULL, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (start == MAP_FAILED) {
      printf("Failed to map mem because %s\n", strerror(errno));
      return NULL;
  }

  // create new info struct and add to state
  alloc_t* new_alloc = malloc(sizeof(alloc_t));
  new_alloc->start = start;
  new_alloc->size = size;
  append_allocs(new_alloc);
  return start;
}

// free block of mem starting from mem
// assume mem is always returned from alloc or map functions, has yet
// to be freed
// if map, do not close fd
// if map, write back changes to file
// if dirty
void userswap_free(void *mem) {
  // free the memory and the node!
  node* head = allocs;
  while (head) {
    // since mem is guaranteed to exist, we will always find a val
    if (head->info->start == mem) {
      size_t block_size = head->info->size;
      // remove mapping
      int res = munmap(mem, block_size);
      if (res) {
        fprintf(stderr, "Failed to make temporary file\n");
      }

      // remove all pages that are resident (now invalid)
      remove_resident(mem, block_size);

      // remove node
      unlink_allocs(head);
      free(head->info);
      free(head);
      return;
    }
    head = head->next;
  }
}

// maps first size bytes of file open in fd in controlled mem
// region and returns pointer to start
// fd always valid, open in rw using open(), unused elsewhr
void *userswap_map(int fd, size_t size) {
  // round up size to next multiple of page sz
  // fill 0s till size bytes are rched
  return NULL;
}

