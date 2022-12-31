#ifndef __FS_FS_INTERNAL_H
#define __FS_FS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Constants. */

/* Offsets within the leader page data. */
#define LD_OFF_CREATED                    0U
#define LD_OFF_WRITTEN                    4U
#define LD_OFF_READ                       8U
#define LD_OFF_NAME                      12U
#define LD_OFF_PROPS                     52U
#define LD_OFF_SPARE                    472U
#define LD_OFF_PROPBEGIN                492U
#define LD_OFF_PROPLEN                  493U
#define LD_OFF_CONSECUTIVE              494U
#define LD_OFF_CHANGESN                 495U
#define LD_OFF_DIRFPHINT                496U
#define LD_OFF_LASTPAGEHINT             506U

/* Offsets within the directory entry. */
#define DIR_OFF_FILE_ENTRY                2U
#define DIR_OFF_NAME                     12U

/* Other constants. */
#define DIR_ENTRY_TYPE_SHIFT              10
#define DIR_ENTRY_LEN_MASK            0x3FFU

/* Offsets in the DiscDescriptor file. */
#define DESCR_OFF_GEOMETRY                0U
#define DESCR_OFF_LAST_SN                 8U
#define DESCR_OFF_BLANK                  12U
#define DESCR_OFF_DISKBT_SIZE            14U
#define DESCR_OFF_VERSIONS_KEPT          16U
#define DESCR_OFF_FREE_PAGES             18U

/* For manipulating bits in the bitmap. */
#define IDX(vda) ((vda) >> 4)
#define BIT(vda) (15 - ((vda) & 15))
#define VDA(idx, bit) (((idx) << 4) + (15 - (bit)))

/* Data structures and types. */

/* Defines the type of the callback function for fs_scan_properties().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_property_cb)(const struct fs *fs,
                                const struct file_entry *fe,
                                uint8_t type, uint8_t length,
                                const uint8_t *data, void *arg);

/* Functions. */

/* Advances to the next page (if at the end of the current page).
 * The open_file to advance is given in parameter `of`.
 * Returns TRUE on success.
 */
int fs_advance_page(const struct fs *fs, struct open_file *of);

/* Increments the last serial number of the filesystem. */
void fs_increment_serial_number(struct fs *fs);

/* Updates the filesystem (disk) metadata.
 * This includes updating the bitmask, the number
 * of free available pages, etc.
 */
void fs_update_disk_metadata(struct fs *fs);

/* Finds a free page within the filesystem.
 * The virtual disk address is returned in `free_vda`.
 * Returns TRUE on success.
 */
int fs_find_free_page(struct fs *fs, uint16_t *free_vda);

/* Function to read the leader page.
 * The file to read the leader page is given in parameter `fe`.
 * The page is stored in `data`.
 * Returns TRUE on success.
 */
int fs_read_leader_page(const struct fs *fs,
                        const struct file_entry *fe,
                        uint8_t data[PAGE_DATA_SIZE]);

/* Scans the file properties from the leader page.
 * The parameter `fe` specifies the file to be scanned.
 * The callback `cb` is used to scan the properties. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_properties(const struct fs *fs,
                       const struct file_entry *fe,
                       scan_property_cb cb, void *arg);


/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter. For the conversion the geometry is specified
 * in `dg`.
 * Returns TRUE on success.
 */
int real_to_virtual(const struct geometry *dg, uint16_t rda, uint16_t *vda);

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter. For the conversion the geometry is specified
 * in `dg`.
 * Returns TRUE on success.
 */
int virtual_to_real(const struct geometry *dg, uint16_t vda, uint16_t *rda);

/* Copies the name from `data` at the offset `offset` to `name` and set
 * the proper NUL byte at the end of the string.
 */
void read_name(const uint8_t *data, size_t offsoet,
               char name[NAME_LENGTH]);

/* Writes the name to `data` from `name` and set the
 * proper string length at the offset `offset`.
 */
void write_name(uint8_t *data, size_t offset,
                const char name[NAME_LENGTH]);

/* Reads a word (in big endian format).
 * The source data is given by `data`, and the offset where
 * the word is in `offset`.
 * Returns the word.
 */
uint16_t read_word_be(const uint8_t *data, size_t offset);

/* Writes a word (in big endian format).
 * The destination data is given by `data`, and the offset where
 * the word is in `offset`. The word to be written is in `w`.
 */
void write_word_be(uint8_t *data, size_t offset, uint16_t w);

/* Reads a serial_number object.
 * The source data is given by `data`, and the offset where
 * the serial_number is in `offset`. The serial_number stored in `sn`.
 */
void read_serial_number(const uint8_t *data, size_t offset,
                        struct serial_number *sn);

/* Writes a serial_number object.
 * The destination data is given by `data`, and the offset where
 * the serial_number is in `offset`. The serial_number to be written is in
 * `sn`.
 */
void write_serial_number(uint8_t *data, size_t offset,
                         const struct serial_number *sn);

/* Reads a file_entry data.
 * The source data is given by `data`, and the offset where
 * the file_entry is in `offset`. The file_entry is stored in `fe`.
 */
void read_file_entry(const uint8_t *data, size_t offset,
                     struct file_entry *fe);

/* Writes a file_entry data.
 * The destination data is given by `data`, and the offset where
 * the file_entry is in `offset`. The file_entry to be written is in
 * `fe`.
 */
void write_file_entry(uint8_t *data, size_t offset,
                      const struct file_entry *fe);

/* Reads a file_position data.
 * The source data is given by `data`, and the offset where
 * the file_position is in `offset`. The file_position is stored in `pos`.
 */
void read_file_position(const uint8_t *data, size_t offset,
                        struct file_position *pos);

/* Writes a file_position data.
 * The destination data is given by `data`, and the offset where
 * the file_position is in `offset`. The file_position to be written is in
 * `pos`.
 */
void write_file_position(uint8_t *data, size_t offset,
                         const struct file_position *pos);

/* Reads the geometry data.
 * The source data is given by `data`, and the offset where
 * the geometry is in `offset`. The geometry is stored in `dg`.
 */
void read_geometry(const uint8_t *data, size_t offset,
                   struct geometry *dg);

/* Writes the geometry data.
 * The destination data is given by `data`, and the offset where
 * the geomtry is in `offset`. The geometry to be written is in
 * `dg`.
 */
void write_geometry(uint8_t *data, size_t offset,
                    const struct geometry *dg);

/* Obtains a time_t from the Alto filesystem.
 * The alto data is located at `offset` in `data`.
 * Returns the time.
 */
time_t read_alto_time(const uint8_t *data, size_t offset);

/* Serializes a time_t to a format suitable for the Alto filesystem.
 * The alto data is located at `offset` in `data`. The time to
 * serialize is in the parameter `time`.
 */
void write_alto_time(uint8_t *data, size_t offset, time_t time);

#endif /* __FS_FS_INTERNAL_H */
