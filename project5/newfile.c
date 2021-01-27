// test
#include "fs.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#define FREE -1
#define END_FILE -2

/* Keep some global state */
super_block fs;
file_descriptor file_des[MAX_FILDES];
int *FAT;
dir_entry *DIR;
int fd_init = 0;
int file_num = 1;

const file_descriptor fd_empty = {.used = 0, .file = -1, .offset = -1};
const dir_entry empty_dir_entry = {.used = 0, .name = "\0", .size = 0, .head = -1, .ref_cnt = 0}; // {used, name, size, head, ref_cnt}

int make_fs(char *disk_name) {
    int disk_health;
    disk_health = make_disk(disk_name);
    if (disk_health == -1) return -1;
    disk_health = open_disk(disk_name);
    if (disk_health == -1) return -1;

    super_block init_fs;

    init_fs.fat_idx = 1;
    init_fs.fat_len = (sizeof(int) * DISK_BLOCKS)/BLOCK_SIZE;
    init_fs.dir_idx = init_fs.fat_idx + init_fs.fat_len; 
    init_fs.dir_len = (sizeof(dir_entry) * MAX_FILES)/BLOCK_SIZE + 1;
    init_fs.data_idx = init_fs.dir_idx + init_fs.dir_len; 
    
    char *buf = malloc(BLOCK_SIZE);

    int unused = FREE;
    int used = END_FILE;
    int i;
    for (i = 0; i < init_fs.fat_len; i++) {
        int j;
        for (j = 0; j < BLOCK_SIZE / sizeof(int); j++) {
            memcpy((void*)(buf + j * sizeof(int)), (void*) ((j < init_fs.data_idx) ? &used : &unused), sizeof(int));
        }
        block_write(init_fs.fat_idx + i, buf);
    }

    memcpy((void*)buf,(void*) &init_fs, sizeof(super_block));
    block_write(0, buf);
    for (i = 0; i < MAX_FILES; i++) {
        memcpy((void*)(buf + i * sizeof(dir_entry)),(void*)&empty_dir_entry, sizeof(dir_entry));
    }
    block_write(init_fs.dir_idx, buf);

    free(buf);
    disk_health = close_disk();
    if (disk_health == -1) return -1;

    return 0;
}


int mount_fs(char *disk_name) {
    int disk_health;
    disk_health = open_disk(disk_name);
    if (disk_health == -1) return disk_health;

    char *buf = malloc(BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void*)&fs, (void*)buf, sizeof(super_block));

    block_read(fs.dir_idx, buf);
    DIR = malloc(fs.dir_len * BLOCK_SIZE);
    memcpy((void*)DIR, (void*)buf, BLOCK_SIZE);

    FAT = malloc(fs.fat_len * BLOCK_SIZE);
    int i;
    for (i = 0; i < fs.fat_len; i++) {
        block_read(fs.fat_idx + i, buf);
        memcpy((void*)FAT + i * BLOCK_SIZE, (void*)buf, BLOCK_SIZE);
    }

    free(buf);
    return 0;
}


int umount_fs(char *disk_name) {
    int disk_health;
    char *buf = malloc(BLOCK_SIZE);
    memcpy((void*)buf,(void*) &fs, sizeof(super_block));
    disk_health = block_write(0, buf);
    if (disk_health == -1) return disk_health;

    memcpy((void*)buf, (void*)DIR, BLOCK_SIZE);
    block_write(fs.dir_idx, buf);

    int i;
    for (i = 0; i < fs.fat_len; i++) {
        memcpy((void*)buf,((void*) FAT) + i * BLOCK_SIZE, BLOCK_SIZE);
        block_write(fs.fat_idx + i, buf);
    }
    return close_disk();
}

int init_file_des(){
    int i;
    if (!fd_init) {
        fd_init = 1;
        for (i = 0; i < MAX_FILDES; i++) {
            file_des[i] = fd_empty;
        }
    }
    return 0;
}

int find_free_FAT_block(){
    int i;
    for (i = 0; i < (fs.fat_len * BLOCK_SIZE)/sizeof(int); i++) {
        if (*(FAT + i) == FREE) {
            return i;
        }
    }
    return -1;
}

int find_DIR_name(char* name, int* file_idx){
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (strcmp((DIR + i) -> name, name) == 0) {
            *file_idx = i;
            return 1;
        }
    }
    return 0;
}

