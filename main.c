#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        errx(EXIT_FAILURE, "usage: %s file-name", argv[0]);
    }

    if (elf_version(EV_CURRENT) == EV_NONE) {
        errx(EXIT_FAILURE, "ELF library initialization "
            "failed: %s", elf_errmsg(-1));
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        err(EXIT_FAILURE, "open '%s' failed", argv[1]);
    }

    Elf *e = elf_begin(fd, ELF_C_READ, NULL);
    if (e == NULL) {
        errx(EXIT_FAILURE, "elf_begin() failed: %s",
            elf_errmsg(-1));
    }

    if (elf_kind(e) != ELF_K_ELF) {
        errx(EXIT_FAILURE, "'%s' is not an ELF object",
            argv[1]);
    }

    Elf_Scn *scn = NULL;
    while ((scn = elf_nextscn(e, scn)) != NULL) {
        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) == NULL) {
            errx(EXIT_FAILURE, "getshdr() failed: %s.",
                elf_errmsg(-1));
        }

        if (shdr.sh_type == SHT_DYNAMIC) {
            break;
        }
    }

    if (scn == NULL) {
        errx(EXIT_FAILURE, "SHT_DYNAMIC section not found",
            elf_errmsg(-1));
    }

    Elf_Data *data = elf_getdata(scn, NULL);
    if (data == NULL) {
        errx(EXIT_FAILURE, "elf_getdata() failed: %s",
            elf_errmsg(-1));
    }

    uintptr_t sonameptr;
    uintptr_t strtabptr;

    size_t num_entries = data->d_size/sizeof(GElf_Dyn);
    for (int i = 0; i < num_entries; i++) {
        GElf_Dyn dyn;
        if (gelf_getdyn(data, i, &dyn) == NULL) {
            errx(EXIT_FAILURE, "gelf_getdyn() failed: %s",
                elf_errmsg(-1));
        }

        switch (dyn.d_tag) {
            case DT_SONAME:
                sonameptr = dyn.d_un.d_ptr;
                break;
            case DT_STRTAB:
                strtabptr = dyn.d_un.d_ptr;
                break;
        }
    }

    Elf_Scn *strtabscn = gelf_offscn(e, strtabptr);
    if (strtabscn == NULL) {
        errx(EXIT_FAILURE, "gelf_offscn() failed: %s",
            elf_errmsg(-1));
    }

    size_t strtabndx = elf_ndxscn(strtabscn);
    if (strtabndx == SHN_UNDEF) {
        errx(EXIT_FAILURE, "elf_ndxscn() failed: %s",
            elf_errmsg(-1));
    }

    char *soname = elf_strptr(e, strtabndx, sonameptr);
    if (soname == NULL) {
        errx(EXIT_FAILURE, "elf_strptr() failed: %s.",
            elf_errmsg(-1));
    }

    printf("soname: %s\n", soname);
        
    elf_end(e);
    close(fd);
    return 0;
}
