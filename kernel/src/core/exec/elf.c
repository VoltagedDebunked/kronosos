#include <core/exec/elf.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <fs/ext2.h>
#include <lib/string.h>
#include <utils/log.h>

// Forward declarations
static bool elf_validate_header(elf64_ehdr_t *ehdr);
static bool elf_load_program_headers(elf_file_t *elf);
static bool elf_load_section_headers(elf_file_t *elf);
static bool elf_load_segments(elf_file_t *elf, uint64_t base_addr);
static bool elf_find_symbol_tables(elf_file_t *elf);
static char *elf_get_string(elf_file_t *elf, size_t string_table_offset, uint32_t offset);

static bool elf_validate_header(elf64_ehdr_t *ehdr) {
    // Check ELF magic number
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        LOG_ERROR("Invalid ELF magic number");
        return false;
    }
    
    // Check 64-bit format
    if (ehdr->e_ident[4] != ELFCLASS64) {
        LOG_ERROR("Not a 64-bit ELF file");
        return false;
    }
    
    // Check little endian
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        LOG_ERROR("Not a little-endian ELF file");
        return false;
    }
    
    // Check x86_64 architecture
    if (ehdr->e_machine != EM_X86_64) {
        LOG_ERROR("Not an x86_64 ELF file");
        return false;
    }
    
    // Check if executable or shared object
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        LOG_ERROR("Not an executable or shared object file");
        return false;
    }
    
    // Validate program headers
    if (ehdr->e_phentsize != sizeof(elf64_phdr_t)) {
        LOG_ERROR("Invalid program header size");
        return false;
    }
    
    // Validate section headers
    if (ehdr->e_shentsize != sizeof(elf64_shdr_t)) {
        LOG_ERROR("Invalid section header size");
        return false;
    }
    
    return true;
}

static bool elf_load_program_headers(elf_file_t *elf) {
    if (elf->header.e_phnum == 0) {
        // No program headers
        elf->program_headers = NULL;
        return true;
    }
    
    // Calculate offset to program headers
    uint64_t phoff = elf->header.e_phoff;
    if (phoff + (elf->header.e_phnum * sizeof(elf64_phdr_t)) > elf->size) {
        LOG_ERROR("Program headers outside file bounds");
        return false;
    }
    
    // Get program headers
    elf->program_headers = (elf64_phdr_t*)((uint8_t*)elf->data + phoff);
    
    LOG_DEBUG("Loaded %d program headers", elf->header.e_phnum);
    return true;
}

static bool elf_load_section_headers(elf_file_t *elf) {
    if (elf->header.e_shnum == 0) {
        // No section headers
        elf->section_headers = NULL;
        return true;
    }
    
    // Calculate offset to section headers
    uint64_t shoff = elf->header.e_shoff;
    if (shoff + (elf->header.e_shnum * sizeof(elf64_shdr_t)) > elf->size) {
        LOG_ERROR("Section headers outside file bounds");
        return false;
    }
    
    // Get section headers
    elf->section_headers = (elf64_shdr_t*)((uint8_t*)elf->data + shoff);
    
    // Process symbol tables and string tables
    elf_find_symbol_tables(elf);
    
    LOG_DEBUG("Loaded %d section headers", elf->header.e_shnum);
    return true;
}

static bool elf_find_symbol_tables(elf_file_t *elf) {
    if (!elf->section_headers || elf->header.e_shnum == 0) {
        return false;
    }
    
    // Find symbol table and string table
    for (uint16_t i = 0; i < elf->header.e_shnum; i++) {
        elf64_shdr_t *section = &elf->section_headers[i];
        
        if (section->sh_type == SHT_SYMTAB) {
            // This is a symbol table
            elf->symtab = (elf64_sym_t*)((uint8_t*)elf->data + section->sh_offset);
            elf->symtab_entries = section->sh_size / sizeof(elf64_sym_t);
            
            // Get the associated string table
            if (section->sh_link < elf->header.e_shnum) {
                elf64_shdr_t *strtab_section = &elf->section_headers[section->sh_link];
                if (strtab_section->sh_type == SHT_STRTAB) {
                    elf->strtab = (char*)((uint8_t*)elf->data + strtab_section->sh_offset);
                    elf->strtab_size = strtab_section->sh_size;
                }
            }
        }
    }
    
    return (elf->symtab != NULL && elf->strtab != NULL);
}

static char *elf_get_string(elf_file_t *elf, size_t string_table_offset, uint32_t offset) {
    if (string_table_offset + offset >= elf->size) {
        return NULL;
    }
    
    return (char*)elf->data + string_table_offset + offset;
}

char *elf_get_section_name(elf_file_t *elf, elf64_shdr_t *section) {
    if (!elf || !section || elf->header.e_shstrndx >= elf->header.e_shnum) {
        return NULL;
    }
    
    // Get the section header string table
    elf64_shdr_t *shstrtab = &elf->section_headers[elf->header.e_shstrndx];
    
    // Get the section name
    return elf_get_string(elf, shstrtab->sh_offset, section->sh_name);
}

