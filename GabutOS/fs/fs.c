#include "fs.h"
#include "ata.h"
#include "screen.h"

extern int strcmp(const char* s1, const char* s2);

#define FS_MAGIC 0x47415543u

#define FS_MAX_FILES 32
#define FS_MAX_FREE_EXTENTS 16
#define FS_SUPERBLOCK_LBA 0
#define FS_TABLE_START_LBA 1
#define FS_TABLE_SECTORS 2
#define FS_DATA_START_LBA (FS_TABLE_START_LBA + FS_TABLE_SECTORS)

struct fs_extent {
    uint32_t start_lba;
    uint32_t sector_count;
};

struct fs_superblock {
    uint32_t magic;
    uint32_t file_count;
    uint32_t next_free_lba;
    uint32_t free_extent_count;
    struct fs_extent free_extents[FS_MAX_FREE_EXTENTS];
    uint8_t reserved[500 - 4 - (FS_MAX_FREE_EXTENTS * 8)];
};

struct fs_entry {
    char name[20];
    uint32_t start_lba;
    uint32_t size_bytes;
    uint32_t used;
} __attribute__((packed));

static struct fs_superblock sb;
static struct fs_entry table[FS_MAX_FILES];

static void fs_write_table(void) {
    uint8_t* raw = (uint8_t*)table;
    for (int s = 0; s < FS_TABLE_SECTORS; s++) {
        ata_write_sector(FS_TABLE_START_LBA + s, raw + s * 512);
    }
}

static void fs_write_superblock(void) {
    ata_write_sector(FS_SUPERBLOCK_LBA, (uint8_t*)&sb);
}

static int find_entry(const char* name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (table[i].used && strcmp(table[i].name, name) == 0) return i;
    }
    return -1;
}

static void free_extents_add(uint32_t start_lba, uint32_t sector_count) {
    if (sector_count == 0) return;

    for (uint32_t i = 0; i < sb.free_extent_count; i++) {
        struct fs_extent* e = &sb.free_extents[i];
        if (e->start_lba + e->sector_count == start_lba) {
            e->sector_count += sector_count;
            return;
        }
        if (start_lba + sector_count == e->start_lba) {
            e->start_lba = start_lba;
            e->sector_count += sector_count;
            return;
        }
    }

    if (sb.free_extent_count < FS_MAX_FREE_EXTENTS) {
        sb.free_extents[sb.free_extent_count].start_lba = start_lba;
        sb.free_extents[sb.free_extent_count].sector_count = sector_count;
        sb.free_extent_count++;
    }
}

static int free_extents_take(uint32_t sectors_needed, uint32_t* out_start_lba) {
    for (uint32_t i = 0; i < sb.free_extent_count; i++) {
        struct fs_extent* e = &sb.free_extents[i];
        if (e->sector_count < sectors_needed) continue;

        *out_start_lba = e->start_lba;

        if (e->sector_count == sectors_needed) {
            for (uint32_t j = i; j < sb.free_extent_count - 1; j++) {
                sb.free_extents[j] = sb.free_extents[j + 1];
            }
            sb.free_extent_count--;
        } else {
            e->start_lba += sectors_needed;
            e->sector_count -= sectors_needed;
        }
        return 1;
    }
    return 0;
}

void fs_format(void) {
    sb.magic = FS_MAGIC;
    sb.file_count = 0;
    sb.next_free_lba = FS_DATA_START_LBA;
    sb.free_extent_count = 0;

    for (int i = 0; i < FS_MAX_FILES; i++) {
        table[i].used = 0;
        table[i].name[0] = '\0';
        table[i].start_lba = 0;
        table[i].size_bytes = 0;
    }

    fs_write_superblock();
    fs_write_table();
}

int fs_mount(void) {
    uint8_t buf[512];
    ata_read_sector(FS_SUPERBLOCK_LBA, buf);
    struct fs_superblock* loaded = (struct fs_superblock*)buf;

    if (loaded->magic != FS_MAGIC) return 0;
    sb = *loaded;

    uint8_t* raw = (uint8_t*)table;
    for (int s = 0; s < FS_TABLE_SECTORS; s++) {
        ata_read_sector(FS_TABLE_START_LBA + s, raw + s * 512);
    }
    return 1;
}

int fs_delete(const char* name) {
    int idx = find_entry(name);
    if (idx == -1) return 0;

    uint32_t sectors = (table[idx].size_bytes + 511) / 512;
    if (sectors == 0) sectors = 1;

    free_extents_add(table[idx].start_lba, sectors);

    table[idx].used = 0;
    table[idx].name[0] = '\0';
    table[idx].start_lba = 0;
    table[idx].size_bytes = 0;

    sb.file_count--;

    fs_write_table();
    fs_write_superblock();

    return 1;
}

int fs_write(const char* name, const uint8_t* data, uint32_t size) {
    int existing = find_entry(name);
    if (existing != -1) {
        fs_delete(name);
    }

    int slot = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!table[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return 0;

    uint32_t sectors_needed = (size + 511) / 512;
    if (sectors_needed == 0) sectors_needed = 1;

    uint32_t start;
    if (!free_extents_take(sectors_needed, &start)) {
        start = sb.next_free_lba;
        sb.next_free_lba += sectors_needed;
    }

    uint8_t sector_buf[512];
    for (uint32_t s = 0; s < sectors_needed; s++) {
        for (uint32_t i = 0; i < 512; i++) sector_buf[i] = 0;

        uint32_t offset = s * 512;
        uint32_t remaining = size - offset;
        uint32_t chunk = remaining > 512 ? 512 : remaining;

        for (uint32_t i = 0; i < chunk; i++) sector_buf[i] = data[offset + i];
        ata_write_sector(start + s, sector_buf);
    }

    int j = 0;
    for (; name[j] != '\0' && j < 19; j++) table[slot].name[j] = name[j];
    table[slot].name[j] = '\0';
    table[slot].start_lba = start;
    table[slot].size_bytes = size;
    table[slot].used = 1;

    sb.file_count++;

    fs_write_table();
    fs_write_superblock();

    return 1;
}

int fs_read(const char* name, uint8_t* out_buf, uint32_t max_size, uint32_t* out_size) {
    int idx = find_entry(name);
    if (idx == -1) return 0;

    uint32_t size = table[idx].size_bytes;
    uint32_t to_copy = size > max_size ? max_size : size;
    uint32_t sectors = (to_copy + 511) / 512;

    uint8_t sector_buf[512];
    for (uint32_t s = 0; s < sectors; s++) {
        ata_read_sector(table[idx].start_lba + s, sector_buf);

        uint32_t offset = s * 512;
        uint32_t remaining = to_copy - offset;
        uint32_t chunk = remaining > 512 ? 512 : remaining;

        for (uint32_t k = 0; k < chunk; k++) out_buf[offset + k] = sector_buf[k];
    }

    if (out_size) *out_size = size;
    return 1;
}

void fs_list(void) {
    int any = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (table[i].used) {
            any = 1;
            print_string("  ");
            print_string(table[i].name);
            print_string(" (");
            print_dec(table[i].size_bytes);
            print_string(" bytes)\n");
        }
    }
    if (!any) print_string("  (kosong)\n");
}
