
#ifndef __COMMON_SERDES_H
#define __COMMON_SERDES_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* Structure to serialize / deserialize objects.
 * Integers are serialized in big-endian format.
 */
struct serdes {
    uint8_t *buffer;              /* The allocated data buffer. */
    size_t size;                  /* The size of the buffer. */
    size_t pos;                   /* The current position in the buffer. */
    int extend;                   /* A flag indicating that the buffer
                                   * should be extendd on demand.
                                   */
};

/* Functions. */

/* Initializes the serdes variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void serdes_initvar(struct serdes *sd);

/* Destroys the serdes object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void serdes_destroy(struct serdes *sd);

/* Creates a new serdes object.
 * This obeys the initvar / destroy / create protocol.
 * The `size` variable specifies the buffer size. The `extend` parameter
 * tells the serdes object to extend the buffer on demand.
 * Returns TRUE on success.
 */
int serdes_create(struct serdes *sd, size_t size, int extend);

/* Rewinds the position to the beginning. */
void serdes_rewind(struct serdes *sd);

/* Verifies that the buffer did not overflow.
 * Returns TRUE is the buffer did not overflow.
 */
int serdes_verify(struct serdes *sd);

/* Extends the buffer to the size in `size`.
 * Returns TRUE on success.
 */
int serdes_extend(struct serdes *sd, size_t size);

/* Reads the contents of the file into the buffer.
 * The name of the file to read is in the parameter `filename`.
 * Returns TRUE on success.
 */
int serdes_read(struct serdes *sd, const char *filename);

/* Writes the contents of the buffer to a file.
 * The name of the file to write is in the parameter `filename`.
 * Returns TRUE on success.
 */
int serdes_write(const struct serdes *sd, const char *filename);

/* Deserializes an uint8_t value.
 * Returns the value.
 */
uint8_t serdes_get8(struct serdes *sd);

/* Deserializes an uint16_t value.
 * Returns the value.
 */
uint16_t serdes_get16(struct serdes *sd);

/* Deserializes an uint32_t value.
 * Returns the value.
 */
uint32_t serdes_get32(struct serdes *sd);

/* Deserializes a boolean value.
 * Returns the value.
 */
int serdes_get_bool(struct serdes *sd);

/* Deserializes an array of uint8_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_get8_array(struct serdes *sd, uint8_t *arr, size_t num);

/* Deserializes an array of uint16_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_get16_array(struct serdes *sd, uint16_t *arr, size_t num);

/* Deserializes an array of uint32_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_get32_array(struct serdes *sd, uint32_t *arr, size_t num);

/* Serializes an uint8_t value.
 * The value to serialize is in the parameter `v`.
 */
void serdes_put8(struct serdes *sd, uint8_t v);

/* Serializes an uint16_t value.
 * The value to serialize is in the parameter `v`.
 */
void serdes_put16(struct serdes *sd, uint16_t v);

/* Serializes an uint32_t value.
 * The value to serialize is in the parameter `v`.
 */
void serdes_put32(struct serdes *sd, uint32_t v);

/* Serializes a boolan value.
 * The value to serialize is in the parameter `v`.
 */
void serdes_put_bool(struct serdes *sd, int v);

/* Serializes an array of uint8_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_put8_array(struct serdes *sd, const uint8_t *arr, size_t num);

/* Serializes an array of uint16_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_put16_array(struct serdes *sd, const uint16_t *arr, size_t num);

/* Serializes an array of uint32_t values.
 * The `arr` is the parameter with the pointer to the array.
 * The number of elements is given by `num`.
 */
void serdes_put32_array(struct serdes *sd, const uint32_t *arr, size_t num);

#endif /* __COMMON_SERDES_H */
