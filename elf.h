
#ifndef _ELF_H_
#define _ELF_H_


#define EI_MAG0     0
#define   ELFMAG0      0x7f
#define EI_MAG1     1
#define   ELFMAG1      'E'
#define EI_MAG2     2
#define   ELFMAG2      'L'
#define EI_MAG3     3
#define   ELFMAG3      'F'
#define EI_CLASS    4
#define   ELFCLASSNONE  0   // Invalid class
#define   ELFCLASS32    1   // 32bit object
#define   ELFCLASS64    2   // 64bit object
#define EI_DATA     5
#define   ELFDATANONE    0   // Invalid data encoding
#define   ELFDATA2LSB    1   // low byte first
#define   ELFDATA2MSB    2   // high byte first
#define EI_VERSION  6        // file version
#define EI_OSABI    7           // Operating System/ABI indication
#define   ELFOSABI_NONE       0 // UNIX System V ABI
#define   ELFOSABI_HPUX       1 // HP-UX operating system
#define   ELFOSABI_NETBSD     2 // NetBSD
#define   ELFOSABI_LINUX      3 // GNU/Linux
#define   ELFOSABI_HURD       4 // GNU/Hurd
#define   ELFOSABI_SOLARIS    6 // Solaris
#define   ELFOSABI_AIX        7 // AIX
#define   ELFOSABI_IRIX       8 // IRIX
#define   ELFOSABI_FREEBSD    9 // FreeBSD
#define   ELFOSABI_TRU64     10 // TRU64 UNIX
#define   ELFOSABI_MODESTO   11 // Novell Modesto
#define   ELFOSABI_OPENBSD   12 // OpenBSD
#define   ELFOSABI_OPENVMS   13 // OpenVMS
#define   ELFOSABI_NSK       14 // Hewlett-Packard Non-Stop Kernel
#define   ELFOSABI_AROS      15 // Amiga Research OS
#define   ELFOSABI_ARM       97 // ARM
#define   ELFOSABI_STANDALONE 255 // Standalone (embedded) application
#define EI_ABIVERSION   8       // ABI version
#define EI_PAD          9       // Start of padding bytes
#define EI_NIDENT 16            // sizeof


typedef struct {
	u8  ident[EI_NIDENT];   // see above
	u16 type;               // enum ET
	u16 machine;            // enum EM
	u32 version;            // enum EV
	u32 entry;              // virtual start address
	u32 phoff;              // off to program header table's (pht)
	u32 shoff;              // off to section header table's (sht)
	u32 flags;              // EF_machine_flag
	u16 ehsize;             // header's size
	u16 phentsize;          // size of pht element
	u16 phnum;              // entry counter in pht
	u16 shentsize;          // size of sht element
	u16 shnum;              // entry count in sht
	u16 shstrndx;           // sht index in name table
}Elf32_Ehdr;

enum elf_ET {
	ET_NONE = 0,    // No file type
	ET_REL  = 1,    // Relocatable file
	ET_EXEC = 2,    // Executable file
	ET_DYN  = 3,    // Share object file
	ET_CORE = 4,    // Core file
};

enum elf_EM {
	EM_NONE  = 0,   // No machine
	EM_386   = 3,   // Intel 80386
	EM_486   = 6,
	EM_MIPS  = 8,   // MIPS
	EM_PPC   = 20,  // PowerPC
	EM_ARM   = 40,  // ARM
};

typedef struct {
	u32 name;      // index in string table
	u32 type;      // enum SHT
	u32 flags;     // enum SHF
	u32 addr;      // address in memmory (or 0)
	u32 offset;    // offset in file
	u32 size;      // section size in bytes
	u32 link;      // index in symbol table
	u32 info;      // extra information
	u32 addralign; // 0 & 1 => no alignment
	u32 entsize;   // size symbol table or eq.
} Elf32_Shdr;


#define PF_R 4
#define PF_W 2
#define PF_X 1

typedef struct {
	u32 type;      // Segment type. see below
	u32 offset;    // from beginning of file at 1 byte of segment resides
	u32 vaddr;     // virtual addr of 1 byte
	u32 paddr;     // reserved for system
	u32 filesz;    // may be 0
	u32 memsz;     // my be 0
	u32 flags;     //  for PT_LOAD access mask (PF_
	u32 align;     // 0/1-no,
}Elf32_Phdr;

enum elf_SEGTYPE {
	PT_NULL    = 0,               //ignore entries in program table
	PT_LOAD    = 1,               //loadable segmen described in _filesz & _memsz
	PT_DYNAMIC = 2,               //dynamic linking information
	PT_INTERP  = 3,               //path name to interpreter (loadable)
	PT_NOTE    = 4,               //auxilarry information
	PT_SHLIB   = 5,               //reserved. Has no specified semantics
	PT_PHDR    = 6,               //location & size program header table
	PT_TLS     = 7,               //Thread local storage segment
};


#endif


