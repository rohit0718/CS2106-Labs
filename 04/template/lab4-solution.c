#include "userswap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define EPRINT(...) fprintf(stderr, __VA_ARGS__)

// #define US_DEBUG
#ifdef US_DEBUG
#define DPRINT(...) EPRINT(__VA_ARGS__)
#else
#define DPRINT(...) ((void)0)
#endif

#define PAGE_ORDER 12
#define PAGE_SIZE 4096ull
#define PAGE_SIZE_MASK (PAGE_SIZE - 1ull)
#define ROUND_TO_PAGE(size) (((size) & ~PAGE_SIZE_MASK) + (((size)&PAGE_SIZE_MASK) ? PAGE_SIZE : 0))

struct allocation {
  void *start;
  size_t size;
  int fd;
  struct allocation *next;
};

static struct allocation *allocation_head;

static _Bool sigsegv_installed = 0;

static struct allocation *find_allocation(void *mem, struct allocation ***prev_next) {
  struct allocation **cur = &allocation_head;
  while (*cur) {
    if ((*cur)->start <= mem && mem < (void *)(((char *)((*cur)->start)) + (*cur)->size)) {
      if (prev_next) {
        *prev_next = cur;
      }
      return *cur;
    }
    cur = &(*cur)->next;
  }
  return NULL;
}

struct pte;
static void swapout_page(void *const addr, struct pte *pte);

enum page_state {
  ps_unmapped,
  ps_nonresident,
  ps_clean,
  ps_dirty,
  ps_last = 0x7fff
} __attribute__((__packed__));

struct pte {
  ssize_t swap_index : 36;
  uint16_t unused : 12;
  _Bool is_map : 1;
  enum page_state state : 15;
} __attribute__((__packed__));
_Static_assert(sizeof(struct pte) == 8, "struct pte wrong size");

struct pt {
  struct pte pte[512];
};
_Static_assert(sizeof(struct pt) <= PAGE_SIZE, "struct pt too large");

struct pmd {
  struct pt *pt[512];
};
_Static_assert(sizeof(struct pmd) <= PAGE_SIZE, "struct pmd too large");

struct pud {
  struct pmd *pmd[512];
};
_Static_assert(sizeof(struct pud) <= PAGE_SIZE, "struct pud too large");

struct p4d {
  struct pud *pud[512];
};
_Static_assert(sizeof(struct p4d) <= PAGE_SIZE, "struct p4d too large");

static struct p4d page_table;

static struct pte *get_pte(void *const addr) {
  uintptr_t addrint = (uintptr_t)addr;
  unsigned int p4di = (addrint >> 39) & 0x1FF;
  unsigned int pudi = (addrint >> 30) & 0x1FF;
  unsigned int pmdi = (addrint >> 21) & 0x1FF;
  unsigned int pti = (addrint >> 12) & 0x1FF;

  struct pud *pud = page_table.pud[p4di];
  if (!pud) {
    return NULL;
  }

  struct pmd *pmd = pud->pmd[pudi];
  if (!pmd) {
    return NULL;
  }

  struct pt *pt = pmd->pt[pmdi];
  if (!pt) {
    return NULL;
  }

  return pt->pte + pti;
}

static struct pte *get_pte_alloc(void *const addr) {
  uintptr_t addrint = (uintptr_t)addr;
  unsigned int p4di = (addrint >> 39) & 0x1FF;
  unsigned int pudi = (addrint >> 30) & 0x1FF;
  unsigned int pmdi = (addrint >> 21) & 0x1FF;
  unsigned int pti = (addrint >> 12) & 0x1FF;

  struct pud *pud = page_table.pud[p4di];
  if (!pud) {
    pud = calloc(1, sizeof(struct pud));
    page_table.pud[p4di] = pud;
  }

  struct pmd *pmd = pud->pmd[pudi];
  if (!pmd) {
    pmd = calloc(1, sizeof(struct pmd));
    pud->pmd[pudi] = pmd;
  }

  struct pt *pt = pmd->pt[pmdi];
  if (!pt) {
    pt = calloc(1, sizeof(struct pt));
    pmd->pt[pmdi] = pt;
    for (size_t i = 0; i < 512; ++i) {
      pt->pte[i].swap_index = -1;
    }
  }

  return pt->pte + pti;
}

static size_t num_resident = 0;
static size_t num_resident_limit = 2106;

struct fifo_list_node {
  void *page;
  struct fifo_list_node *next;
  struct fifo_list_node *prev;
};

static struct fifo_list_node *fifo_list_head;
static struct fifo_list_node *fifo_list_tail;

static void add_resident_page(void *addr) {
  struct fifo_list_node *node = malloc(sizeof(struct fifo_list_node));
  if (!node) {
    abort();
  }

  *node = (struct fifo_list_node){.page = addr, .next = NULL, .prev = fifo_list_tail};
  if (fifo_list_tail) {
    fifo_list_tail->next = node;
  }
  fifo_list_tail = node;
  if (!fifo_list_head) {
    fifo_list_head = node;
  }

  ++num_resident;
}