int find_DIR_file(int file){
    int i;
    for(i = 0; i < MAX_FILES; i++){
        if((DIR + i) -> head == file) return i;
    }
    return -1;
}

int fs_open(char *name) {
    if(!fd_init) init_file_des();
    
    int file_idx = -1;
    if (!find_DIR_name(name, &file_idx)) return -1;

    int i;
    int free_fildes_found = 0;
    int fildes_idx = -1;
    for (i = 0; i < MAX_FILDES; i++) {
        if (file_des[i].used == 0) {
            free_fildes_found = 1;
            fildes_idx = i;
            break;
        }
    }

    if (!free_fildes_found) {
        return -1;
    }
    
    file_des[fildes_idx].used = 1;
    file_des[fildes_idx].file = (DIR + file_idx) -> head;
    file_des[fildes_idx].offset = 0;
    ((DIR + file_idx) -> ref_cnt)++;

    return fildes_idx;    
}

int fs_close(int fildes) {
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used == 0) return -1;
    
    file_des[fildes] = fd_empty;

    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if ((DIR + i) -> head == file_des[fildes].file) {
            ((DIR + i) -> ref_cnt)--;
        }
    }
    
    return 0;
}


int fs_create(char *name) {
    // check valid name //
    if (*name == '\0' || strlen(name) > 15) return -1;

    // find free slot in DIR //
    int i;
    int is_free = 0;
    int free_idx = -1;
    for (i = 0; i < MAX_FILES; i++) {
        if (is_free == 0 && (DIR + i) -> used == 0) {
            is_free = 1;
            free_idx = i;
        }
        // check if name already used //
        if (strcmp((DIR + i) -> name, name) == 0) return -1;
    }

    if (!is_free) return -1;

    (DIR + free_idx) -> used = 1;
    strcpy((DIR + free_idx) -> name, name);
    (DIR + free_idx) -> head = find_free_FAT_block();
    *(FAT + (DIR + free_idx) -> head) = END_FILE;

    return 0;
}


int fs_delete(char *name) {
    int file_idx = -1;
    if (!find_DIR_name(name, &file_idx)) return -1;
    if ((DIR + file_idx) -> ref_cnt > 0) return -1;

    int blockNum = (DIR + file_idx) -> head;
    char *buf = malloc(BLOCK_SIZE);
    while (*(FAT + blockNum) != END_FILE) {
        int temp = *(FAT + blockNum);
        block_write(fs.fat_idx + blockNum, buf);
        *(FAT + blockNum) = FREE;
        blockNum = temp;
    }
    *(FAT + blockNum) = FREE;

    (DIR + file_idx) -> used = 0;
    strcpy((DIR + file_idx) -> name, "");
    (DIR + file_idx) -> size = 0;
    (DIR + file_idx) -> head = -1;
    
    return 0;
}

int fs_read(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used != 1) return -1;

    dir_entry *check_DIR = DIR + find_DIR_file(file_des[fildes].file);
    file_des[fildes].file = check_DIR -> head;

    if (file_des[fildes].file == -1) return 0;

    int nbytes = 0;
    int current_fat_idx = file_des[fildes].file;
    while (nbytes + BLOCK_SIZE < file_des[fildes].offset && *(FAT + current_fat_idx) != END_FILE) {
        current_fat_idx = *(FAT + current_fat_idx);
        nbytes += BLOCK_SIZE;
    }

    char *r_buf = malloc(BLOCK_SIZE);
    size_t r_bytes = 0;

    while (r_bytes < nbyte) {
        if (file_des[fildes].offset % BLOCK_SIZE == 0 && file_des[fildes].offset != 0) {
            if (*(FAT + current_fat_idx) == END_FILE) {
                break;
            }
            current_fat_idx = *(FAT + current_fat_idx);
        }
        block_read(fs.fat_idx + current_fat_idx, r_buf);
        
        int copy_bytes;
        int in_end = nbyte - r_bytes;
        int block_end = BLOCK_SIZE - file_des[fildes].offset % BLOCK_SIZE;
        int file_max = check_DIR -> size - file_des[fildes].offset % BLOCK_SIZE;

        if (in_end < block_end && in_end < file_max) {
            copy_bytes = in_end;
        } else if (block_end < file_max) {
            copy_bytes = block_end;
        } else {
            copy_bytes = file_max;
        }

        if (copy_bytes <= 0) {
            break;
        }
        
        memcpy((void*)buf + r_bytes, (void*)r_buf + file_des[fildes].offset % BLOCK_SIZE, copy_bytes);
        file_des[fildes].offset += copy_bytes;
        r_bytes += copy_bytes;
    }

    return r_bytes;
}


