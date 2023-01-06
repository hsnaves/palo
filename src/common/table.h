
#ifndef __COMMON_TABLE_H
#define __COMMON_TABLE_H

#include <stddef.h>

/* Data structures and types. */

/* Structure to represent a string. */
struct string {
    const char *s;                /* The pointer to the string. */
    size_t len;                   /* The length of the string. */
    unsigned int hash;            /* The hash of the string. */
};

/* Structure used to represent a list of strings. */
struct string_node {
    struct string str;            /* The actual string. */
    struct string_node *next;     /* The next element of the list. */
};

/* Structure representing a hash table. */
struct table {
    struct string_node **table;   /* Pointer to table elements. */
    unsigned int num_slots;       /* The size of the array `table`. */
    unsigned int num_elements;    /* The number of elements in the table. */
};

/* Functions. */

/* Implementation of the Jenkins OAAT hash.
 * The input string is given by `s` and has length `len`.
 * Returns the hash of the string.
 */
unsigned int string_hash(const char *s, size_t len);

/* Initializes the table variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void table_initvar(struct table *t);

/* Destroys the table object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void table_destroy(struct table *t);

/* Creates a new table object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int table_create(struct table *t);

/* Clears the contents of the table. */
void table_clear(struct table *t);

/* Finds a given string in the table.
 * The string must have its hash computed already.
 * Returns a pointer to the node structure holding this entry in the table.
 */
struct string_node *table_find(struct table *t, const struct string *str);

/* Adds a string_node to the table.
 * The parameter `n` contains the node to be added. The string in `n->str`
 * must be populated already. The value of `n->next` will be modified by
 * this function.
 * Returns TRUE on success.
 */
int table_add(struct table *t, struct string_node *n);

/* Re-hashes the table with a different number of slots.
 * The new number of slots is given by the parameter `num_slots`.
 * Returns TRUE on success.
 */
int table_rehash(struct table *t, unsigned int num_slots);


#endif /* __COMMON_TABLE_H */
