
/* A good source of information on the Alto filesystem is the OS source
 * code itself, which be found at:
 * https://xeroxalto.computerhistory.org/Indigo/AltoSource/OSSOURCES.DM!2_/.index.html
 */

#ifndef __FS_FS_H
#define __FS_FS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Constants. */
#define NAME_LENGTH                      40U
#define PAGE_DATA_SIZE                  512U

/* To interpret the bits of serial_number.word1. */
#define SN_DIRECTORY                 0x8000U
#define SN_RAND                      0x4000U
#define SN_NOLOG                     0x2000U
#define SN_PART1_MASK                0x1FFFU

/* To interpret the version. */
#define VERSION_FREE                 0xFFFFU
#define VERSION_BAD                  0xFFFEU

/* Types of directory entries. */
#define DIR_ENTRY_VALID                   1U
#define DIR_ENTRY_MISSING                 0U

/* Data structures and types. */

/* The serial number of a file.
 * Corresponds to the SN structure in AltoFileSys.D.
 */
struct serial_number {
    uint16_t word1;               /* Some bits are interesting here:
                                   * 0x8000: indicates it is a directory
                                   * 0x4000: random bit (not used)
                                   * 0x2000: no longer used
                                   * 0x1FFF: part 1
                                   */
    uint16_t word2;               /* Second word of serial number
                                   * (part2).
                                   */
};

/* Describes a particular file by label & leader page disk address.
 * Corresponds to the FP structure in AltoFileSys.D.
 */
struct file_entry {
    struct serial_number sn;      /* The serial number of the file. */
    uint16_t version;             /* The file version. */
    uint16_t blank;               /* Expansion to 2-word vda. */
    uint16_t leader_vda;          /* The vda (virtual disk address)
                                   * of the leader page of the file.
                                   */
};

/* Structure to represent a position within an open file.
 * Corresponds to the FA structure in AltoFileSys.D.
 */
struct file_position {
    uint16_t vda;                 /* The virtual disk address of the
                                   * current page.
                                   */
    uint16_t pgnum;               /* The index of the page within the file.
                                   * Leader page has index 0.
                                   */
    uint16_t pos;                 /* The position with respect to the
                                   * current page.
                                   */
};

/* Structure to represent an open file.
 * Roughly corresponds to the CFA structure in AltoFileSys.D.
 */
struct open_file {
    struct file_entry fe;         /* The file_entry information. */
    struct file_position pos;     /* The file_position information. */
    int eof;                      /* If it reached the end of file. */
    int error;                    /* Indicates the file has error. */
};

/* Structure representing a filesystem page (sector). */
struct page {
    uint16_t page_vda;            /* The virtual disk address of the page. */
    uint16_t header[2];           /* Page header. */
    struct {
        uint16_t next_rda;        /* The (real) DA of next page. */
        uint16_t prev_rda;        /* The (real) DA of previous page. */
        uint16_t unused;
        uint16_t nbytes;          /* Number of used bytes in the page. */
        uint16_t file_pgnum;      /* Page number of a file. */
        uint16_t version;         /* Notable values:
                                   * 0xFFFF for free pages.
                                   * 0xFFF2 for bad pages.
                                   */
        struct serial_number sn;  /* The file serial number. */
    } label;
    uint8_t data[512];            /* Page data. */
};

/* Structure to represent an entry within a directory.
 * Corresponds to the DV structure in AltoFileSys.D.
 */
struct directory_entry {
    uint16_t type;                /* The type of this entry. */
    uint16_t length;              /* The length of this entry. */
    struct file_entry fe;         /* A pointer to file_entry information. */
    uint8_t name_length;          /* The original length of the name. */
    char name[NAME_LENGTH];       /* The name of the file. */
};

/* The file information (from the leader page).
 * Roughly corresponds to the LD structure in AltoFileSys.D.
 */
