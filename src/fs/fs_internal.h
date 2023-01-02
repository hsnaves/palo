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


/* check.c */


/* Checks the file_entry with the data on disk.
 * The file_entry to check is in parameter `fe`.
 * Returns TRUE if it is a valid file_entry object.
 */
int check_file_entry(const struct fs *fs, const struct file_entry *fe);

/* Checks the directory_entry.
 * The directory_entry to check is in parameter `de`.
 * Returns TRUE if it is a valid directory_entry object.
 */
int check_directory_entry(const struct fs *fs,
                          const struct directory_entry *de);

/* Checks the file properties of a given file.
 * The parameter `fe` specifies the file.
 * Returns TRUE if the properties are valid.
 */
int check_file_properties(const struct fs *fs,
                          const struct file_entry *fe);

/* Checks if the contents of the directory are valid.
 * The parameter `dir_fe` specifies the directory.
 * Returns TRUE if the directory contents are valid.
 */
int check_directory_contents(const struct fs *fs,
                             const struct file_entry *dir_fe);

/* Checks the open_file for errors.
 * The parameter `of` specifies the file to check.
 * Returns TRUE if the open_file has no errors.
 */
int check_of(const struct fs *fs, struct open_file *of);


/* dir.c */


/* Compresses the entries in a directory.
 * The parameter `dir_fe` specifies the directory.
 * The `used_length` and `empty_length` return statistics about the
 * usage of the directory file. Those statistics are measured in words.
 */
void compress_directory(struct fs *fs,
                        const struct file_entry *dir_fe,
                        size_t *used_length, size_t *empty_length);

/* Adds one entry to the directory.
 * The parameter `dir_fe` specifies the directory.
 * The `de` is the directory_entry to be added.
 * Returns TRUE if the entry was added successfully.
 */
int add_directory_entry(struct fs *fs,
                        const struct file_entry *dir_fe,
                        const struct directory_entry *de);


/* disk.c */


/* Increments the last serial number of the filesystem. */
void increment_serial_number(struct fs *fs);

/* Updates the filesystem (disk) metadata.
 * This includes updating the bitmask, the number
 * of free available pages, etc.
 */
void update_disk_metadata(struct fs *fs);

/* Finds a free page within the filesystem.
 * The virtual disk address is returned in `free_vda`.
 * Returns TRUE on success.
 */
int find_free_page(struct fs *fs, uint16_t *free_vda);

/* Updates the DiskDescriptor file.
 * Returns TRUE on success.
 */
int update_disk_descriptor(struct fs *fs);


/* file.c */


/* Converts the virtual disk address of the leader page `leader_vda` of a
 * file to a file_entry object `fe`.
 */
void get_file_entry(const struct fs *fs, uint16_t leader_vda,
                    struct file_entry *fe);

/* Obtains the file_entry object of the SysDir.
 * The file_entry is stored in `sysdir_fe`.
 */
void get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe);

/* Creates a new file_entry in the filesystem.
 * The `leader_vda` parameter specifies the VDA of the leader page.
 * If `directory` is set to TRUE, a directory is created.
 * The created file_entry is stored in `fe`.
 * Returns TRUE on success.
 */
void new_file_entry(struct fs *fs, uint16_t leader_vda, int directory,
                    struct file_entry *fe);

/* Obtains an open_file.
 * The file is specified by `fe` and the open file is stored in `of`.
 * This function starts at the leader page if `skip_leader` is set
 * to FALSE.
 */
void get_of(const struct fs *fs,
            const struct file_entry *fe,
            int skip_leader,
            struct open_file *of);

/* Advances to the next page.
 * The open_file to advance is given in parameter `of`.
 */
void advance_page(const struct fs *fs, struct open_file *of);

/* Reads `len` bytes of an open file `of` to `dst`.
 * If `dst` is NULL, the file pointer in `of` is still updated,
 * but no actual bytes are copied.
 * Returns the number of bytes read.
 */
size_t _read(const struct fs *fs, struct open_file *of,
             uint8_t *dst, size_t len);

/* Writes `len` bytes of an open file `of` from `src`.  If `src` is
 * NULL, the file is zeroed. The parameter `extends` tells the function
 * to allocate free pages when it reaches the end of the file,
 * thereby extending the existing file.
 * Returns the number of written bytes.
 */
size_t _write(struct fs *fs, struct open_file *of,
              const uint8_t *src, size_t len, int extend);

/* Trims the file to have the size matching the current position
 * in the file.
 */
void trim(struct fs *fs, struct open_file *of);


/* meta.c */


/* Function to read the leader page.
 * The file to read the leader page is given in parameter `fe`.
 * The page is stored in `data`.
 */
void read_leader_page(const struct fs *fs,
                      const struct file_entry *fe,
                      uint8_t data[PAGE_DATA_SIZE]);

/* Determines the file length.
 * The `fe` specifies the file. Optionally, the `end_of` returns a
 * pointer to the end of the file (if provided).
 * Returns the file length.
 */
size_t file_length(const struct fs *fs, const struct file_entry *fe,
                   struct open_file *end_of);

/* Obtains the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc.
 */
void file_info(const struct fs *fs,
               const struct file_entry *fe,
               struct file_info *finfo);

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

/* Resolves a name in the filesystem.
 * The name of the file to find is given in `name`. If the file
 * is found, `found` will return TRUE.
 * The parameter `fe` will be populated with information about
 * the file found (such as leader page virtual disk address, etc.).
 * The parameter `dir_fe`, if provided, will be populated with
 * the information about the directory that contains the file.
 * If the file was not found, the `suffix` will be populated with
 * the unresolved suffix.
 * Returns TRUE on success.
 */
void resolve_name(const struct fs *fs, const char *name, int *found,
                  struct file_entry *fe, struct file_entry *dir_fe,
                  const char **suffix);

#endif /* __FS_FS_INTERNAL_H */
