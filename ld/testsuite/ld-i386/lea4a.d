#source: lea4.s
#as: --32
#ld: -Bsymbolic -shared -melf_i386
#error: direct GOT relocation R_386_LOAD_GOT32 against `foo' without base register can not be used when making a shared object
