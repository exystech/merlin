/******************************************************************************

	COPYRIGHT(C) JONAS 'SORTIE' TERMANSEN 2011.

	This file is part of Sortix.

	Sortix is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	Sortix is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY# without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
	details.

	You should have received a copy of the GNU General Public License along
	with Sortix. If not, see <http://www.gnu.org/licenses/>.

	boot.s
	Bootstraps the kernel and passes over control from the boot-loader to the
	kernel main function. It also jumps into long mode!

******************************************************************************/


.globl start, _start

.section .text

.text  0x100000
.type _start, @function
.code32
start:
_start:
	jmp multiboot_entry

	# Align 32 bits boundary.
	.align	4

	# Multiboot header.
multiboot_header:
	# Magic.
	.long	0x1BADB002
	# Flags.
	.long	0x00000003
	# Checksum.
	.long	-(0x1BADB002 + 0x00000003)

multiboot_entry:
	
	# We got our multiboot information in various registers. But we are going
	# to need these registers. But where can we store them then? Oh hey, let's
	# store then in the code already run!

	# Store the pointer to the Multiboot information structure.
	mov %ebx, 0x100000

	# Store the magic value.
	mov %eax, 0x100004

	movl $0x1000, %edi
	mov %edi, %cr3
	xorl %eax, %eax
	movl $4096, %ecx
	rep stosl
	movl %cr3, %edi

	movl $0x2003, (%edi)
	addl $0x1000, %edi

	movl $0x3003, (%edi)
	addl $0x1000, %edi

	movl $0x4003, (%edi)
	addl $0x1000, %edi

	movl $0x3, %ebx
	movl $512, %ecx

SetEntry:
	mov %ebx, (%edi)
	add $0x1000, %ebx
	add $8, %edi
	loop SetEntry

	mov %cr4, %eax
	orl $0x20, %eax
	mov %eax, %cr4

	mov $0xC0000080, %ecx
	rdmsr
	orl $0x100, %eax
	wrmsr

	mov %cr0, %eax
	orl $0x80000000, %eax
	mov %eax, %cr0

	mov GDTPointer, %eax
	lgdtl GDTPointer

	ljmp $0x10, $Realm64

.code64
Realm64:

	cli
	mov $0x18, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs



	# Disable virtual memory
	#movq %cr0, %rdi
	#movabs $0xffffffff7fffffff, %rbx
	#andq %rbx, %rdi
	#movq %rdi, %cr0

	jmp Main

.section .data
GDT64:                           # Global Descriptor Table (64-bit).
	GDTNull:                       # The null descriptor.
	.word 0                         # Limit (low).
	.word 0                         # Base (low).
	.byte 0                         # Base (middle)
	.byte 0                         # Access.
	.byte 0                         # Granularity.
	.byte 0                         # Base (high).
	GDTUnused:                       # The null descriptor.
	.word 0                         # Limit (low).
	.word 0                         # Base (low).
	.byte 0                         # Base (middle)
	.byte 0                         # Access.
	.byte 0                         # Granularity.
	.byte 0                         # Base (high).
	GDTCode:                       # The code descriptor.
	.word 0xFFFF                    # Limit (low).
	.word 0                         # Base (low).
	.byte 0                         # Base (middle)
	.byte 0x9A                      # Access.
	.byte 0xAF                      # Granularity.
	.byte 0                         # Base (high).
	GDTData:                       # The data descriptor.
	.word 0xFFFF                    # Limit (low).
	.word 0                         # Base (low).
	.byte 0                         # Base (middle)
	.byte 0x92                      # Access.
	.byte 0x8F                      # Granularity.
	.byte 0                         # Base (high).
	GDTPointer:                    # The GDT-pointer.
	.word GDTPointer - GDT64 - 1     # Limit.
	.long GDT64                      # Base.
	.long 0
	
Main:
	# Copy the character B onto the screen so we know it works.
	movq $0x242, %r15
	movq %r15, %rax
	movw %ax, 0xB8000


	# Load the pointer to the Multiboot information structure.
	mov 0x100000, %ebx

	# Load the magic value.
	mov 0x100004, %eax

	# The linker is kindly asked to put the real 64-bit kernel at 0x110000.
	jmp 0x110000

