#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include "so_util.h"

extern void fatal_error(const char *fmt, ...);

static so_module *head = NULL;
static so_module *tail = NULL;

static size_t page_align(size_t size) {
    size_t page = 0x1000;
    return (size + page - 1) & ~(page - 1);
}

static int map_fixed_region(uintptr_t addr, size_t size, int prot, void **out) {
    size = page_align(size);

    void *ptr = mmap(
        (void *)addr,
        size,
        prot,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );

    if (ptr == MAP_FAILED) {
        printf("mmap failed at 0x%08x size 0x%zx: %s\n",
               (unsigned int)addr,
               size,
               strerror(errno));
        return -1;
    }

    if ((uintptr_t)ptr != addr) {
        printf("mmap returned wrong address: wanted 0x%08x got %p\n",
               (unsigned int)addr,
               ptr);
        munmap(ptr, size);
        return -1;
    }

    *out = ptr;
    return 0;
}

void hook_thumb(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        return;
    }

    addr &= ~1;

    if (addr & 2) {
        uint16_t nop = 0xbf00;
        memcpy((void *)addr, &nop, sizeof(nop));
        addr += 2;
    }

    uint32_t hook[2];

    /*
     * Thumb:
     * LDR PC, [PC]
     * destination
     */
    hook[0] = 0xf000f8df;
    hook[1] = dst;

    memcpy((void *)addr, hook, sizeof(hook));
    __builtin___clear_cache((char *)addr, (char *)(addr + sizeof(hook)));
}

void hook_arm(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        return;
    }

    uint32_t hook[2];

    /*
     * ARM:
     * LDR PC, [PC, #-0x4]
     * destination
     */
    hook[0] = 0xe51ff004;
    hook[1] = dst;

    memcpy((void *)addr, hook, sizeof(hook));
    __builtin___clear_cache((char *)addr, (char *)(addr + sizeof(hook)));
}

void hook_addr(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        return;
    }

    if (addr & 1) {
        hook_thumb(addr, dst);
    } else {
        hook_arm(addr, dst);
    }
}

void so_flush_caches(so_module *mod) {
    if (!mod) {
        return;
    }

    if (mod->text_base && mod->text_size) {
        __builtin___clear_cache(
            (char *)mod->text_base,
            (char *)(mod->text_base + mod->text_size)
        );
    }

    if (mod->data_base && mod->data_size) {
        __builtin___clear_cache(
            (char *)mod->data_base,
            (char *)(mod->data_base + mod->data_size)
        );
    }
}

