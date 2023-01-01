
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
    uint8_t data[PAGE_DATA_SIZE]; /* Page data. */
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

/* Structure representing the disk geometry. */
struct geometry {
    uint16_t num_disks;           /* Number of disks. */
    uint16_t num_cylinders;       /* Number of cylinders. */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */
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

    struct geometry dg;           /* One of the properties of this file. */
    int has_dg;                   /* If it has the disk geometry. */

    struct file_entry fe;         /* Hint to the file entry. */
    struct file_position last_page;/* Hint to the last page. */
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
    int checked;                  /* If the filesystem was checked. */
};

/* Defines the type of the callback function for fs_scan_directory().
 * The callback should return TRUE to continue scanning, and FALSE to
 * stop scanning.
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

/* Checks the integrity of the filesystem.
 * Returns TRUE on success.
 */
int fs_check_integrity(struct fs *fs);

/* Obtains an open_file.
 * The file is specified by `fe` and the open file is stored in `of`.
 * This function starts at the leader page if `skip_leader` is set
 * to FALSE.
 * Returns TRUE on success.
 */
int fs_get_of(const struct fs *fs,
              const struct file_entry *fe,
              int skip_leader,
              struct open_file *of);

/* Opens a file for reading or writing.
 * The file is specified by `name` and the open file is stored in `of`.
 * The `mode` specifies how to open the file. The valid modes are:
 *   "r" -> opens for reading,
 *   "w" -> opens the file for writing.
 * Returns TRUE on success.
 */
int fs_open(const struct fs *fs,
            const char *name,
            const char *mode,
            struct open_file *of);

/* Closes the open_file `of`.
 * Returns TRUE on success.
 */
int fs_close(const struct fs *fs,
             struct open_file *of);

/* Reads `len` bytes of an open file `of` to `dst`.
 * If `dst` is NULL, the file pointer in `of` is still updated,
 * but no actual bytes are copied.
 * Returns the number of bytes read.
 */
size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len);

/* Obtains the file_entry of the SysDir.
 * The `sysdir_fe` will be populated with the corresponding SysDir
 * file_entry.
 * Returns TRUE on success.
 */
int fs_get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe);

/* Determines the file length.
 * The `fe` specifies the file. The file length is returned in `length`.
 * Returns TRUE on success.
 */
int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length);

/* Obtains the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc.
 * Returns TRUE on success.
 */
int fs_file_info(const struct fs *fs,
                 const struct file_entry *fe,
                 struct file_info *finfo);

/* Scans (lists) one directory.
 * The directory is specified by the file_entry `dir_fe` parameter.
 * The callback `cb` is used to list the directory. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                      scan_directory_cb cb, void *arg);

/* Resolves a name in the filesystem.
 * The name of the file to find is given in `name`. If the file
 * is found, `found` will return TRUE.
 * The parameter `fe` will be populated with information about
 * the file found (such as leader page virtual disk address, etc.).
 * The parameter `dir_fe`, if provided, will be populated with
 * the information about the directory that contains the file.
 * Returns TRUE on success.
 */
int fs_resolve_name(const struct fs *fs, const char *name, int *found,
                    struct file_entry *fe, struct file_entry *dir_fe);

/* Extracts a file from the filesystem.
 * The `name` is the name of the file in the filesystem.
 * The `output_filename` specifies the filename of the output file to
 * write.
 * Returns TRUE on success.
 */
int fs_extract_file(const struct fs *fs, const char *name,
                    const char *output_filename);

#endif /* __FS_FS_H */
