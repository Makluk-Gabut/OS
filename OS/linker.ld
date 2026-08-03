/* Menentukan entry point utama sistem operasi */
ENTRY(_start)

SECTIONS
{
	/* GRUB biasa memuat kernel mulai dari alamat memori 1 Megabyte */
	. = 1M;

	kernel_start = .;

	/* Letakkan Multiboot header terlebih dahulu, baru kode eksekusi (.text) */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	}

	/* Sektor untuk data konstanta (Read-Only Data) */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	/* Sektor untuk data variabel yang memiliki nilai awal */
	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	/* Sektor untuk stack dan variabel kosong (Block Started by Symbol) */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}

	kernel_end = .;
}