int so_load(so_module *mod, const char *filename, uintptr_t load_addr) {
    int res = 0;
    uintptr_t data_addr = 0;
    void *so_data = NULL;
    size_t so_size = 0;

    memset(mod, 0, sizeof(so_module));

    FILE *fp = fopen(filename, "rb");

    if (!fp) {
        printf("so_load: failed to open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        printf("so_load: invalid file size\n");
        fclose(fp);
        return -1;
    }

    so_size = (size_t)file_size;
    so_data = malloc(so_size);

    if (!so_data) {
        printf("so_load: malloc failed\n");
        fclose(fp);
        return -1;
    }

    if (fread(so_data, 1, so_size, fp) != so_size) {
        printf("so_load: fread failed\n");
        fclose(fp);
        free(so_data);
        return -1;
    }

    fclose(fp);

    if (memcmp(so_data, ELFMAG, SELFMAG) != 0) {
        printf("so_load: not an ELF file\n");
        res = -1;
        goto err_free_so;
    }

    mod->ehdr = (Elf32_Ehdr *)so_data;
    mod->phdr = (Elf32_Phdr *)((uintptr_t)so_data + mod->ehdr->e_phoff);
    mod->shdr = (Elf32_Shdr *)((uintptr_t)so_data + mod->ehdr->e_shoff);
    mod->shstr = (char *)((uintptr_t)so_data + mod->shdr[mod->ehdr->e_shstrndx].sh_offset);

    printf("ELF loaded in memory\n");
    printf("Program headers: %d\n", mod->ehdr->e_phnum);
    printf("Section headers: %d\n", mod->ehdr->e_shnum);

    for (int i = 0; i < mod->ehdr->e_phnum; i++) {
        if (mod->phdr[i].p_type != PT_LOAD) {
            continue;
        }

        void *prog_data = NULL;
        size_t align = mod->phdr[i].p_align ? mod->phdr[i].p_align : 0x1000;
        size_t prog_size = 0;

        if ((mod->phdr[i].p_flags & PF_X) == PF_X) {
            prog_size = ALIGN_MEM(mod->phdr[i].p_memsz, align);

            printf("Mapping TEXT at 0x%08x size 0x%zx\n",
                   (unsigned int)load_addr,
                   prog_size);

            res = map_fixed_region(
                load_addr,
                prog_size,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                &prog_data
            );

            if (res < 0) {
                goto err_free_so;
            }

            mod->text_blockid = 1;

            mod->phdr[i].p_vaddr += (Elf32_Addr)prog_data;
            mod->text_base = mod->phdr[i].p_vaddr;
            mod->text_size = mod->phdr[i].p_memsz;

            data_addr = (uintptr_t)prog_data + page_align(prog_size);
        } else {
            if (data_addr == 0) {
                printf("so_load: data segment before text segment?\n");
                res = -1;
                goto err_free_text;
            }

            prog_size = ALIGN_MEM(
                mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - (data_addr - mod->text_base),
                align
            );

            printf("Mapping DATA at 0x%08x size 0x%zx\n",
                   (unsigned int)data_addr,
                   prog_size);

            res = map_fixed_region(
                data_addr,
                prog_size,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                &prog_data
            );

            if (res < 0) {
                goto err_free_text;
            }

            mod->data_blockid = 2;

            mod->phdr[i].p_vaddr += (Elf32_Addr)mod->text_base;
            mod->data_base = mod->phdr[i].p_vaddr;
            mod->data_size = mod->phdr[i].p_memsz;
        }

        memset(prog_data, 0, prog_size);

        memcpy(
            (void *)mod->phdr[i].p_vaddr,
            (void *)((uintptr_t)so_data + mod->phdr[i].p_offset),
            mod->phdr[i].p_filesz
        );
    }

    for (int i = 0; i < mod->ehdr->e_shnum; i++) {
        char *sh_name = mod->shstr + mod->shdr[i].sh_name;
        uintptr_t sh_addr = mod->text_base + mod->shdr[i].sh_addr;
        size_t sh_size = mod->shdr[i].sh_size;

        if (strcmp(sh_name, ".dynamic") == 0) {
            mod->dynamic = (Elf32_Dyn *)sh_addr;
            mod->num_dynamic = sh_size / sizeof(Elf32_Dyn);
        } else if (strcmp(sh_name, ".dynstr") == 0) {
            mod->dynstr = (char *)sh_addr;
        } else if (strcmp(sh_name, ".dynsym") == 0) {
            mod->dynsym = (Elf32_Sym *)sh_addr;
            mod->num_dynsym = sh_size / sizeof(Elf32_Sym);
        } else if (strcmp(sh_name, ".rel.dyn") == 0) {
            mod->reldyn = (Elf32_Rel *)sh_addr;
            mod->num_reldyn = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".rel.plt") == 0) {
            mod->relplt = (Elf32_Rel *)sh_addr;
            mod->num_relplt = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".init_array") == 0) {
            mod->init_array = (void *)sh_addr;
            mod->num_init_array = sh_size / sizeof(void *);
        } else if (strcmp(sh_name, ".hash") == 0) {
            mod->hash = (void *)sh_addr;
        }
    }

    if (
        mod->dynamic == NULL ||
        mod->dynstr == NULL ||
        mod->dynsym == NULL ||
        mod->reldyn == NULL ||
        mod->relplt == NULL
    ) {
        printf("so_load: missing required ELF sections\n");
        printf("dynamic=%p dynstr=%p dynsym=%p reldyn=%p relplt=%p\n",
               mod->dynamic,
               mod->dynstr,
               mod->dynsym,
               mod->reldyn,
               mod->relplt);

        res = -2;
        goto err_free_data;
    }

    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_SONAME:
                mod->soname = mod->dynstr + mod->dynamic[i].d_un.d_ptr;
                break;

            default:
                break;
        }
    }

    free(so_data);

    if (!head && !tail) {
        head = mod;
        tail = mod;
    } else {
        tail->next = mod;
        tail = mod;
    }

    printf("so_load: success\n");
    printf("text_base=0x%08x text_size=0x%zx\n",
           (unsigned int)mod->text_base,
           mod->text_size);
    printf("data_base=0x%08x data_size=0x%zx\n",
           (unsigned int)mod->data_base,
           mod->data_size);

    return 0;

err_free_data:
    if (mod->data_base && mod->data_size) {
        munmap((void *)mod->data_base, page_align(mod->data_size));
    }

