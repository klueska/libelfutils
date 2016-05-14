#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LD_SO_CACHE "/etc/ld.so.cache"

#define CACHEMAGIC_OLD "ld.so-1.7.0"
#define CACHEMAGIC_NEW "glibc-ld.so.cache1.1"

#define ALIGN_TYPE_OFFSET(addr, type) \
    ((__alignof__(type) - 1) & (~(__alignof__(type) - 1)))

#define FLAGS_ELF    0x00000001
#define FLAGS_I386   0x00000800
#define FLAGS_X86_64 0x00000300

/* Older versions of libc had a very simple format for ld.so.cache. The file
 * simply listed the number of libary entries, followed by the entries
 * themselves, followed by a string table holding strings pointed to by the
 * library entries. This format is summarized below:

        CACHEMAGIC_OLD
        nlibs
        libs[0]
        ...
        libs[nlibs-1]
        string[0] -- Address of offset 0 in strtab
        ...
        string[n]

 * For glibc 2.2 and beyond, a new format was created so that each
 * library entry could hold more meta-data about the libraries they
 * reference. To preserve backwards compatibility, the new format was
 * embedded in the old format inside its string table (simply moving
 * all existing strings further down in the string table). This makes
 * sense for backwards comaptibility because code that could parse the
 * old format  still works (the offsets for strings pointed to by
 * the library entries are just larger now).
 *
 * However, it adds complications when parsing for the new format
 * because the new format' header needs to be aligned on an 8 byte
 * boundary (potentially pushing the start address of the string table
 * down a few bytes). A summary of the new format embedded in the old
 * format with annotations on the start address of the string table
 * can be seen below:

        CACHEMAGIC_OLD
        nlibs
        libs[0]
        ...
        libs[nlibs-1]
        pad (align for new format) -- Address of offset 0 in the old strtab
        CACHEMAGIC_NEW             -- Address of offset 0 in the new strtab
        nlibs
        len_strings
        unused -- 20 bytes reserved for future extensions
        libs[0]
        ...
        libs[newnlibs-1]
        string[0]
        ...
        string[n]
*/

struct header_old
{
  char magic[sizeof(CACHEMAGIC_OLD) - 1];
  uint32_t nlibs;
};

struct libentry_old
{
  int32_t flags;  /* 0x01 indicated ELF library. */
  uint32_t key;   /* String table index. */
  uint32_t value; /* String table index. */
};

struct header_new
{
  char magic[sizeof(CACHEMAGIC_NEW) - 1];
  uint32_t nlibs;     /* Number of entries.  */
  uint32_t stringslen; /* Size of string table. */
  uint32_t unused[5]; /* Leave space for future extensions
                         and align to 8 byte boundary. */
};

struct libentry_new
{
  int16_t flags;        /* Flags bits determine arch and library type. */
  uint32_t key;         /* String table index. */
  uint32_t value;       /* String table index. */
  uint32_t osversion;   /* Required OS version. */
  uint64_t hwcap;       /* Hwcap entry. */
};


bool validatePtr(char *base, uint32_t limit, char *ptr, uint32_t offset)
{
    if (ptr + offset < base)
        return false;

    if (ptr + offset >= base + limit)
        return false;

    return true;
}