static void remove_resident_page_node(struct fifo_list_node *node) {
  if (node->next) {
    node->next->prev = node->prev;
  } else {
    fifo_list_tail = node->prev;
  }

  if (node->prev) {
    node->prev->next = node->next;
  } else {
    fifo_list_head = node->next;
  }
}

static void remove_resident_page_range(void *const addr, size_t size) {
  const uintptr_t addrint = (uintptr_t)addr;
  const uintptr_t end = addrint + size;
  struct fifo_list_node *cur = fifo_list_head;
  while (cur) {
    DPRINT("Removing resident page node for %p due to free\n", cur->page);
    uintptr_t curpageint = (uintptr_t)cur->page;
    struct fifo_list_node *to_free = cur;
    cur = cur->next;
    if (curpageint >= addrint && curpageint < end) {
      remove_resident_page_node(to_free);
      --num_resident;
      free(to_free);
    }
  }
}

static void evict_resident_page(void) {
  if (!fifo_list_head) {
    if (num_resident) {
      EPRINT("Resident page count positive but resident page head is NULL\n");
      abort();
    }
    return;
  }

  struct fifo_list_node *const evictee_node = fifo_list_head;
  void *const evictee = evictee_node->page;
  remove_resident_page_node(evictee_node);
  free(evictee_node);

  struct pte *pte = get_pte(evictee);
  if (!pte || pte->state == ps_unmapped) {
    EPRINT("Attempted to evict unmapped page\n");
    abort();
  }

  swapout_page(evictee, pte);
  pte->state = ps_nonresident;
  mprotect(evictee, PAGE_SIZE, PROT_NONE);
  madvise(evictee, PAGE_SIZE, MADV_DONTNEED);
  DPRINT("Evicting %p\n", evictee);
  --num_resident;
}

static void evict_pages(size_t needed_pages) {
  while (num_resident && num_resident + needed_pages > num_resident_limit) {
    evict_resident_page();
  }
}

static int swapfilefd = -1;
static ssize_t *free_swap_pages = NULL;
static size_t free_swap_pages_end = 0;
static size_t free_swap_pages_capacity = 0;

static ssize_t last_swap_index(void) {
  struct stat s;
  if (fstat(swapfilefd, &s)) {
    perror("fstat swapfile");
    abort();
  }
  return s.st_size / PAGE_SIZE;
}

static void open_swapfile(void) {
  if (swapfilefd != -1) {
    return;
  }

  char *swapfilename = NULL;
  if (asprintf(&swapfilename, "%d.swap", getpid()) == -1) {
    EPRINT("asprintf failed\n");
    abort();
  }

  int fd = open(swapfilename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    perror("open swapfile");
    abort();
  }

  swapfilefd = fd;
}

static ssize_t find_free_swap_page(void) {
  if (free_swap_pages_end > 0) {
    return free_swap_pages[--free_swap_pages_end];
  }
  return -1;
}

static void add_free_swap_page(ssize_t page_index) {
  if (free_swap_pages_end >= free_swap_pages_capacity) {
    free_swap_pages_capacity = free_swap_pages_capacity ? free_swap_pages_capacity * 2 : 8;
    ssize_t *new_free_swap_pages =
        reallocarray(free_swap_pages, free_swap_pages_capacity, sizeof(ssize_t));
    if (!new_free_swap_pages) {
      EPRINT("failed to allocate free_swap_pages\n");
      abort();
    }
    free_swap_pages = new_free_swap_pages;
  }

  free_swap_pages[free_swap_pages_end++] = page_index;
}

static void flush_page(void *const addr, struct allocation *a) {
  DPRINT("flushing page %p\n", addr);
  if (pwrite(a->fd, addr, PAGE_SIZE, (char *)addr - (char *)a->start) != PAGE_SIZE) {
    perror("flush pwrite incomplete/failed");
    abort();
  }
}

static void swapout_page(void *const addr, struct pte *pte) {
  if (!pte || pte->state != ps_dirty) {
    return;
  }

  if (pte->is_map) {
    struct allocation *a = find_allocation(addr, NULL);
    if (!a) {
      EPRINT("failed to find allocation\n");
      abort();
    }

    flush_page(addr, a);
    return;
  }

  open_swapfile();

  ssize_t free_swap_page = pte->swap_index != -1 ? pte->swap_index : find_free_swap_page();
  if (free_swap_page == -1) {
    free_swap_page = last_swap_index();
  }

  if (pwrite(swapfilefd, addr, PAGE_SIZE, free_swap_page << PAGE_ORDER) != PAGE_SIZE) {
    perror("swapout pwrite incomplete/failed");
    abort();
  }
  pte->swap_index = free_swap_page;
}

