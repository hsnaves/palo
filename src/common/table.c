
#include <stdlib.h>
#include <string.h>

#include "common/table.h"
#include "common/utils.h"

/* Functions. */
unsigned int string_hash(const char *s, size_t len)
{
    unsigned int hash;
    size_t i;

    hash = 0;
    for (i = 0; i < len; i++) {
        hash += (unsigned int) s[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

void table_initvar(struct table *t)
{
    t->table = NULL;
}

void table_destroy(struct table *t)
{
    if (t->table) free((void *) t->table);
    t->table = NULL;
}

int table_create(struct table *t)
{
    size_t size;
    table_initvar(t);

    t->num_slots = 32;
    size = t->num_slots * sizeof(struct string_node *);
    t->table = (struct string_node **) malloc(size);
    if (unlikely(!t->table)) {
        report_error("table: create: memory exhausted");
        return FALSE;
    }
    memset(t->table, 0, size);
    t->num_elements = 0;

    return TRUE;
}

void table_clear(struct table *t)
{
    size_t size;
    size = t->num_slots * sizeof(struct string_node *);
    memset(t->table, 0, size);
}

struct string_node *table_find(struct table *t, const struct string *str)
{
    struct string_node *n;
    unsigned int slot;

    slot =str->hash % t->num_slots;
    n = t->table[slot];
    while (n) {
        if (n->str.hash == str->hash && n->str.len == str->len) {
            if (strncmp(str->s, n->str.s, str->len) == 0)
                return n;
        }
        n = n->next;
    }

    return NULL;
}

int table_add(struct table *t, struct string_node *n)
{
    unsigned int slot;

    if (t->num_elements >= 2 * t->num_slots) {
        struct string_node **new_table;
        struct string_node **nn_ptr;
        struct string_node *nn;
        unsigned int new_slot;
        unsigned int new_num_slots;
        size_t size;

        new_num_slots = 2 * t->num_slots;
        size = new_num_slots * sizeof(struct string_node *);
        new_table = (struct string_node **) malloc(size);
        if (unlikely(!new_table)) {
            report_error("table: add: memory exhausted");
            return FALSE;
        }
        memset(new_table, 0, size);

        for (slot = 0; slot < t->num_slots; slot++) {
            nn = t->table[slot];
            while (nn) {
                t->table[slot] = nn->next;

                /* Add nodes in reverse order to keep the
                 * the original ordering.
                 */
                new_slot = nn->str.hash % new_num_slots;
                nn_ptr = &new_table[new_slot];
                nn->next = NULL;
                while (nn_ptr[0]) {
                    nn_ptr = &nn_ptr[0]->next;
                }
                nn_ptr[0] = nn;

                nn = t->table[slot];
            }
        }

        free((void *) t->table);
        t->table = new_table;
        t->num_slots = new_num_slots;
    }

    slot = n->str.hash % t->num_slots;
    n->next = t->table[slot];
    t->table[slot] = n;
    t->num_elements++;
    return TRUE;
}
