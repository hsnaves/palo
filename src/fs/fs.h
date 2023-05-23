
/* A good source of information on the Alto filesystem is the OS source
 * code itself, which be found at:
 * https://xeroxalto.computerhistory.org/Indigo/AltoSource/OSSOURCES.DM!2_/.index.html
 */

#ifndef __FS_FS_H
#define __FS_FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Constants. */
#define MAX_PAGE_SIZE                  1024U
#define NAME_LENGTH                      40U

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

/* Error constants. */
#define ERROR_NO_ERROR                     0
#define ERROR_UNKNOWN                     -1
#define ERROR_FS_UNCHECKED                -2
#define ERROR_INVALID_OF                  -3
#define ERROR_INVALID_FE                  -4
#define ERROR_INVALID_DE                  -5
#define ERROR_DISK_FULL                   -6
#define ERROR_DIR_FULL                    -7
#define ERROR_FILE_NOT_FOUND              -8
#define ERROR_DIR_NOT_FOUND               -9
#define ERROR_INVALID_NAME               -10
#define ERROR_INVALID_MODE               -11
#define ERROR_READ_ONLY                  -12
#define ERROR_NOT_DIRECTORY              -13
#define ERROR_ALREADY_EXIST              -14
#define ERROR_END                        -15

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
    int error;                    /* Indicates the error, according
                                   * to the ERROR constants.
                                   */
    int read_only;                /* File was open in read-only mode. */
    int modified;                 /* Indicates that file was modified. */
    int new_file;                 /* If this is a new file. */
    struct file_entry dir_fe;     /* The file_entry of the parent
                                   * directory, for new files.
                                   */
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
                                   * 0xFFFE for bad pages.
                                   */
        struct serial_number sn;  /* The file serial number. */
    } label;
    uint8_t *data;                /* Page data. */
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
    uint16_t sector_words;        /* Number of words per sector. */
};

/* The file information (from the leader page).
 * Roughly corresponds to the LD structure in AltoFileSys.D.
 */
struct file_info {
    time_t created;               /* The time the file was created. */
    time_t written;               /* The time the file was written. */
    time_t read;                  /* The time the file was accessed. */

    uint8_t name_length;          /* The original length of the name. */
    char name[NAME_LENGTH];       /* The name of the file (hint). */

    uint8_t props[420];           /* Copy of the properties. */
    uint8_t spare[20];            /* Spare data. */

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
    struct page *pages;           /* Filesystem pages (sectors). */
    uint16_t length;              /* Total length of the filesystem
                                   * in pages.
                                   */
    uint16_t disk_length;         /* The length of a single disk. */
    uint16_t sector_bytes;        /* Number of bytes per sector. */

    uint16_t *ref_count;          /* A reference count on how
                                   * many directories point to the
                                   * corresponding pages.
                                   */
    uint16_t *bitmap;             /* Disk usage bitmap. */
    uint8_t  *data;               /* The disk raw data. */
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

/* Translates the error to a string.
 * Returns the string representation of the error.
 */
const char *fs_error(int error);

/* Reads the contents of the disk from a file named `filename`.
 * This will populate the disk number `disk_num`.
 * Returns TRUE on success.
 */
int fs_load_image(struct fs *fs,
                  const char *filename, uint16_t disk_num);

/* Writes the contents of the disk to a file named `filename`.
 * This will dump the disk number `disk_num`.
 * Returns TRUE on success.
 */
int fs_save_image(const struct fs *fs,
                  const char *filename, uint16_t disk_num);

/* Wipes the contents of the free pages in the disk. */
void fs_wipe_free_pages(struct fs *fs);

/* Formats the filesystem.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_format(struct fs *fs, int *error);

/* Installs the boot file onto the filesystem.
 * The name of the boot file is given in `name`.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_install_boot(struct fs *fs,
                    const char *name,
                    int *error);

/* Checks the integrity of the filesystem.
 * Returns TRUE on success.
 */
int fs_check_integrity(struct fs *fs);

/* Scavenges the filesystem for what it can salvage.
 * This will write files in the current directory.
 * The parameter `fp` is to where the status messages should be
 * printed.
 */
void fs_scavenge(const struct fs *fs, FILE *fp);

/* Obtains the file_entry of the SysDir.
 * The `sysdir_fe` will be populated with the corresponding SysDir
 * file_entry.
 * Returns TRUE on success.
 */
int fs_get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe);

