#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ELF file magic number
#define ELF_MAGIC 0x464C457F // "\x7FELF" in little endian

// ELF file classes
#define ELFCLASS32  1 // 32-bit objects
#define ELFCLASS64  2 // 64-bit objects

// ELF data encoding
#define ELFDATA2LSB 1 // Little endian
#define ELFDATA2MSB 2 // Big endian

// ELF file types
#define ET_NONE     0 // No file type
#define ET_REL      1 // Relocatable file
#define ET_EXEC     2 // Executable file
#define ET_DYN      3 // Shared object file
#define ET_CORE     4 // Core file

// ELF machine architectures
#define EM_NONE     0  // No machine
#define EM_386      3  // Intel 80386
#define EM_X86_64   62 // AMD x86-64

// Program header types
#define PT_NULL     0 // Unused entry
#define PT_LOAD     1 // Loadable segment
#define PT_DYNAMIC  2 // Dynamic linking information
#define PT_INTERP   3 // Interpreter pathname
#define PT_NOTE     4 // Auxiliary information
#define PT_SHLIB    5 // Reserved
#define PT_PHDR     6 // Program header table
#define PT_TLS      7 // Thread-local storage
#define PT_LOOS     0x60000000 // OS-specific
#define PT_HIOS     0x6FFFFFFF // OS-specific
#define PT_LOPROC   0x70000000 // Processor-specific
#define PT_HIPROC   0x7FFFFFFF // Processor-specific

// Program header flags
#define PF_X        0x1 // Executable
#define PF_W        0x2 // Writable
#define PF_R        0x4 // Readable

// Section header types
#define SHT_NULL        0  // Inactive section
#define SHT_PROGBITS    1  // Program defined information
#define SHT_SYMTAB      2  // Symbol table
#define SHT_STRTAB      3  // String table
#define SHT_RELA        4  // Relocation entries with addends
#define SHT_HASH        5  // Symbol hash table
#define SHT_DYNAMIC     6  // Dynamic linking information
#define SHT_NOTE        7  // Notes
#define SHT_NOBITS      8  // Occupies no space in file
#define SHT_REL         9  // Relocation entries without addends
#define SHT_SHLIB       10 // Reserved
#define SHT_DYNSYM      11 // Dynamic linker symbol table

// Section header flags
#define SHF_WRITE       0x1 // Writable
#define SHF_ALLOC       0x2 // Occupies memory during execution
#define SHF_EXECINSTR   0x4 // Executable

// ELF64 header structure
typedef struct {
    uint8_t  e_ident[16]; // Magic number and other info
    uint16_t e_type;      // Object file type
    uint16_t e_machine;   // Architecture
    uint32_t e_version;   // Object file version
    uint64_t e_entry;     // Entry point virtual address
    uint64_t e_phoff;     // Program header table file offset
    uint64_t e_shoff;     // Section header table file offset
    uint32_t e_flags;     // Processor-specific flags
    uint16_t e_ehsize;    // ELF header size in bytes
    uint16_t e_phentsize; // Program header table entry size
    uint16_t e_phnum;     // Program header table entry count
    uint16_t e_shentsize; // Section header table entry size
    uint16_t e_shnum;     // Section header table entry count
    uint16_t e_shstrndx;  // Section header string table index
} elf64_ehdr_t;

// ELF64 program header structure
typedef struct {
    uint32_t p_type;    // Segment type
    uint32_t p_flags;   // Segment flags
    uint64_t p_offset;  // Segment file offset
    uint64_t p_vaddr;   // Segment virtual address
    uint64_t p_paddr;   // Segment physical address
    uint64_t p_filesz;  // Segment size in file
    uint64_t p_memsz;   // Segment size in memory
    uint64_t p_align;   // Segment alignment
} elf64_phdr_t;

// ELF64 section header structure
typedef struct {
    uint32_t sh_name;      // Section name (string table index)
    uint32_t sh_type;      // Section type
    uint64_t sh_flags;     // Section flags
    uint64_t sh_addr;      // Section virtual address
    uint64_t sh_offset;    // Section file offset
    uint64_t sh_size;      // Section size in bytes
    uint32_t sh_link;      // Link to another section
    uint32_t sh_info;      // Additional section information
    uint64_t sh_addralign; // Section alignment
    uint64_t sh_entsize;   // Entry size if section holds table
} elf64_shdr_t;

// ELF64 symbol table entry
typedef struct {
    uint32_t st_name;  // Symbol name (string table index)
    uint8_t  st_info;  // Symbol type and binding
    uint8_t  st_other; // Symbol visibility
    uint16_t st_shndx; // Section index
    uint64_t st_value; // Symbol value
    uint64_t st_size;  // Symbol size
} elf64_sym_t;

// ELF file structure
typedef struct {
    void *data;                   // Pointer to file data
    size_t size;                  // Size of file data
    elf64_ehdr_t header;          // ELF header
    elf64_phdr_t *program_headers; // Program headers
    elf64_shdr_t *section_headers; // Section headers
    elf64_sym_t *symtab;          // Symbol table
    uint32_t symtab_entries;      // Number of symbol table entries
    char *strtab;                 // String table
    size_t strtab_size;           // Size of string table
    void *entry_point;            // Entry point
    uint64_t base_addr;           // Base address
    uint64_t top_addr;            // Top address (highest address used)
} elf_file_t;

// ELF file functions
bool elf_parse_memory(void *data, size_t size, elf_file_t *elf);
bool elf_parse_file(const char *filename, elf_file_t *elf);
bool elf_load(elf_file_t *elf, uint64_t base_addr);
bool elf_unload(elf_file_t *elf);
void elf_free(elf_file_t *elf);
void *elf_get_symbol_address(elf_file_t *elf, const char *symbol_name);
char *elf_get_section_name(elf_file_t *elf, elf64_shdr_t *section);

#endif // ELF_H