struct file_info {
    uint8_t name_length;          /* The original length of the name. */
    char name[NAME_LENGTH];       /* The name of the file (hint). */
    time_t created;               /* The time the file was created. */
    time_t written;               /* The time the file was written. */
    time_t read;                  /* The time the file was accessed. */

    uint8_t propbegin;            /* The index for properties. */
    uint8_t proplen;              /* The number of words for properties. */
    uint8_t consecutive;          /* The consecutive value. */
    uint8_t change_sn;            /* The change serial number value. */

    struct file_entry fe;         /* Hint to the file entry. */
    struct file_position last_page;/* Hint to the last page. */
};

/* Structure representing the disk geometry. */
struct geometry {
    uint16_t num_disks;           /* Number of disks. */
    uint16_t num_cylinders;       /* Number of cylinders. */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */
};

/* Structure representing the filesystem. */
struct fs {
    struct geometry dg;           /* The disk geometry. */
    uint16_t disk_num;            /* The disk number. */
    struct page *pages;           /* Filesystem pages (sectors). */
    uint16_t length;              /* Total length of the filesystem
                                   * in pages.
                                   */

    uint16_t *bitmap;             /* Disk usage bitmap. */
    uint16_t bitmap_size;         /* The size of the bitmap. */
    uint16_t free_pages;          /* Number of free pages. */
    struct serial_number last_sn; /* Last used serial number. */
};

/* Defines the type of the callback function for fs_scan_properties().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_property_cb)(const struct fs *fs,
                                const struct file_entry *fe,
                                uint8_t type, uint8_t length,
                                const uint8_t *data, void *arg);

/* Defines the type of the callback function for fs_scan_files().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_files_cb)(const struct fs *fs,
                             const struct file_entry *fe,
                             void *arg);

/* Defines the type of the callback function for fs_scan_directory().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_directory_cb)(const struct fs *fs,
                                 const struct directory_entry *de,
                                 void *arg);

/* Functions. */

/* Initializes the fs variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void fs_initvar(struct fs *fs);

/* Destroys the fs object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void fs_destroy(struct fs *fs);

/* Creates a new fs object.
 * This obeys the initvar / destroy / create protocol.
 * The parameter `dg` specifies the disk geometry.
 * Returns TRUE on success.
 */
int fs_create(struct fs *fs, struct geometry dg);

/* Reads the contents of the disk from a file named `filename`.
 * Returns TRUE on success.
 */
int fs_load_image(struct fs *fs, const char *filename);

/* Writes the contents of the disk to a file named `filename`.
 * Returns TRUE on success.
 */
int fs_save_image(const struct fs *fs, const char *filename);

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
int fs_real_to_virtual(const struct fs *fs, uint16_t rda, uint16_t *vda);

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
int fs_virtual_to_real(const struct fs *fs, uint16_t vda, uint16_t *rda);

/* Checks the integrity of the filesystem.
 * The level of integrity to check is given by `level`.
 * If `level` is negative, the maximum level is assumed.
 * Returns TRUE on success.
 */
int fs_check_integrity(struct fs *fs, int level);

/* Updates the filesystem metadata.
 * This includes updating the bitmask, the number
 * of free available pages, etc.
 */
void fs_update_metadata(struct fs *fs);

/* Finds a free page within the filesystem.
 * The virtual disk address is returned in `free_vda`.
 * Returns TRUE on success.
 */
int fs_find_free_page(struct fs *fs, uint16_t *free_vda);

/* Converts the virtual disk address of the leader page `leader_vda` of a
 * file to a file_entry object `fe`.
 * Returns TRUE on success.
 */
int fs_file_entry(const struct fs *fs, uint16_t leader_vda,
                  struct file_entry *fe);

/* Checks the file_entry with the data on disk.
 * The file_entry to check is in parameter `fe`.
 * Returns TRUE if it is a valid file_entry object.
 */
int fs_check_file_entry(const struct fs *fs, const struct file_entry *fe);

/* Checks the directory_entry.
 * The directory_entry to check is in parameter `de`.
 * Returns TRUE if it is a valid directory_entry object.
 */
