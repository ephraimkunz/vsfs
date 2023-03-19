// A simple file-system simulator based off of vsfs (Very Simple File System) in
// Operating Systems: Three Easy Pieces
// (https://pages.cs.wisc.edu/~remzi/OSTEP/file-implementation.pdf)

// TODO:
// Handle storing file data.
// Use more than one block per inode.


#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE_IN_BYTES 4096
#define DISK_SIZE_IN_BLOCKS 64
#define DATA_REGION_IN_BLOCKS 56
#define INODE_TABLE_IN_BLOCKS 5
#define INODE_SIZE_IN_BYTES 256
#define MAX_DIRECTORY_NAME_LENGTH 30

static void *disk;

// Bitmaps
typedef uint64_t word_t;
enum { BITS_PER_WORD = sizeof(word_t) * CHAR_BIT };
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)

void set_bit(word_t *words, int n) {
  words[WORD_OFFSET(n)] |= (1 << BIT_OFFSET(n));
}

void clear_bit(word_t *words, int n) {
  words[WORD_OFFSET(n)] &= ~(1 << BIT_OFFSET(n));
}

int get_bit(word_t *words, int n) {
  word_t bit = words[WORD_OFFSET(n)] & (1 << BIT_OFFSET(n));
  return bit != 0;
}

int first_clear_bit(word_t *words) {
  for (int i = 0; i < BLOCK_SIZE_IN_BYTES * CHAR_BIT; ++i) {
    if (!get_bit(words, i)) {
      return i;
    }
  }

  return -1;
}

// Other data structures

enum inode_type { inode_type_file, inode_type_directory };

typedef struct superblock {
  int inode_bitmap_block;
  int data_bitmap_block;
  int inode_table_start;
  int data_region_start;
  int root_inode;
} superblock;

typedef struct inode {
  int type;
  int size;
  void *data_block_pointers[12];
} inode;

typedef struct direntry {
  int inum;
  char name[MAX_DIRECTORY_NAME_LENGTH + 1];
} direntry;

// Functions

word_t *inode_bitmap() {
  superblock *fs = disk;
  return disk + fs->inode_bitmap_block * BLOCK_SIZE_IN_BYTES;
}

word_t *data_bitmap() {
  superblock *fs = disk;
  return disk + fs->data_bitmap_block * BLOCK_SIZE_IN_BYTES;
}

inode *inode_for_index(int index) {
  superblock *fs = disk;
  return disk + fs->inode_table_start * BLOCK_SIZE_IN_BYTES +
         index * INODE_SIZE_IN_BYTES;
}

void *data_block_for_index(int index) {
  superblock *fs = disk;
  return disk + (fs->data_region_start + index) * BLOCK_SIZE_IN_BYTES;
}

void init_direntry_datablock_with_default_directories(void *data_block) {
  direntry *entry = data_block;
  entry->inum = 0;
  strncpy(entry->name, ".", MAX_DIRECTORY_NAME_LENGTH);

  entry = &entry[1];
  entry->inum = 0;
  strncpy(entry->name, "..", MAX_DIRECTORY_NAME_LENGTH);
}

void fs_create_disk() {
  disk = calloc(DISK_SIZE_IN_BLOCKS, BLOCK_SIZE_IN_BYTES);

  superblock *fs = disk;
  fs->inode_bitmap_block = 1;
  fs->data_bitmap_block = 2;
  fs->inode_table_start = 3;
  fs->data_region_start = 3 + INODE_TABLE_IN_BLOCKS;
  fs->root_inode = 0;

  // Create root directory.

  set_bit(inode_bitmap(), 0);
  inode *in = inode_for_index(0);
  in->type = inode_type_directory;

  set_bit(data_bitmap(), 0);
  void *data_block = data_block_for_index(0);
  in->data_block_pointers[0] = data_block;

  init_direntry_datablock_with_default_directories(data_block);
}

inode *root_inode() {
  superblock *fs = disk;
  return inode_for_index(fs->root_inode);
}

