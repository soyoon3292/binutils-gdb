# name: attributes for -march=armv6z
# source: blank.s
# as: -march=armv6z
# readelf: -A
# This test is only valid on ELF based ports.
# not-target: *-*-*coff *-*-pe *-*-wince *-*-*aout* *-*-netbsd *-*-riscix*

Attribute Section: aeabi
File Attributes
  Tag_CPU_name: "6Z"
  Tag_CPU_arch: v6KZ
  Tag_ARM_ISA_use: Yes
  Tag_THUMB_ISA_use: Thumb-1