err_free_text:
    if (mod->text_base && mod->text_size) {
        munmap((void *)mod->text_base, page_align(mod->text_size));
    }

err_free_so:
    if (so_data) {
        free(so_data);
    }

    return res;
}

int so_relocate(so_module *mod) {
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn
            ? &mod->reldyn[i]
            : &mod->relplt[i - mod->num_reldyn];

        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);
        int type = ELF32_R_TYPE(rel->r_info);

        switch (type) {
            case R_ARM_ABS32:
                if (sym->st_shndx != SHN_UNDEF) {
                    *ptr += mod->text_base + sym->st_value;
                } else {
                    *ptr = mod->text_base + rel->r_offset;
                }
                break;

            case R_ARM_RELATIVE:
                *ptr += mod->text_base;
                break;

            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
                if (sym->st_shndx != SHN_UNDEF) {
                    *ptr = mod->text_base + sym->st_value;
                } else {
                    *ptr = mod->text_base + rel->r_offset;
                }
                break;

            default:
                fatal_error("Error: unknown relocation type %x\n", type);
                break;
        }
    }

    return 0;
}

uintptr_t so_resolve_link(so_module *mod, const char *symbol) {
    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_NEEDED: {
                so_module *curr = head;

                while (curr) {
                    if (
                        curr != mod &&
                        curr->soname &&
                        strcmp(curr->soname, mod->dynstr + mod->dynamic[i].d_un.d_ptr) == 0
                    ) {
                        uintptr_t link = so_symbol(curr, symbol);

                        if (link) {
                            return link;
                        }
                    }

                    curr = curr->next;
                }

                break;
            }

            default:
                break;
        }
    }

    return 0;
}

int so_resolve(
    so_module *mod,
    so_default_dynlib *default_dynlib,
    int size_default_dynlib,
    int default_dynlib_only
) {
    int missing_count = 0;

    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn
            ? &mod->reldyn[i]
            : &mod->relplt[i - mod->num_reldyn];

        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);
        int type = ELF32_R_TYPE(rel->r_info);

        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT: {
                if (sym->st_shndx == SHN_UNDEF) {
                    int resolved = 0;
                    const char *name = mod->dynstr + sym->st_name;

                    if (!default_dynlib_only) {
                        uintptr_t link = so_resolve_link(mod, name);

                        if (link) {
                            *ptr = link;
                            resolved = 1;
                        }
                    }

                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(name, default_dynlib[j].symbol) == 0) {
                            *ptr = default_dynlib[j].func;
                            resolved = 1;
                            break;
                        }
                    }

                    if (!resolved) {
                        printf("Missing symbol: %s\n", name);
                        missing_count++;
                    }
                }

                break;
            }

            default:
                break;
        }
    }

    if (missing_count) {
        printf("so_resolve: %d missing symbols\n", missing_count);
    } else {
        printf("so_resolve: all symbols resolved\n");
    }

    return 0;
}

void so_initialize(so_module *mod) {
    for (int i = 0; i < mod->num_init_array; i++) {
        if (mod->init_array[i]) {
            mod->init_array[i]();
        }
    }
}

uint32_t so_hash(const uint8_t *name) {
    uint64_t h = 0;
    uint64_t g;

    while (*name) {
        h = (h << 4) + *name++;

        if ((g = (h & 0xf0000000)) != 0) {
            h ^= g >> 24;
        }

        h &= 0x0fffffff;
    }

    return h;
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
    if (mod->hash) {
        uint32_t hash = so_hash((const uint8_t *)symbol);
        uint32_t nbucket = mod->hash[0];
        uint32_t *bucket = &mod->hash[2];
        uint32_t *chain = &bucket[nbucket];

        for (int i = bucket[hash % nbucket]; i; i = chain[i]) {
            if (mod->dynsym[i].st_shndx == SHN_UNDEF) {
                continue;
            }

            if (
                mod->dynsym[i].st_info != SHN_UNDEF &&
                strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0
            ) {
                return mod->text_base + mod->dynsym[i].st_value;
            }
        }
    }

    for (int i = 0; i < mod->num_dynsym; i++) {
        if (mod->dynsym[i].st_shndx == SHN_UNDEF) {
            continue;
        }

        if (
            mod->dynsym[i].st_info != SHN_UNDEF &&
            strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0
        ) {
            return mod->text_base + mod->dynsym[i].st_value;
        }
    }

    return 0;
}