void print_inode_recursive(char *name, inode *inode, int level) {
  for (int i = 0; i < level; ++i) {
    printf(" ");
  }

  printf("%s\n", name);

  // If this is a . or .. directory, don't print it because we'd get into a
  // loop.
  int length = strlen(name);
  if ((length == 1 && name[0] == '.') ||
      (length == 2 && name[0] == '.' && name[1] == '.')) {
    return;
  }

  superblock *fs = disk;
  if (inode->type == inode_type_directory) {
    direntry *entry = inode->data_block_pointers[0];

    int entry_index = 0;
    // Don't continue if the entry has no name.
    while (entry->name[0]) {
      print_inode_recursive(entry->name, inode_for_index(entry->inum),
                            level + 1);

      entry = &entry[++entry_index];
    }
  }
}

void print_tree() { print_inode_recursive("/", root_inode(), 0); }

inode *directory_inode(inode *curr_dir_inode, char *directory_name) {
  if (curr_dir_inode->type != inode_type_directory) {
    return NULL;
  }

  direntry *entry = curr_dir_inode->data_block_pointers[0];

  int entry_index = 0;
  while (entry->name[0]) {
    if (0 == strcmp(entry->name, directory_name)) {
      return inode_for_index(entry->inum);
    }

    entry = &entry[++entry_index];
  }

  return NULL;
}

void create_inode(inode *parent, char *name, int inode_type) {
  assert(parent->type == inode_type_directory);

  // Find a free inode.
  superblock *fs = disk;
  word_t *bitmap = inode_bitmap();
  int inum = first_clear_bit(bitmap);
  set_bit(bitmap, inum);

  // Find a data block
  bitmap = data_bitmap();
  int data_block_num = first_clear_bit(bitmap);
  set_bit(bitmap, data_block_num);
  void *data_block = data_block_for_index(data_block_num);

  // Init the new inode.
  inode *inode = inode_for_index(inum);
  inode->data_block_pointers[0] = data_block;
  inode->type = inode_type;

  // Add reference to directory.
  direntry *entry = parent->data_block_pointers[0];
  int index = 0;
  while (entry->name[0]) {
    entry = &entry[++index];
  }

  strncpy(entry->name, name, MAX_DIRECTORY_NAME_LENGTH);
  entry->inum = inum;

  // If this is a directory, init with . and .. dir entries.
  if (inode_type == inode_type_directory) {
    init_direntry_datablock_with_default_directories(data_block);
  }
}

void create_file(char *path) {
  if (path[0] == '/') {
    path++;
  } else {
    // We don't support relative paths yet.
    return;
  }

  // Walk the path of directories to find the direct parent.
  inode *in = root_inode();

  char *current_path_component, *string, *tofree;
  tofree = string = strdup(path);

  while ((current_path_component = strsep(&string, "/")) != NULL)
    if (!string) {
      // The remaining string is used up, which means this is the filename
      create_inode(in, current_path_component, inode_type_file);
    } else {
      in = directory_inode(in, current_path_component);
      if (!in) {
        break;
      }
    }

  free(tofree);
}

void create_dir(char *path) {
  if (path[0] == '/') {
    path++;
  } else {
    // We don't support relative paths yet.
    return;
  }

  // Walk the path of directories to find the direct parent.
  inode *in = root_inode();

  char *current_path_component, *string, *tofree;
  tofree = string = strdup(path);

  while ((current_path_component = strsep(&string, "/")) != NULL)
    if (!string) {
      // The remaining string is used up, which means this is the new directory
      // name
      create_inode(in, current_path_component, inode_type_directory);
    } else {
      in = directory_inode(in, current_path_component);
      if (!in) {
        break;
      }
    }

  free(tofree);
}

int main() {
  fs_create_disk();
  print_tree();
  printf("\n\n");

  create_file("/test.txt");
  create_dir("/testdir");
  create_file("/testdir/test1.txt");
  create_file("/testdir/test2.txt");
  print_tree();
}