int fs_check_directory_entry(const struct fs *fs,
                             const struct directory_entry *de);

/* Opens a file for reading or writing.
 * The file is specified by `fe` and the open file is stored in `of`.
 * This function starts at the leader page.
 * Returns TRUE on success.
 */
int fs_open(const struct fs *fs,
            const struct file_entry *fe,
            struct open_file *of);

/* Checks the open_file for errors.
 * The parameter `of` specifies the file to check.
 * Returns TRUE if the open_file has no errors.
 */
int fs_check_of(const struct fs *fs, struct open_file *of);

/* Reads `len` bytes of an open file `of` to `dst`.
 * If `dst` is NULL, the file pointer in `of` is still updated,
 * but no actual bytes are copied.
 * Returns the number of bytes read.
 */
size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len);

/* Advances to the next page (if at the end of the current page).
 * The open_file to advance is given in parameter `of`.
 * Returns TRUE on success.
 */
int fs_advance_page(const struct fs *fs, struct open_file *of);

/* Writes `len` bytes of an open file `of` from `src`.  If `src` is
 * NULL, the file is zeroed. The parameter `extends` tells the function
 * to allocate free pages when it reaches the end of the file,
 * thereby extending the existing file.
 * Returns the number of written bytes.
 */
size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend);

/* Trims the file to have the size matching the current position
 * in the file.
 * Returns TRUE on success.
 */
int fs_trim(struct fs *fs, struct open_file *of);

/* Determines a file length.
 * The `fe` determines the file.
 * The file length is returned in `length`.
 * Optionally, the `end_of` returns a pointer to the end of the
 * file (if provided).
 * Returns TRUE on success.
 */
int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length, struct open_file *end_of);

/* Obtains the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc.
 * Returns TRUE on success.
 */
int fs_file_info(const struct fs *fs,
                 const struct file_entry *fe,
                 struct file_info *finfo);

/* Scans the file properties from the leader page.
 * The parameter `fe` specifies the file to be scanned.
 * The callback `cb` is used to scan the properties. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_properties(const struct fs *fs,
                       const struct file_entry *fe,
                       scan_property_cb cb, void *arg);

/* Finds a file in the filesystem.
 * The name of the file to find is given in `name`.
 * The parameter `fe` will be populated with information about
 * the file found (such as leader page virtual disk address, etc.).
 * The parameter `dir_fe`, if provided, will be populated with
 * the information about the directory that contains the file.
 * Returns TRUE on success.
 */
int fs_find_file(const struct fs *fs, const char *name,
                 struct file_entry *fe, struct file_entry *dir_fe);

/* Scans the files in the filesystem.
 * The callback `cb` is used to scan the filesystem. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_files(const struct fs *fs, scan_files_cb cb, void *arg);

/* Scans one directory..
 * The directory is specified by the file_entry `fe` parameter.
 * The callback `cb` is used to scan the directory. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_directory(const struct fs *fs, const struct file_entry *fe,
                      scan_directory_cb cb, void *arg);


/* Updates the DiskDescriptor file.
 * Returns TRUE on success.
 */
int fs_update_descriptor(struct fs *fs);

/* Updates the leader page of a file with the correct hints.
 * Returns TRUE on success.
 */
int fs_update_leader_page(struct fs *fs, const struct file_entry *fe);

/* Extracts a file from the filesystem.
 * The `fe` contains information about the location of the file in
 * in the filesystem. The `output_filename` specifies the filename
 * of the output file to write. If it is to include the leader page
 * data, the `include_leader_page` parameter should be set to TRUE.
 * Returns TRUE on success.
 */
int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename, int include_leader_page);

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

/* Obtains a time_t from the Alto filesystem.
 * The alto data is located at `offset` in `data`.
 * Returns the time.
 */
time_t read_alto_time(const uint8_t *data, size_t offset);

#endif /* __FS_FS_H */
