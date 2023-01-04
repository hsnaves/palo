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

/* Defines the type of the callback function for scan_properties().
 * The callback should return TRUE to continue scanning, and FALSE to
 * stop scanning.
 */
typedef int (*scan_property_cb)(const struct fs *fs,
                                const struct file_entry *fe,
                                uint8_t type, uint8_t length,
                                const uint8_t *data, void *arg);

/* Defines the type of the callback function for scan_files().
 * The callback should return TRUE to continue scanning, and FALSE to
 * stop scanning.
 */
typedef int (*scan_files_cb)(const struct fs *fs,
                             const struct file_entry *fe,
                             void *arg);

/* Functions. */


/* basic.c */


/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter. For the conversion, the geometry is specified
 * in `dg`.
 * Returns TRUE on success.
 */
int real_to_virtual(const struct geometry *dg, uint16_t rda, uint16_t *vda);

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter. For the conversion, the geometry is specified
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
 * the serial_number is in `offset`. The serial_number to be written
 * is in `sn`.
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
 * the file_position is in `offset`. The file_position is stored
 * in `pos`.
 */
void read_file_position(const uint8_t *data, size_t offset,
                        struct file_position *pos);

/* Writes a file_position data.
 * The destination data is given by `data`, and the offset where
 * the file_position is in `offset`. The file_position to be written
 * is in `pos`.
 */
void write_file_position(uint8_t *data, size_t offset,
                         const struct file_position *pos);

/* Reads a directory_entry data.
 * The source data is given by `data`, and the offset where
 * the directory_entry is in `offset`. The `header_only` parameter
 * indicates that this function will only decode the first word, which
 * contains `de->type` and `de->length`.
 * The directory_entry is stored in `de`.
 */
void read_directory_entry(const uint8_t *data, size_t offset,
                          int header_only, struct directory_entry *de);

/* Writes the directory_entry data.
 * The destination data is given by `data`, and the offset where
 * the directory_entry is in `offset`. The directory_entry to be written
 * is in `de`.
 */
void write_directory_entry(uint8_t *data, size_t offset,
                           const struct directory_entry *de);

/* Reads the geometry data.
 * The source data is given by `data`, and the offset where
 * the geometry is in `offset`. The geometry is stored in `dg`.
 */
void read_geometry(const uint8_t *data, size_t offset,
                   struct geometry *dg);

/* Writes the geometry data.
 * The destination data is given by `data`, and the offset where
 * the geometry is in `offset`. The geometry to be written is in
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

/* Updates the directory_entry length based on the name length.
 * The directory_entry `de` is modified upon exit.
 */
void update_directory_entry_length(struct directory_entry *de);


/* check.c */


/* Checks the file_entry with the data on disk.
 * The file_entry to check is in parameter `fe`.
 * If the parameter `verbose` is set to TRUE, error messages are
 * printed to the standard error.
 * Returns TRUE if it is a valid file_entry object.
 */
int check_file_entry(const struct fs *fs,
                     const struct file_entry *fe,
                     int verbose);

/* Checks the open_file for errors.
 * The parameter `of` specifies the file to check.
 * The error is populated in `of->error`.
 * Returns TRUE if the open_file has no errors.
 */
int check_of(const struct fs *fs, struct open_file *of);


/* dir.c */


/* Updates the reference counts for files. */
void update_reference_counts(struct fs *fs);

/* Reads a directory_entry from an open_file `of`.
 * The directory_entry is stored in `de`.
 * Returns TRUE if an entry was fetched.
 */
int fetch_directory_entry(const struct fs *fs,
                          struct open_file *of,
                          struct directory_entry *de);

/* Appends the empty entries at the end of the directory.
 * The parameter `of` is the open_file of the currently open directory.
 * The `empty_length` specifies how many empty words are left.
 * The `extend` parameter is passed to fs_write() to extend the
 * directory.
 * Returns TRUE on success.
 */
int append_empty_entries(struct fs *fs,
                         struct open_file *of,
                         size_t empty_length,
                         int extend);

