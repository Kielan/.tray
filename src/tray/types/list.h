/* These structs are the foundation for all linked lists in the lib system.
 * Doubly-linked lists start from a List and contain elements beginning
 * with Link. */

#pragma once

/* All structs which are put into linked lists begin with this. */
typedef struct Link {
  struct Link *next, *prev;
} Link;

/* Subclass of Link. Use this when it is not worth defining a custom one. */
typedef struct LinkData {
  struct LinkData *next, *prev;
  void *data;
} LinkData;

/* Never change the size of this! structs_genfile.c detects ptr_size with it. */
typedef struct List {
  void *first, *last;
} List;

/* 8 byte alignment! */
