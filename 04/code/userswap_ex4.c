#include "userswap.h"
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// page size
#define PAGE 4096
#define MAX_PAGE 2106

typedef struct {
  void* start;
  size_t size;
  bool dirty;
  bool is_file;
  int fd;
} alloc_t;

typedef struct {
  void* mem_addr; // mem addr that is saved into swap
  off_t file_loc; // page occupies bytes [file_loc, file_loc + PAGE)
} swap_t;

typedef struct node {
  // info and swap_info can be union
  alloc_t* info;
  swap_t* swap_info;
  struct node* next;
  struct node* prev;
} node;

size_t LORM = MAX_PAGE;
// linked list of allocations
node* allocs = NULL;

// doubly linked list; FIFO queue of resident pages
node* resident_pages = NULL;

// linked list of free swap file locations
node* swap_locations = NULL;

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

void append_swap_locations(swap_t* new_swap) {
  node* new_node = malloc(sizeof(node));
  new_node->swap_info = new_swap;
  new_node->next = NULL;
  new_node->prev = NULL;
  // no node yet
  if (!swap_locations) {
    swap_locations = new_node;
    return;
  }
  node* cur = swap_locations;
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

void unlink_swap_location(node* node) {
  if (!node) return;
  // this is the first node
  if (!node->prev) {
    swap_locations = node->next;
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

bool is_file_loc_free(off_t file_loc) {
  node* head = swap_locations;
  while (head) {
    if (head->swap_info->file_loc == file_loc) {
      return false;
    }
    head = head->next;
  }
  return true;
}

// if swapfile exists, writes page info into existing file
// swapfile structure: page1page2....
// then linked list can just contain starting byte number
void write_to_swap(char* addr) {
  // if swapfile does not exist, we create a new file
  char* buffer;
  int err = asprintf(&buffer, "%d.swap", getpid());
  if (err == -1) {
      fprintf(stderr, "Failed to print to allocated string");
  }
  int fd = open(buffer, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH | S_IRGRP | S_IWGRP);
  free(buffer);
  if (fd == -1) {
      fprintf(stderr, "Open failed (write_to_swap): %s\n", strerror(errno));
      return;
  }

  // read file in chunks of PAGE
  char page_buffer[PAGE];
  off_t file_loc = 0;
  ssize_t off = 0;
  do {
    if (off == -1) {
      fprintf(stderr, "Read failed (write_to_swap): %s\n", strerror(errno));
      break;
    }
    if (is_file_loc_free(file_loc)) {
      break;
    }
    file_loc += PAGE;
  } while ((off = read(fd, page_buffer, PAGE)));

  // write PAGE bytes from addr in mem to file_loc in file
  err = write(fd, addr, PAGE);
  if (err == -1) {
    fprintf(stderr, "Write failed: %s\n", strerror(errno));
  }
  close(fd); // we are done with the file

  // store file loc into swap_locations
  swap_t* new_swap = malloc(sizeof(swap_t));
  new_swap->mem_addr = addr;
  new_swap->file_loc = file_loc;
  append_swap_locations(new_swap);
}

node* get_alloc_node(char* addr) {
  node* head = allocs;
  while (head) {
    char* start_add = (char*) head->info->start;
    char* end_add = (char*) head->info->start + head->info->size;
    if (start_add <= addr && addr < end_add) {
      return head;
    }
    head = head->next;
  }
  return NULL;
}

void write_to_file(int fd, char* addr) {
  // use addr to get alloc start addr
  node* alloc_node = get_alloc_node(addr);
  // use alloc start addr to get offset
  off_t offset = addr - (char*) alloc_node->info->start;
  // pwrite into file
  int err = pwrite(fd, addr, PAGE, offset);
  if (err == -1) {
    fprintf(stderr, "Write failed (write_to_file): %s\n", strerror(errno));
  }
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
    // we write page to swapfile if dirty
    if (head->info->dirty) {
      // is this a mem map file?
      if (head->info->is_file) {
        write_to_file(head->info->fd, head->info->start);
      } else {
        write_to_swap(head->info->start);
      }
    }
    // clear page in mem
    mprotect(head->info->start, head->info->size, PROT_NONE);
    madvise(head->info->start, head->info->size, MADV_DONTNEED);
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
// called by free method, no need to write to swap
void remove_resident(char* start, size_t block_size) {
  node* head = resident_pages;
  while (head) {
    node* cur_head = head;
    head = head->next;

    char* cur_addr = cur_head->info->start;
    if (start <= cur_addr && cur_addr < (start + block_size)) {
      // page is within the range! we need to remove it
      unlink_resident_page(cur_head);

      // if its a dirty mapped file, we write back to file
      if (cur_head->info->dirty && cur_head->info->is_file) {
        write_to_file(cur_head->info->fd, cur_addr);
      }

      // clear page in mem
      mprotect(cur_head->info->start, cur_head->info->size, PROT_NONE);
      madvise(cur_head->info->start, cur_head->info->size, MADV_DONTNEED);
      // free the data structure
      free(cur_head->info);
      free(cur_head);
    }
  }
}

// removes all swap loc in range start to start + block_size
// we do lazy deletions, if file_loc doesnt exist, simply overwrite block
// called by free method
void remove_swap(char* start, size_t block_size) {
  node* head = swap_locations;
  while (head) {
    node* cur_head = head;
    head = head->next;

    char* cur_addr = cur_head->info->start;
    if (start <= cur_addr && cur_addr < (start + block_size)) {
      // page is within the range! we need to remove it
      unlink_swap_location(cur_head);

      // free the data structure
      free(cur_head->swap_info);
      free(cur_head);
    }
  }
}

// returns address of first page start address <= size
char* align_to_page(char* size) {
  size_t idx = (size_t) size / PAGE;
  return (char*) (idx * PAGE);
}

// sets page starting at addr as dirty
// addr is guarenteed to exist in resident_pages
void set_to_dirty(char* addr) {
  node* head = resident_pages;
  while (head) {
    if (head->info->start == addr) {
      head->info->dirty = true;
      return;
    }
    head = head->next;
  }
}

node* has_swap(char* addr) {
  node* head = swap_locations;
  while (head) {
    if (head->swap_info->mem_addr == addr) {
      return head;
    }
    head = head->next;
  }
  return NULL;
}

// moves PAGE bytes from pid.swap loc swap_info->file_loc to addr
void swap_to_mem(node* node, char* addr) {
  // setvbuf(stdout, NULL, _IONBF, 0);
  swap_t* swap_info = node->swap_info;
  // open file, guaranteed to exist
  char* buffer;
  int err = asprintf(&buffer, "%d.swap", getpid());
  if (err == -1) {
      fprintf(stderr, "Failed to print to allocated string");
  }
  int fd = open(buffer, O_RDWR, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH | S_IRGRP | S_IWGRP);
  if (fd == -1) {
    fprintf(stderr, "Open failed (swap_to_mem): %s\n", strerror(errno));
  }
  free(buffer);

  // seek to the swap_info->file_loc byte
  lseek(fd, swap_info->file_loc , SEEK_SET); // is this over by one

  // write info in file to the memory location
  char page_buffer[PAGE];
  err = read(fd, page_buffer, PAGE);
  if (err == -1) {
    fprintf(stderr, "Read failed (swap_to_mem): %s\n", strerror(errno));
  }
  close(fd); // done with file

  // temporarily make the page writable
  mprotect(addr, PAGE, PROT_READ | PROT_WRITE);
  for (int i = 0; i < PAGE; ++i) {
    addr[i] = page_buffer[i];
  }
  // reset back to readonly
  mprotect(addr, PAGE, PROT_READ);

  // remove file info from list
  unlink_swap_location(node);
  free(node->swap_info);
  free(node);
}

// moves PAGE bytes from fd loc offset to addr
// remember to keep the fd pointer immutable for future correctness
void file_to_mem(int fd, off_t offset, char* addr) {
  // temporarily make the page writable
  mprotect(addr, PAGE, PROT_READ | PROT_WRITE);
  int err = pread(fd, addr, PAGE, offset);
  if (err == -1) {
    fprintf(stderr, "Read failed (file_to_mem): %s\n", strerror(errno));
  }
  // reset back to readonly
  mprotect(addr, PAGE, PROT_READ);
}

void page_fault_handler(char* addr) {
  // align read address to nearest start of page
  addr = align_to_page(addr);

  // if not resident, we bring to mem
  if (!is_resident(addr)) {
    // if next write exceeds lorm, evict with FIFO
    handle_exceed_lorm(true);

    // bring page to mem
    mprotect(addr, PAGE, PROT_READ);

    // if swap file exists, load page info from file
    node* node_with_swap_loc = has_swap(addr);
    if (node_with_swap_loc) {
      swap_to_mem(node_with_swap_loc, addr);
    }

    // if this is a mem mapped file, bring in contents of file to mem
    // TODO: try speed this slow func
    node* alloc_node = get_alloc_node(addr);
    if (alloc_node->info->is_file) { // alloc_node guaranteed to exist
      off_t offset = addr - (char*) alloc_node->info->start; // offset is mul of PAGE
      file_to_mem(alloc_node->info->fd, offset, addr);
    }

    // add page to fifo queue
    alloc_t* new_page = malloc(sizeof(alloc_t));
    new_page->start = addr;
    new_page->size = PAGE;
    new_page->dirty = false;
    // set file info in the resident page (if any) to support write backs to fd
    new_page->is_file = alloc_node->info->is_file;
    new_page->fd = alloc_node->info->fd;
    append_resident_pages(new_page);

    return;
  }

  // else we have written to a readonly page, set to PROT_READ | PROT_WRITE
  set_to_dirty(addr);
  mprotect(addr, PAGE, PROT_READ | PROT_WRITE);
}

void sigsegv_handler(int sig, siginfo_t *info, void *ucontext) {
  // check if faulting mem address is within controlled region, else unregister
  bool is_controlled = false;
  node* head = allocs;
  char* start_add = NULL;
  char* end_add = NULL;
  char* cur_pos = (char*) info->si_addr; // Memory location which caused fault
  while (head) {
    start_add = (char*) head->info->start;
    end_add = (char*) head->info->start + head->info->size;
    if (start_add <= cur_pos && cur_pos < end_add) {
      is_controlled = true;
      break;
    }
    head = head->next;
  }
  if (!is_controlled) {
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
  size = round_up(size);

  // allocate new mmap
  void* start = mmap(NULL, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (start == MAP_FAILED) {
      fprintf(stderr, "Failed to map mem because %s\n", strerror(errno));
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
      // remove all pages that are resident (now invalid)
      remove_resident(mem, block_size);

      // remove all pages that are in swap (now invalid)
      remove_swap(mem, block_size);

      // remove mapping
      int res = munmap(mem, block_size);
      if (res) {
        fprintf(stderr, "Failed to munmap\n");
      }

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
  // install sigsegv handler, if not done yet
  struct sigaction sa;
  sa.sa_sigaction = sigsegv_handler;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);

  // round up size to next multiple of page sz
  size = round_up(size);

  // allocate new mmap
  void* start = mmap(NULL, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (start == MAP_FAILED) {
      fprintf(stderr, "Failed to map mem because %s\n", strerror(errno));
      return NULL;
  }

  // create new info struct and add to state
  alloc_t* new_alloc = malloc(sizeof(alloc_t));
  new_alloc->start = start;
  new_alloc->size = size;
  new_alloc->dirty = false;
  new_alloc->is_file = true;
  new_alloc->fd = fd;
  append_allocs(new_alloc);

  // if file is shorter than size bytes, we 0 fill it till size bytes
  struct stat statbuf;
  int err = fstat(fd, &statbuf);
  if (err == -1) {
      fprintf(stderr, "Failed fstat: %s\n", strerror(errno));
      return NULL;
  }
  // file is too small, need to extend with 0s
  if ((size_t) statbuf.st_size < size) {
    err = ftruncate(fd, size);
    if (err == -1) {
      fprintf(stderr, "Failed ftruncate: %s\n", strerror(errno));
      return NULL;
    }
  }

  return start;
}