int main(int argc, char **argv)
{
    FILE *file = fopen(LD_SO_CACHE, "rb");
    if (file == NULL) {
        err(EXIT_FAILURE, "fopen '%s' failed", LD_SO_CACHE);
    }

    if (fseek(file, 0, SEEK_END) == -1) {
        fclose(file);
        err(EXIT_FAILURE, "fseek() failed");
    }

    size_t filelen = ftell(file);
    if (filelen == -1) {
        fclose(file);
        err(EXIT_FAILURE, "ftell() failed");
    }

    if (fseek(file, 0, SEEK_SET) == -1) {
        fclose(file);
        err(EXIT_FAILURE, "fseek() failed");
    }

    char *buffer = malloc(filelen);
    if (buffer == NULL) {
        fclose(file);
        err(EXIT_FAILURE, "malloc() failed");
    }

    size_t ret = fread(buffer, 1, filelen, file);
    if (ret != filelen) {
        free(buffer);
        fclose(file);
        err(EXIT_FAILURE, "fread() failed");
    }

    /* Construct pointers to all of the important regions in the old
     * format: the header, the libentry array, the strtab. */
    char *bufptr = buffer;
    uint32_t offset = 0;

    struct header_old *header_old = (struct header_old*)bufptr;
    offset = sizeof(struct header_old);
    if (!validatePtr(buffer, filelen, bufptr, offset)) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }
    bufptr += offset;
    
    struct libentry_old *libs_old = (struct libentry_old*)bufptr;
    offset = header_old->nlibs * sizeof(struct libentry_old);
    if (!validatePtr(buffer, filelen, bufptr, offset)) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }
    bufptr += offset;

    char *strtab = (char *)bufptr;

    /* Assuming we are working with the new format (it is the only
     * fomat we support), the header and all of its library entries
     * are embedded in the old format's string table. The header
     * itself is aligned on an 8 byte boundary, so we need to align
     * our bufptr here to get it to point to the new header. */
    offset = ALIGN_TYPE_OFFSET((uintptr_t)bufptr, struct header_new);
    if (!validatePtr(buffer, filelen, bufptr, offset)) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }
    bufptr += offset;

    /* Construct pointers to all of the important regions in the new
     * format: the header, the libentry array, and the new strtab
     * (which starts at the same address as the aligned header_new
     * pointer). */
    struct header_new *header_new = (struct header_new*)bufptr;
    offset = sizeof(struct header_new);
    if (!validatePtr(buffer, filelen, bufptr, offset)) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }
    bufptr += offset;

    struct libentry_new *libs_new = (struct libentry_new*)bufptr;
    offset = header_new->nlibs * sizeof(struct libentry_new);
    if (!validatePtr(buffer, filelen, bufptr, offset)) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }
    bufptr += offset;

    strtab = (char *)header_new;

    /* Adjust bufptr to add on the additional size of the strings
     * contained in the string table. At this point, bufptr should
     * point to an address just beyond the end of the file. */
    bufptr += header_new->stringslen;

    if ((bufptr - buffer) != filelen) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }

    if (strncmp(header_old->magic,
                CACHEMAGIC_OLD,
                sizeof(CACHEMAGIC_OLD) - 1) != 0) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }

    if (strncmp(header_new->magic,
                CACHEMAGIC_NEW,
                sizeof(CACHEMAGIC_NEW) - 1) != 0) {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }

    /* Make sure the very last character in the buffer is a '\0'. This
     * way, no matter what strings we index in the string table, we
     * know they will never run beyond the end of the file buffer when
     * extracting them. */
    if (*(bufptr - 1) != '\0') {
        free(buffer);
        fclose(file);
        errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
    }

    /* Validate all string offsets are within the bounds of the strtab. */
    for (int i = 0; i < header_new->nlibs; i++) {
        if (strtab + libs_new[i].key >= bufptr) {
            free(buffer);
            fclose(file);
            errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
        }
        if (strtab + libs_new[i].value >= bufptr) {
            free(buffer);
            fclose(file);
            errx(EXIT_FAILURE, "error parsing '%s'", LD_SO_CACHE);
        }
    }

    printf("header_old->magic: %.*s\n", sizeof(CACHEMAGIC_OLD) - 1, header_old->magic);
    printf("header_old->nlibs: %d\n", header_old->nlibs);
    printf("header_new->magic: %.*s\n", sizeof(CACHEMAGIC_NEW) - 1, header_new->magic);
    printf("header_new->nlibs: %d\n", header_new->nlibs);
    for (int i = 0; i < header_new->nlibs; i++) {
        printf("libs_new[%d].flags: %p\n", i, libs_new[i].flags);
        printf("libs_new[%d].key: %s\n", i, &strtab[libs_new[i].key]);
        printf("libs_new[%d].value: %s\n", i, &strtab[libs_new[i].value]);
        printf("libs_new[%d].osversion: %d\n", i, libs_new[i].osversion);
        printf("libs_new[%d].hwcap: %ld\n", i, libs_new[i].hwcap);
    }

    free(buffer);
    fclose(file);
    return 0;
}