int fs_write(int fildes, void *buf, size_t nbyte) {
    // check if valid file descriptor //
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used != 1) return -1;

    dir_entry *check_DIR = DIR + find_DIR_file(file_des[fildes].file);

    int nbytes = 0;
    int current_fat_idx = file_des[fildes].file;
    while (nbytes + BLOCK_SIZE < file_des[fildes].offset && *(FAT + current_fat_idx) != END_FILE) {
        current_fat_idx = *(FAT + current_fat_idx);
        nbytes += BLOCK_SIZE;
    }
    
    char *wr_buf = malloc(BLOCK_SIZE);
    size_t wr_bytes = 0;
    while (wr_bytes < nbyte) {
        if (file_des[fildes].offset % BLOCK_SIZE == 0 && file_des[fildes].offset != 0) {
            int free_block = find_free_FAT_block();
            if (free_block == -1) break;
            *(FAT + current_fat_idx) = free_block;
            current_fat_idx = free_block;
            *(FAT + current_fat_idx) = END_FILE;
        }
        if (fs.fat_idx + current_fat_idx >= DISK_BLOCKS) {
            break;
        }
        block_read(fs.fat_idx + current_fat_idx, wr_buf);
        int copy_bytes;
        int block_end = BLOCK_SIZE - file_des[fildes].offset % BLOCK_SIZE;
        int in_end = nbyte - wr_bytes;

        int nbytes_max_filesize_end = MAX_FILE_SIZE - file_des[fildes].offset;
        
        if (block_end < in_end && block_end < nbytes_max_filesize_end) {
            copy_bytes = block_end;
        } else if (in_end < nbytes_max_filesize_end) {
            copy_bytes = in_end;
        } else {
            copy_bytes = nbytes_max_filesize_end;
        }

        memcpy((void*)wr_buf + file_des[fildes].offset % BLOCK_SIZE, (void*)buf + wr_bytes, copy_bytes);
        block_write(fs.fat_idx + current_fat_idx, wr_buf);
        file_des[fildes].offset += copy_bytes;
        if (file_des[fildes].offset > check_DIR -> size) {
            check_DIR -> size = file_des[fildes].offset;
        }
        wr_bytes += copy_bytes;
    }

    return wr_bytes;
}

int fs_get_filesize(int fildes) {
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used != 1) return -1;
    return (DIR + find_DIR_file(file_des[fildes].file)) -> size;
}

int fs_listfiles(char ***files) {
    char **list = malloc(MAX_FILES*sizeof(char*));
    int i;
    int idx = 0;
    
    for (i = 0; i < MAX_FILES; i++) {
        if ((DIR + i) -> used == 1) {
            *(list + idx) = malloc(MAX_NAME*sizeof(char));
            strcpy(*(list + idx), (DIR + i) -> name);
            idx++;
        }
    }
    *(list + idx) = NULL;
    *files = list;
    
    return 0;
}


int fs_lseek(int fildes, off_t offset) {
    dir_entry *check_DIR = DIR + find_DIR_file(file_des[fildes].file);
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used != 1 || offset < 0 || offset > check_DIR -> size) return -1;
    file_des[fildes].offset = offset;
    return 0;
}


int fs_truncate(int fildes, off_t length) {
    dir_entry *check_DIR = DIR + find_DIR_file(file_des[fildes].file);
    if (fildes < 0 || fildes >= MAX_FILDES || file_des[fildes].used != 1 || length > check_DIR -> size) return -1;

    int last = -1;
    int current = file_des[fildes].file;
    int temp;
    int current_length = 0;

    while (*(FAT + current) != END_FILE) {
        temp = *(FAT + current);
        if (current_length > length) {
            *(FAT + current) = FREE;
            if (*(FAT + last) != FREE)
                *(FAT + last) = END_FILE;
        }
        last = current;
        current = temp;   
    }
   
    check_DIR -> size = length;
    
    int i;
    for (i = 0; i < MAX_FILDES; i++) {
        if (file_des[i].file == file_des[fildes].file && file_des[i].offset > length) {
            file_des[i].offset = length;
        }
    }
    return 0;
}