bool elf_parse_memory(void *data, size_t size, elf_file_t *elf) {
    if (!data || !elf || size < sizeof(elf64_ehdr_t)) {
        LOG_ERROR_MSG("Invalid ELF data or buffer too small");
        return false;
    }
    
    // Initialize ELF file structure
    memset(elf, 0, sizeof(elf_file_t));
    elf->data = data;
    elf->size = size;
    
    // Parse ELF header
    elf64_ehdr_t *ehdr = (elf64_ehdr_t*)data;
    
    // Validate ELF header
    if (!elf_validate_header(ehdr)) {
        LOG_ERROR_MSG("Invalid ELF header");
        return false;
    }
    
    // Store ELF header
    memcpy(&elf->header, ehdr, sizeof(elf64_ehdr_t));
    
    // Load program headers and section headers
    if (!elf_load_program_headers(elf) || !elf_load_section_headers(elf)) {
        return false;
    }
    
    LOG_INFO("Successfully parsed ELF file from memory: entry=0x%llX", elf->header.e_entry);
    return true;
}

bool elf_parse_file(const char *filename, elf_file_t *elf) {
    if (!filename || !elf) {
        LOG_ERROR_MSG("Invalid filename or ELF structure");
        return false;
    }
    
    // Open the file
    int fd = ext2_open(filename, EXT2_O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open ELF file: %s", filename);
        return false;
    }
    
    // Read the ELF header first to validate and get file info
    elf64_ehdr_t header;
    ssize_t header_bytes = ext2_read(fd, &header, sizeof(elf64_ehdr_t));
    
    if (header_bytes != sizeof(elf64_ehdr_t)) {
        LOG_ERROR("Failed to read ELF header");
        ext2_close(fd);
        return false;
    }
    
    // Validate the header
    if (!elf_validate_header(&header)) {
        LOG_ERROR_MSG("Invalid ELF header");
        ext2_close(fd);
        return false;
    }
    
    // Reopen the file to reset position
    ext2_close(fd);
    fd = ext2_open(filename, EXT2_O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to reopen ELF file: %s", filename);
        return false;
    }
    
    // Allocate an initial buffer - we'll start with a reasonable size and expand if needed
    size_t initial_size = 64 * 1024; // 64 KB initial buffer
    void *file_data = pmm_alloc_pages((initial_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
    if (!file_data) {
        LOG_ERROR("Failed to allocate memory for ELF file");
        ext2_close(fd);
        return false;
    }
    
    // Read file in chunks
    size_t total_read = 0;
    size_t buffer_size = initial_size;
    ssize_t bytes_read;
    
    while ((bytes_read = ext2_read(fd, (uint8_t*)file_data + total_read, 
                                  buffer_size - total_read)) > 0) {
        total_read += bytes_read;
        
        // Check if we need to expand the buffer
        if (total_read >= buffer_size) {
            // Expand buffer by 64 KB
            size_t new_size = buffer_size + 64 * 1024;
            void *new_data = pmm_alloc_pages((new_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
            
            if (!new_data) {
                LOG_ERROR("Failed to expand buffer for ELF file");
                pmm_free_pages(file_data, (buffer_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
                ext2_close(fd);
                return false;
            }
            
            // Copy existing data to new buffer
            memcpy(new_data, file_data, total_read);
            
            // Free old buffer
            pmm_free_pages(file_data, (buffer_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
            
            // Update pointers and sizes
            file_data = new_data;
            buffer_size = new_size;
        }
    }
    
    // Close the file
    ext2_close(fd);
    
    // Parse the file from memory
    bool result = elf_parse_memory(file_data, total_read, elf);
    
    // If parsing failed, free the allocated memory
    if (!result) {
        pmm_free_pages(file_data, (total_read + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
    }
    
    return result;
}

bool elf_load(elf_file_t *elf, uint64_t base_addr) {
    if (!elf || !elf->program_headers) {
        LOG_ERROR_MSG("Invalid ELF file or no program headers");
        return false;
    }
    
    // Store base address
    elf->base_addr = base_addr;
    
    // Load program segments
    if (!elf_load_segments(elf, base_addr)) {
        return false;
    }
    
    // Calculate entry point
    elf->entry_point = (void*)(elf->header.e_entry + 
                              (elf->header.e_type == ET_DYN ? base_addr : 0));
    
    LOG_INFO("Loaded ELF file at base=0x%llX, entry=0x%llX", 
             base_addr, (uint64_t)elf->entry_point);
             
    return true;
}

static bool elf_load_segments(elf_file_t *elf, uint64_t base_addr) {
    elf->top_addr = 0;
    
    // Process all program headers
    for (uint16_t i = 0; i < elf->header.e_phnum; i++) {
        elf64_phdr_t *phdr = &elf->program_headers[i];
        
        // Only load PT_LOAD segments
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        
        // Calculate virtual address
        uint64_t vaddr = phdr->p_vaddr;
        if (elf->header.e_type == ET_DYN) {
            vaddr += base_addr;
        }
        
        // Calculate size in pages (rounded up)
        size_t page_size = PAGE_SIZE_4K;
        size_t segment_size = phdr->p_memsz;
        size_t pages = (segment_size + page_size - 1) / page_size;
        
        // Allocate memory
        void *segment_memory = pmm_alloc_pages(pages);
        if (!segment_memory) {
            LOG_ERROR("Failed to allocate memory for segment");
            return false;
        }
        
        // Clear memory
        memset(segment_memory, 0, pages * page_size);
        
        // Copy segment data
        if (phdr->p_filesz > 0) {
            // Check bounds
            if (phdr->p_offset + phdr->p_filesz > elf->size) {
                LOG_ERROR("Segment data outside file bounds");
                pmm_free_pages(segment_memory, pages);
                return false;
            }
            
            // Copy data
            void *segment_data = (uint8_t*)elf->data + phdr->p_offset;
            memcpy(segment_memory, segment_data, phdr->p_filesz);
        }
        
        // Map segment to virtual memory
        uint64_t flags = VMM_FLAG_PRESENT;
        if (phdr->p_flags & PF_W) flags |= VMM_FLAG_WRITABLE;
        if (!(phdr->p_flags & PF_X)) flags |= VMM_FLAG_NO_EXECUTE;
        
        // Align virtual address to page boundary
        uint64_t page_vaddr = vaddr & ~(page_size - 1);
        
        // Map pages
        for (size_t j = 0; j < pages; j++) {
            uint64_t page_phys = (uint64_t)segment_memory + j * page_size;
            uint64_t page_virt = page_vaddr + j * page_size;
            
            if (!vmm_map_page(page_virt, page_phys, flags)) {
                LOG_ERROR("Failed to map segment page to virtual memory");
                return false;
            }
        }
        
        // Update top address
        uint64_t segment_end = vaddr + segment_size;
        if (segment_end > elf->top_addr) {
            elf->top_addr = segment_end;
        }
        
        LOG_DEBUG("Loaded segment %d: vaddr=0x%llX, size=%zu, flags=0x%llX", 
                 i, vaddr, segment_size, flags);
    }
    
    return true;
}

bool elf_unload(elf_file_t *elf) {
    if (!elf || elf->base_addr == 0) {
        return false;
    }
    
    // Process all program headers
    for (uint16_t i = 0; i < elf->header.e_phnum; i++) {
        elf64_phdr_t *phdr = &elf->program_headers[i];
        
        // Only unload PT_LOAD segments
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        
        // Calculate virtual address
        uint64_t vaddr = phdr->p_vaddr;
        if (elf->header.e_type == ET_DYN) {
            vaddr += elf->base_addr;
        }
        
        // Calculate size in pages (rounded up)
        size_t page_size = PAGE_SIZE_4K;
        size_t segment_size = phdr->p_memsz;
        size_t pages = (segment_size + page_size - 1) / page_size;
        
        // Align virtual address to page boundary
        uint64_t page_vaddr = vaddr & ~(page_size - 1);
        
        // Unmap pages and free physical memory
        for (size_t j = 0; j < pages; j++) {
            uint64_t page_virt = page_vaddr + j * page_size;
            uint64_t page_phys = vmm_get_physical_address(page_virt);
            
            if (page_phys) {
                // Unmap page
                vmm_unmap_page(page_virt);
                
                // Free physical memory (only once per contiguous block)
                if (j == 0 || vmm_get_physical_address(page_virt - page_size) != page_phys - page_size) {
                    pmm_free_page((void*)page_phys);
                }
            }
        }
    }
    
    // Reset base address
    elf->base_addr = 0;
    elf->top_addr = 0;
    
    return true;
}

void elf_free(elf_file_t *elf) {
    if (!elf) {
        return;
    }
    
    // Unload from memory if loaded
    if (elf->base_addr != 0) {
        elf_unload(elf);
    }
    
    // Free file data
    if (elf->data) {
        pmm_free_pages(elf->data, (elf->size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
        elf->data = NULL;
    }
    
    // Reset structure
    memset(elf, 0, sizeof(elf_file_t));
}

void *elf_get_symbol_address(elf_file_t *elf, const char *symbol_name) {
    if (!elf || !symbol_name || !elf->symtab || !elf->strtab) {
        return NULL;
    }
    
    // Search for the symbol
    for (uint32_t i = 0; i < elf->symtab_entries; i++) {
        elf64_sym_t *sym = &elf->symtab[i];
        
        // Get symbol name
        char *name = elf_get_string(elf, (uint8_t*)elf->strtab - (uint8_t*)elf->data, sym->st_name);
        if (!name || strcmp(name, symbol_name) != 0) {
            continue;
        }
        
        // Calculate symbol address
        uint64_t addr = sym->st_value;
        if (elf->header.e_type == ET_DYN) {
            addr += elf->base_addr;
        }
        
        return (void*)addr;
    }
    
    return NULL;
}