static void swapin_page(void *const addr, struct pte *pte) {
  if (!pte || (pte->swap_index == -1 && !pte->is_map)) {
    return;
  }

  if (mprotect(addr, PAGE_SIZE, PROT_READ | PROT_WRITE)) {
    perror("swapin mprotect");
    abort();
  }

  if (pte->is_map) {
    struct allocation *a = find_allocation(addr, NULL);
    if (!a) {
      EPRINT("failed to find allocation\n");
      abort();
    }

    if (pread(a->fd, addr, PAGE_SIZE, (char *)addr - (char *)a->start) != PAGE_SIZE) {
      perror("mapin pread incomplete/failed");
      abort();
    }
    return;
  }

  if (pread(swapfilefd, addr, PAGE_SIZE, pte->swap_index << PAGE_ORDER) != PAGE_SIZE) {
    perror("swapin pread incomplete/failed");
    abort();
  }
}

static void handle_fault(void *addr, struct pte *pte) {
  addr = (void *)((uintptr_t)addr & ~PAGE_SIZE_MASK);
  switch (pte->state) {
  case ps_unmapped:
    break;
  case ps_nonresident:
    evict_pages(1);
    swapin_page(addr, pte);
    mprotect(addr, PAGE_SIZE, PROT_READ);
    pte->state = ps_clean;
    add_resident_page(addr);
    DPRINT("Faulted in %p, num_resident = %zu, limit = %zu\n", addr, num_resident,
           num_resident_limit);
    break;
  case ps_clean:
    mprotect(addr, PAGE_SIZE, PROT_READ | PROT_WRITE);
    pte->state = ps_dirty;
    break;
  case ps_dirty:
    DPRINT("Unexpected fault on ps_dirty page\n");
    abort();
    // should not happen?
    break;
  default:
    DPRINT("Invalid page state %d\n", pte->state);
    abort();
    break;
  }
}

static void handle_sigsegv(int signum, siginfo_t *siginfo, void *ucontext) {
  (void)signum;
  (void)ucontext;
  void *addr = siginfo->si_addr;
  struct pte *pte = get_pte(addr);
  if (!pte || pte->state == ps_unmapped) {
    EPRINT("uncontrolled segfault at %p, bailing\n", addr);
    abort();
    sigaction(SIGSEGV, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
    return;
  }
  handle_fault(addr, pte);
}

static void install_sigsegv(void) {
  if (sigsegv_installed) {
    return;
  }
  if (sigaction(SIGSEGV,
                &(struct sigaction){.sa_sigaction = handle_sigsegv, .sa_flags = SA_SIGINFO},
                NULL) == 0) {
    sigsegv_installed = 1;
  }
}

void userswap_set_size(size_t size) {
  num_resident_limit = ROUND_TO_PAGE(size) >> PAGE_ORDER;
  evict_pages(0);
}

static void *common_alloc(size_t size, int fd) {
  install_sigsegv();
  size = ROUND_TO_PAGE(size);

  void *ret = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ret == MAP_FAILED) {
    return NULL;
  }
  struct allocation *this_allocation = malloc(sizeof(struct allocation));
  if (!this_allocation) {
    goto munmap_fail;
  }

  if (fd != -1) {
    struct stat s;
    if (fstat(fd, &s)) {
      goto munmap_fail;
    }
    if (size > (size_t)s.st_size) {
      if (ftruncate(fd, (off_t)size)) {
        goto munmap_fail;
      }
    }
  }

  char *addr = ret;
  char *const endaddr = addr + size;
  for (; addr < endaddr; addr += PAGE_SIZE) {
    struct pte *pte = get_pte_alloc(addr);
    pte->state = ps_nonresident;
    if (fd != -1) {
      pte->is_map = 1;
    }
  }
  *this_allocation =
      (struct allocation){.start = ret, .size = size, .next = allocation_head, .fd = fd};
  allocation_head = this_allocation;

  return ret;

munmap_fail:
  munmap(ret, size);
  return NULL;
}

void *userswap_alloc(size_t size) { return common_alloc(size, -1); }

void userswap_free(void *mem) {
  struct allocation **prev_next;
  struct allocation *a = find_allocation(mem, &prev_next);
  if (a) {
    size_t size = a->size;
    char *addr = mem;
    char *const endaddr = addr + size;
    for (; addr < endaddr; addr += PAGE_SIZE) {
      struct pte *pte = get_pte(addr);
      if (!pte) {
        continue;
      }
      if (pte->swap_index != -1) {
        add_free_swap_page(pte->swap_index);
      } else if (pte->is_map && pte->state == ps_dirty) {
        flush_page(addr, a);
      }
      pte->swap_index = -1;
      pte->state = ps_unmapped;
    }
    remove_resident_page_range(mem, a->size);
    munmap(mem, size);
    *prev_next = a->next;
  } else {
    EPRINT("invalid pointer %p passed to userswap_free\n", mem);
    abort();
  }
  DPRINT("After free, num_resident = %zu\n", num_resident);
}

void *userswap_map(int fd, size_t size) { return common_alloc(size, fd); }