/* Compresses the entries in a directory.
 * The parameter `dir_fe` specifies the directory.
 * The `used_length` and `empty_length` return statistics about the
 * usage of the directory file. Those statistics are measured in words.
 * Returns TRUE on success.
 */
int compress_directory(struct fs *fs,
                       const struct file_entry *dir_fe,
                       size_t *used_length,
                       size_t *empty_length);

/* Adds one entry to the directory.
 * The parameter `dir_fe` specifies the directory.  The `de` is the
 * directory_entry to be added.
 * Returns TRUE if the entry was added successfully.
 */
int add_directory_entry(struct fs *fs,
                        const struct file_entry *dir_fe,
                        const struct directory_entry *de);

/* Removes one entry from the directory.
 * The parameter `dir_fe` specifies the directory.  The `remove_name`
 * is the name of the entry to be removed.
 * Returns TRUE if the entry was removed successfully.
 */
int remove_directory_entry(struct fs *fs,
                           const struct file_entry *dir_fe,
                           const char *remove_name);


/* disk.c */


/* Increments the last serial number of the filesystem. */
void increment_serial_number(struct fs *fs);

/* Updates the filesystem (disk) metadata.
 * This includes updating the bitmask, the number
 * of free available pages, etc.
 */
void update_disk_metadata(struct fs *fs);

/* Finds a free page within the filesystem.
 * The virtual disk address is returned in `free_vda`. The parameter
 * `last_vda`, if provided, contains a pointer to the last vda of the
 * current file. This parameter is used to allocate contiguous regions
 * on the disk. If provided, this function tries to allocate the page
 * next to `last_vda`, if it is free. When `last_vda` is not provided,
 * this function allocates the first page of the largest contiguous
 * free region available.
 * Returns TRUE on success.
 */
int allocate_page(struct fs *fs, uint16_t *free_vda,
                  const uint16_t *last_vda);

/* Returns multiple pages to the filesystem.
 * The virtual disk address of the first page is in `vda`.
 * If `follow` is set to TRUE, this function traverses all
 * pages linked by the `next_rda` field in the page label,
 * until the last page.
 */
void free_pages(struct fs *fs, uint16_t vda, int follow);


/* file.c */


/* Creates a new directory at `name`.
 * The parameter `is_sysdir` specifies if this directory is the SysDir
 * directory.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int make_directory(struct fs *fs,
                   const char *name,
                   int is_sysdir,
                   int *error);


/* meta.c */


/* Function to read the leader page.
 * The file to read the leader page is given in parameter `fe`.
 * The page is stored in `data`.
 */
void read_leader_page(const struct fs *fs,
                      const struct file_entry *fe,
                      uint8_t data[PAGE_DATA_SIZE]);

/* Writes the leader page.
 * The file whose leader page is to be written is specified by `fe`.
 * The `finfo` parameter specifies the data of the leader page.
 */
void write_leader_page(struct fs *fs,
                       const struct file_entry *fe,
                       const struct file_info *finfo);

/* Updates the leader page of a file with the correct hints.
 * The file whose leader page is to be written is specified by `fe`.
 */
void update_leader_page(struct fs *fs, const struct file_entry *fe);


/* scan.c */


/* Scans the file properties from the leader page.
 * The parameter `fe` specifies the file to be scanned.
 * The callback `cb` is used to scan the properties. The `arg` is
 * an extra parameter passed to the callback.
 */
void scan_properties(const struct fs *fs,
                     const struct file_entry *fe,
                     scan_property_cb cb, void *arg);

/* Scans the files in the filesystem.
 * The callback `cb` is used to scan the filesystem. The `arg` is
 * an extra parameter passed to the callback.
 */
void scan_files(const struct fs *fs, scan_files_cb cb, void *arg);

/* Scans one directory.
 * The directory is specified by the file_entry `dir_fe` parameter.
 * The callback `cb` is used to scan the directory. The `arg` is
 * an extra parameter passed to the callback.
 */
void scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                    scan_directory_cb cb, void *arg);


#endif /* __FS_FS_INTERNAL_H */
