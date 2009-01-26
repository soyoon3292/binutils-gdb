# name: attributes for -march=xscale
# source: blank.s
# as: -march=xscale
# readelf: -A
# This test is only valid on ELF based ports.
# not-target: *-*-*coff *-*-pe *-*-wince *-*-*aout* *-*-netbsd *-*-riscix*

Attribute Section: aeabi
File Attributes
  Tag_CPU_name: "xscale"
  Tag_CPU_arch: v5TE
  Tag_ARM_ISA_use: Yes
  Tag_THUMB_ISA_use: Thumb-1
