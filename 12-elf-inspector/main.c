// main.c — elf_inspect
// Challenge [12]: ELF64 Binary Inspector
//
// Build: gcc -Wall -Wextra -o elf_inspect main.c
// Usage: ./elf_inspect <elf-binary>
//
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *sh_type_str(uint32_t type) {
    switch (type) {
        case SHT_NULL:     return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB:   return "SYMTAB";
        case SHT_STRTAB:   return "STRTAB";
        case SHT_RELA:     return "RELA";
        case SHT_HASH:     return "HASH";
        case SHT_DYNAMIC:  return "DYNAMIC";
        case SHT_NOTE:     return "NOTE";
        case SHT_NOBITS:   return "NOBITS";
        case SHT_REL:      return "REL";
        case SHT_DYNSYM:   return "DYNSYM";
        default:           return "OTHER";
    }
}

static const char *pt_type_str(uint32_t type) {
    switch (type) {
        case PT_NULL:    return "NULL";
        case PT_LOAD:    return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP:  return "INTERP";
        case PT_NOTE:    return "NOTE";
        case PT_PHDR:    return "PHDR";
        case PT_TLS:     return "TLS";
        default:         return "OTHER";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf-binary>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Could not open ELF file.\n");
        return 1;
    }   
    struct stat stat;
    if (-1 == fstat(fd, &stat)) {
        perror("Could not get fstats\n");
        return 1;
    }
    uint8_t *base = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)base;

    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Not an ELF file\n");
        munmap(base, stat.st_size);
        close(fd);
        return 1;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "Not a 64-bit ELF\n");
        munmap(base, stat.st_size);
        close(fd);
        return 1;
    }
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "Not little-endian ELF\n");
        munmap(base, stat.st_size);
        close(fd);
        return 1;
    }

    Elf64_Shdr *shdrs = (Elf64_Shdr *)(base + ehdr->e_shoff);
    Elf64_Phdr *phdrs = (Elf64_Phdr *)(base + ehdr->e_phoff);

    printf("=== Section Headers ===\n");
    printf("[idx] %-20s %-10s %-10s %-8s flags\n", "Name", "Type", "Offset", "Size");
    char *shstrtab_data = (char *)(base + shdrs[ehdr->e_shstrndx].sh_offset);
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        char *name = shstrtab_data + shdrs[i].sh_name;
        printf("[%3d] %-20s %-10s 0x%08lx %8lu  0x%lx\n",
            i, name, sh_type_str(shdrs[i].sh_type),
            (unsigned long)shdrs[i].sh_offset,
            (unsigned long)shdrs[i].sh_size,
            (unsigned long)shdrs[i].sh_flags);
    }

    printf("\n=== Program Headers ===\n");
    printf("%-10s %-18s %-10s %-8s %-8s flags\n", "Type", "VAddr", "Offset", "FileSz", "MemSz");
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        char flags[4] = "---";
        if (ph->p_flags & PF_R) flags[0] = 'r';
        if (ph->p_flags & PF_W) flags[1] = 'w';
        if (ph->p_flags & PF_X) flags[2] = 'x';
        printf("%-10s 0x%016lx 0x%08lx %8lu %8lu  %s\n",
            pt_type_str(ph->p_type),
            (unsigned long)ph->p_vaddr,
            (unsigned long)ph->p_offset,
            (unsigned long)ph->p_filesz,
            (unsigned long)ph->p_memsz,
            flags);
    }

    printf("\n=== Symbol Table (.symtab — globals only) ===\n");
    int found_symtab = 0;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type != SHT_SYMTAB) continue;
        found_symtab = 1;
        if (shdrs[i].sh_entsize == 0) continue;
        char *strtab = (char *)(base + shdrs[shdrs[i].sh_link].sh_offset);
        size_t count = shdrs[i].sh_size / shdrs[i].sh_entsize;
        Elf64_Sym *syms = (Elf64_Sym *)(base + shdrs[i].sh_offset);
        printf("%-40s %-8s %-8s %-18s size\n", "Name", "Bind", "Type", "Value");
        for (size_t j = 0; j < count; ++j) {
            if (ELF64_ST_BIND(syms[j].st_info) != STB_GLOBAL) continue;
            const char *sym_name = strtab + syms[j].st_name;
            int stype = ELF64_ST_TYPE(syms[j].st_info);
            const char *type_str = stype == STT_FUNC   ? "FUNC"   :
                                   stype == STT_OBJECT ? "OBJECT" :
                                   stype == STT_NOTYPE ? "NOTYPE" : "OTHER";
            printf("%-40s GLOBAL   %-8s 0x%016lx %lu\n",
                sym_name, type_str,
                (unsigned long)syms[j].st_value,
                (unsigned long)syms[j].st_size);
        }
    }
    if (!found_symtab)
        printf("no .symtab found\n");

    // TODO: munmap + close
    munmap(base, stat.st_size);
    close(fd);

    return 0;
}