/* Obtains an open_file.
 * The file is specified by `fe` and the open file is stored in `of`.
 * This function starts at the leader page if `skip_leader` is set
 * to FALSE. To open in read-only mode the parameter `read_only`
 * should be set to TRUE.
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_get_of(const struct fs *fs,
              const struct file_entry *fe,
              int skip_leader, int read_only,
              struct open_file *of);

/* Opens a file for reading or writing.
 * The file is specified by `name` and the open file is stored in `of`.
 * The `mode` specifies how to open the file. The valid modes are:
 *   "r"  -> opens for reading,
 *   "r+" -> opens the file for reading and writing, but does
 *           not create the file if it does not exist,
 *   "w"  -> opens the file for reading and writing, create the
 *           file if it does not exist, but if exists, it truncates
 *           the file on open,
 *   "w+" -> opens the file for reading and writing, but does not
 *           truncate if the file already exists,
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_open(struct fs *fs,
            const char *name,
            const char *mode,
            struct open_file *of);

/* Closes the open_file `of`.
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_close(struct fs *fs, struct open_file *of);

/* Opens the file in read-only mode.
 * The same parameters of fs_open() apply here, except `mode`, which
 * is assumed to be "r". This function is useful because the `fs`
 * reference is const.
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_open_ro(const struct fs *fs,
               const char *name,
               struct open_file *of);

/* Closes the open_file `of`, which was opened in read-only mode.
 * As in fs_open_ro(), the reference of `fs` is const.
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_close_ro(const struct fs *fs, struct open_file *of);

/* Reads `len` bytes of an open file `of` to `dst`.
 * If `dst` is NULL, the file pointer in `of` is still updated,
 * but no actual bytes are copied.
 * Returns the number of bytes read. Any errors are written to
 * `of->error`.
 */
size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len);

/* Writes `len` bytes of an open file `of` from `src`.  If `src` is
 * NULL, the file is zeroed. The parameter `extends` tells the function
 * to allocate free pages when it reaches the end of the file,
 * thereby extending the existing file.
 * Returns the number of written bytes. Any errors are written to
 * `of->error`.
 */
size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend);

/* Truncates the given file.
 * The file to be truncated is given by the parameter `of`.
 * Returns TRUE on success. Any errors are written to `of->error`.
 */
int fs_truncate(struct fs *fs, struct open_file *of);

/* Creates a link from `name` to the file pointed by the file_entry `fe`.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_link(struct fs *fs,
            const char *name,
            const struct file_entry *fe,
            int *error);

/* Removes the link to the name `name`.
 * If `remove_underlying` is set to TRUE, the underlying file is removed
 * from the filesystem.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_unlink(struct fs *fs,
              const char *name,
              int remove_underlying,
              int *error);

/* Creates a new directory at `name`.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_mkdir(struct fs *fs,
             const char *name,
             int *error);

/* Determines the file length.
 * The `fe` specifies the file. The file length is returned in `length`.
 * The `error` parameter, if provided, returns the details about the
 * error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length, int *error);

/* Obtains the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc. The `error` parameter, if provided, returns the details about
 * the error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_get_file_info(const struct fs *fs,
                     const struct file_entry *fe,
                     struct file_info *finfo,
                     int *error);

/* Sets the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc. The `error` parameter, if provided, returns the details about
 * the error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_set_file_info(struct fs *fs,
                     const struct file_entry *fe,
                     const struct file_info *finfo,
                     int *error);

/* Scans (lists) one directory.
 * The directory is specified by the file_entry `dir_fe` parameter.
 * The callback `cb` is used to list the directory. The `arg` is
 * an extra parameter passed to the callback. The `error` parameter,
 * if provided, returns the details about the error, in case the
 * function fails.
 * Returns TRUE on success.
 */
int fs_scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                      scan_directory_cb cb, void *arg, int *error);

/* Resolves a name in the filesystem.
 * The name of the file to find is given in `name`. If the file
 * is found, `found` will return TRUE.
 * The parameter `fe` will be populated with information about
 * the file found (such as leader page virtual disk address, etc.).
 * The parameter `dir_fe`, if provided, will be populated with
 * the information about the directory that contains the file.
 * If the file was not found, the `base_name` will be populated with
 * the unresolved suffix (the base name).
 * Returns TRUE on success.
 */
int fs_resolve_name(const struct fs *fs, const char *name, int *found,
                    struct file_entry *fe, struct file_entry *dir_fe,
                    const char **base_name);

/* Updates the DiskDescriptor file.
 * The `error` parameter, if provided, returns more information about
 * the error, in case the function fails.
 * Returns TRUE on success.
 */
int fs_update_disk_descriptor(struct fs *fs, int *error);

/* Extracts a file from the filesystem.
 * The `name` is the name of the file in the filesystem.
 * The `output_filename` specifies the filename of the output file to
 * write.
 * Returns TRUE on success.
 */
int fs_extract_file(const struct fs *fs, const char *name,
                    const char *output_filename);

/* Inserts a file into the filesystem.
 * The `input_filename` specifies the filename of the input file to
 * read.
 * The `name` is the name of the file in the filesystem.
 * Returns TRUE on success.
 */
int fs_insert_file(struct fs *fs, const char *input_filename,
                   const char *name);

/* Copies a file from `src` to `dst`.
 * Returns TRUE on success.
 */
int fs_copy(struct fs *fs, const char *src, const char *dst);

/* Prints the contents of a directory to `fp`.
 * The directory is specified by the parameter `dir_name`.
 * To print more information, the `verbose` parameter should be set
 * to a positive number (the bigger the more verbose the output).
 * Returns TRUE on success.
 */
int fs_print_directory(const struct fs *fs,
                       const char *dir_name,
                       int verbose, FILE *fp);

#endif /* __FS_FS_H */
