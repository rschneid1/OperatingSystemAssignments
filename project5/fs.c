#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk.h"

#define MAX_FILDES 32
#define MAX_FILES 64
#define DEFAULT -1
#define MAX_F_SIZE 16777216
#define MAX_F_NAME 15
#define E_O_F -99

// capabilities
// create
// write
// read
// delete

// superblock
struct super_block {
	int fat_idx; // First block of the FAT
	int fat_len; // Length of FAT in blocks
	int dir_idx; // First block of directory
	int dir_len; // Length of directory in blocks
	int data_idx; // First block of file-data
};


// directory entry (file metadata)
struct dir_entry {
	int used; // Is this file-"slot" in use
	char name [MAX_F_NAME + 1]; // DOH !
	int size; // file size
	int head; // first data block of file
	int ref_cnt;
	// how many open file descriptors are there?
	// ref_cnt > 0 -> cannot delete file
};


// file descriptor
struct file_descriptor {
	int used; // fd in use
	int file; // the first block of the file
		  // (f) to which fd refers too
	off_t offset; // position of fd within f

};

// globals
struct super_block fs; // used for current file system
struct file_descriptor filde_array[MAX_FILDES]; // 32
int *FAT; // Will be populated with the FAT data
struct dir_entry *DIR; // Will be populated with the directory data


int make_fs(char* disk_name){
	if(make_disk(disk_name) == -1){
		return -1;
	}

	// open the disk
	if(open_disk(disk_name) == -1){
		return -1;
	}
	// initialize super block
	struct super_block new_fs;

	/*
	PARAMS:
	32 MB / 4 KB = 8,000 BLOCKS
	sizeof(int) = 4 BYTE , 1 BLOCK stores 1,000 int
	FAT: 1 int per block = 8,000 / (1,000 int/BLOCK) = 8 BLOCKS
	sizeof(dir_entry) = 4 + 16 + 4 + 4 + 4 = 32 B
	64 FILES * 32 B = 2,048 BYTES , ... fits in 1 BLOCK	

	*/

	new_fs.dir_idx = 1;
	new_fs.dir_len = 1;
	new_fs.fat_idx = 2;
	new_fs.fat_len = 9;
	new_fs.data_idx = fs.fat_idx + fs.fat_len; // 11			
		
	// write meta-information to disk
	// set up block
	char* blk = (char*) malloc(BLOCK_SIZE);
	memset(blk, 0 , BLOCK_SIZE);

	// super block
	memcpy((void*) blk, (void*) &new_fs, sizeof(struct super_block));
	if(block_write(0, blk) == -1){
		fprintf(stderr, "make_fs: failed to write superblock\n");
		return -1;
	} // BLOCK 0
		
	// initialize and write directory
	memset(blk, 0, BLOCK_SIZE);
	struct dir_entry sample[MAX_FILES];
	int i;
	for(i = 0; i < MAX_FILES; i++){
		sample[i].used = DEFAULT;	
	}
	memcpy((void*) blk, (void*) &sample, MAX_FILES * sizeof(struct dir_entry));
	if(block_write(new_fs.dir_idx, blk) == -1){
		fprintf(stderr, "make_fs: failed to write directory \n");
		return -1;
	} // BLOCK 1
 		
	// FAT
	FAT = malloc(new_fs.fat_len * BLOCK_SIZE);
	for(i = 0; i < (1000 * new_fs.fat_len); i++){
		FAT[i] = DEFAULT;
	}
	//int* int_blk = (int*) malloc(BLOCK_SIZE);
	memset(blk, DEFAULT, BLOCK_SIZE);
	int block_index = new_fs.fat_idx;
	for(i = 0; i < new_fs.fat_len; i++){
		if(block_write(block_index, blk) == -1){
			fprintf(stderr, "make_fs: failed to write FAT \n");
			return -1;
		} // BLOCKS 2 - 9
		block_index++;
	}

	free(blk);			
	free(FAT);

	// initialize file descriptors
	// int i;
	for(i = 0; i < MAX_FILDES; i++){
		filde_array[i].used = DEFAULT;
		filde_array[i].file = DEFAULT;
		filde_array[i].offset = DEFAULT;		
	}

	if(close_disk() == -1){
		fprintf(stderr, "make_fs: close disk failed \n");
		return -1;
	}
	return 0;
}

int mount_fs(char* disk_name){
	// open the disk
	if(open_disk(disk_name) == -1){
		return -1;
	}

	// load variables from disk
	char* blk = malloc(BLOCK_SIZE);
	memset(blk, 0 , BLOCK_SIZE);

	// read super block and put it in global fs
	if(block_read(0 , blk) == -1){
		fprintf(stderr, "mount_fs: failed to read from superblock\n");
		return -1;
	}
	//fs = (struct* super_block) malloc(sizeof(struct super_block));
	memcpy((void*) &fs, (void*)  blk, sizeof(struct super_block));
				
	// read directory and put it in global pointers
	memset(blk, 0, BLOCK_SIZE);
	if(block_read(fs.dir_idx, blk)){
		fprintf(stderr, "mount_fs: failed to read from directory \n");
		return -1;
	}
	DIR = malloc(fs.dir_len * BLOCK_SIZE);
	memcpy((void*) DIR, (void*) blk, BLOCK_SIZE);	

	// read FAT and put in in global FAT	
	int i;
	FAT =  malloc(fs.fat_len * BLOCK_SIZE);
	int block_index = fs.fat_idx;
	for(i = 0 ; i < fs.fat_len; i++){
		if(block_read(block_index, blk) == -1){
			fprintf(stderr, "mount_fs: failed to read from FAT \n");
			return -1;
		}
		block_index++;
		memcpy((void*) FAT + ( i * BLOCK_SIZE), (void*) blk, BLOCK_SIZE);
	}

	// declare the used spots in FAT as unusable	
	for(i = 0; i < (fs.fat_len + fs.dir_len + 1); i++){
		FAT[i] = E_O_F;
	}

	free(blk);

	return 0;
}

int umount_fs(char* disk_name){

	// write meta-information to disk
	// set up block
	char* blk = malloc(BLOCK_SIZE);
	memset(blk, 0 , BLOCK_SIZE);

	// write super block
	memcpy((void*) blk, (void*) &fs, sizeof(struct super_block));
	if(block_write(0, blk) == -1){
		fprintf(stderr, "unmount_fs: could not write superblock\n");
		return -1;
	} // BLOCK 0
		
	// write directory
	int i;
	int block_index = fs.dir_idx;
	for(i = 0; i < fs.dir_len; i++){
		memset(blk, 0, BLOCK_SIZE);
		memcpy((void*) blk , (void*) DIR + (i * BLOCK_SIZE), BLOCK_SIZE);
		if(block_write(block_index, blk) == -1){
			fprintf(stderr, "unmount_fs: could not write directory \n");
			return -1;
		}
		block_index++;
	}
 		
	// FAT
	block_index = fs.fat_idx;
	for(i = 0; i < fs.fat_len; i++){
		memset(blk, 0, BLOCK_SIZE);
		memcpy((void*) blk, (void*) FAT + (i * BLOCK_SIZE), BLOCK_SIZE);
		if(block_write(block_index, blk) == -1){
			fprintf(stderr, "unmount_fs: could not write FAT \n");
			return -1;
		}
		block_index++;
	}
	

	if(close_disk() == -1){
		fprintf(stderr, "unmount_fs: could not close disk \n");
		return -1;
	}
	free(FAT);
	free(blk);
	return 0;
}

int fs_open(char* name){
	// find file in directory and find open file descriptor
	if(!DIR){
		fprintf(stderr, "fs_open: dir not populated\n");
		return -1;	
	}		
	
	// find find file
	int i;
	int file_index = -1;
	for(i = 0; i < MAX_FILES; i++){
		if((strcmp( DIR[i].name, name) == 0) && DIR[i].used == 1){
			file_index = i;
			break;
		}		
	} 
	if(file_index == -1){
		fprintf(stderr, "fs_open: could not find file in directory\n");
		return -1;
	}

	int open_filde = -1;
	// find open file descriptor
	for(i = 0; i < MAX_FILDES; i++){
		if(filde_array[i].used == -1){
			open_filde = i;
			break;
		}	
	}
	if(open_filde == -1){
		fprintf(stderr, "fs_open: max file descriptors reached \n");
		return -1;
	}	

	filde_array[open_filde].used = 1;
	filde_array[open_filde].file = DIR[file_index].head;
	filde_array[open_filde].offset = 0;

	// increment ref count
	DIR[file_index].ref_cnt++;
	
	return open_filde;
}

int fs_close(int fildes){
	// check
	int index = fildes;
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_close: filde is out of bounds \n");
		return -1;
	}		
	if(filde_array[index].used == DEFAULT){
		fprintf(stderr, "fs_close: filde is not being used \n");
		return -1;
	}

	filde_array[fildes].used = DEFAULT;
	filde_array[fildes].offset = 0;	

	// find file and decrement ref count
	int file_index = -1;
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if(DIR[i].head == filde_array[fildes].file){
			file_index = i;
			break;
		}		
	} 
	if(file_index == -1){
		fprintf(stderr, "fs_close: could not find file in directory\n");
		return -1;
	}
	
	DIR[file_index].ref_cnt--;
	filde_array[fildes].file = DEFAULT;	

	return 0;
}

int fs_create(char *name){
	// check if name is too long
	if(strlen(name) > MAX_F_NAME){
		fprintf(stderr, "fs_create: file name is too long \n");
		return -1;
	}
	// check if file name already exists
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if( (strcmp( DIR[i].name , name) == 0) && DIR[i].used == 1){
			fprintf(stderr,"fs_create: file name already exists \n");
			return -1;
		}
	}
	// find open index in directory
	//int i;
	int file_index = DEFAULT;
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].used == DEFAULT){
			file_index = i;	
			break;
		}	
	}
	if(file_index == DEFAULT){
		fprintf(stderr, "fs_create: could not find open directory slot \n");
		return -1;
	}
	// find open FAT slot
	int FAT_index = DEFAULT;
	for(i = 0; i < DISK_BLOCKS; i++){
		if( FAT[i] == DEFAULT){
			FAT_index = i;
			break;
		}
	}
	if(FAT_index == DEFAULT){
		fprintf(stderr, "fs_create: could not find open block in FAT table \n");
		return -1;
	}	

	// intialize entry
	DIR[file_index].used = 1;
	strcpy(DIR[file_index].name , name);
	DIR[file_index].size = 0;
	DIR[file_index].head = FAT_index;
	DIR[file_index].ref_cnt = 0;		
	FAT[FAT_index] = E_O_F;
	
	return 0;
}

int fs_delete(char *name){
	// error checking: name dne , if fd is open
	if( strlen(name) > MAX_F_NAME){
		fprintf(stderr, "fs_delete: inputted name is too long \n");
		return -1;
	}
	int i;
	int file_index = -1;
	for(i = 0; i < MAX_FILES; i++){
		if( (strcmp(DIR[i].name , name) == 0) && DIR[i].used == 1){
			file_index = i;	
			break;
		}	
	}
	if(file_index == -1){
		fprintf(stderr, "fs_delete: file does not exist in directory \n");
		return -1;
	}	
	if(DIR[file_index].ref_cnt > 0){
		fprintf(stderr, "fs_delete: tried to delete file with open filde descriptor \n");
		return -1;
	}
	
	int start = DIR[file_index].head;
	char* blk = malloc(BLOCK_SIZE);
	memset(blk, 0, BLOCK_SIZE);

	while(FAT[start] != E_O_F){
		if(block_write( start , blk) == -1){
			fprintf(stderr, "fs_delete: failed to write over block \n");
			return -1;
		}
		int temp = FAT[start];
		FAT[start] = DEFAULT;
		start = temp;					
	} 
	// write over E_O_F block
	if(block_write(  start , blk) == -1){
		fprintf(stderr, "fs_delete: failed to write over EOF  block \n");
		return -1;
	}
	FAT[start] = DEFAULT;
	
	DIR[file_index].used = DEFAULT;
	DIR[file_index].size = DEFAULT;
	DIR[file_index].head = DEFAULT;
	DIR[file_index].ref_cnt = DEFAULT;		

	free(blk);
	
	return 0;
}



int fs_read(int fildes, void *buf, size_t nbyte){
	// error checking
	if(filde_array[fildes].used == DEFAULT){
		fprintf(stderr, "fs_read: file descriptor is not used\n");
		return -1;
	}	
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_read: invalid file descriptor \n");
		return -1;
	}

	// buf is assumed to be big enough to hold at least nbytes

	// find file size
	int dir_file_index = -1;
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].head == filde_array[fildes].file && (DIR[i].used == 1)){
			dir_file_index = i;
			break;
		}
	}
	if(dir_file_index == -1){
		fprintf(stderr, "fs_write: could not find filde file in directory \n");
		return -1;
	}
	int size = DIR[dir_file_index].size;
	
	
	// find amount of blocks to be read and where to start
	int offset = filde_array[fildes].offset;

	size_t to_be_read = nbyte;
	int cant_read_past = 0;

	if( nbyte + offset > size){
		int extra = (nbyte + offset) - size;
		to_be_read = nbyte - extra;
	}


	if( nbyte + offset > MAX_F_SIZE){
		int extra = (nbyte + offset) - MAX_F_SIZE;
		to_be_read = nbyte - extra;	
		cant_read_past = 1;
	}
	


	int position = offset / BLOCK_SIZE ; // floor	
	int offset_to_next_byte = offset % BLOCK_SIZE;

	int complete_blocks  =  to_be_read / BLOCK_SIZE ; // floor
	int remainder =	to_be_read  % BLOCK_SIZE;
	
	// iterate to starting position	
	int start = filde_array[fildes].file;
	//int i;
	for(i = 0; i < position; i++){
		if(start == E_O_F){
			fprintf(stderr, "fs_read: offset is greater than file legnth \n");
			return -1;
		}
		start = FAT[start];	
	}

	
	// set up block buffer
	char* blk = malloc(BLOCK_SIZE);
	memset(blk, 0, BLOCK_SIZE);	

	//int i;	
	int bytes_read = 0;
	int reached_end = 0;
	int buf_offset = 0;
	
	// read the byte distance from the offset to the FIRST complete block requested
	if(offset_to_next_byte > 0 && complete_blocks > 0 && cant_read_past == 0){
		if(block_read(start,blk) == -1){
			fprintf(stderr, "fs_read: unable to read from offset to next block \n");
			return -1;
		}
		memcpy((void*) buf, (void*) blk + offset_to_next_byte, (BLOCK_SIZE - offset_to_next_byte));	
		bytes_read = bytes_read + (BLOCK_SIZE - offset_to_next_byte);
		buf_offset = bytes_read;
		start = FAT[start];			  		
	}
	// read the complete blocks (after the offset)
	for(i = 0; i < complete_blocks; i++){
		if(start == E_O_F){
			reached_end = 1;
			break;
		}
		if(block_read(start, blk) == -1){
			fprintf(stderr, "fs_read: unable to read from block \n");
			return -1;
		} 
		memcpy((void*) buf + buf_offset, (void*) blk, BLOCK_SIZE);		
		bytes_read = bytes_read + BLOCK_SIZE;
		buf_offset = bytes_read;
		start = FAT[start];
	}

	// read the remainder of the block after the LAST complete block requested	
	if(remainder > 0 && complete_blocks > 0 && reached_end == 0){
		memset(blk, 0, BLOCK_SIZE);
		if(block_read(start, blk) == -1){
			fprintf(stderr, "fs_read: unable to read remainder from last block \n");
			return -1;
		}
		memcpy((void*) buf + buf_offset, (void*) blk, remainder); 
		bytes_read = bytes_read + remainder;
		buf_offset = bytes_read;
	} else if(remainder > 0 && complete_blocks == 0 && reached_end == 0){
		// read the last block
		if(block_read(start, blk) == -1){
			fprintf(stderr, "fs_read: unable to read remainder from last block \n");
			return -1;
		} 
		memcpy((void*) buf + buf_offset, (void*) blk + offset, remainder);		
		bytes_read = bytes_read + remainder;
		buf_offset = bytes_read;

	}

	filde_array[fildes].offset = offset + bytes_read;
	free(blk);
	
	// number of bytes read
	return bytes_read;
}



int fs_write(int fildes, void *buf, size_t nbyte){
	// error checking
	if(filde_array[fildes].used == DEFAULT){
		fprintf(stderr, "fs_write: file descriptor is not used\n");
		return -1;
	}	
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_write: invalid file descriptor \n");
		return -1;
	}

	// buf is assumed to be big enough to hold at least nbytes
	// find file
	int dir_file_index = -1;
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].head == filde_array[fildes].file && (DIR[i].used == 1)){
			dir_file_index = i;
			break;
		}
	}
	if(dir_file_index == -1){
		fprintf(stderr, "fs_write: could not find filde file in directory \n");
		return -1;
	}
	int size = DIR[dir_file_index].size;
	int offset = filde_array[fildes].offset;
	
	size_t to_be_written = nbyte;
	int reached_end = 0;

	if( nbyte + offset > MAX_F_SIZE){
		int extra = (nbyte + offset) - MAX_F_SIZE;
		to_be_written = nbyte - extra;	
		reached_end = 1;
	}
	
	// find amount of blocks to be written and where to start
	int position = offset / BLOCK_SIZE ; // floor , block_position
	int offset_to_next_byte = offset % BLOCK_SIZE; // acts as offset into block 

	int complete_blocks  = to_be_written / BLOCK_SIZE; // floor
	int remainder =	to_be_written % BLOCK_SIZE;	
	
	

	int temp = 0;
	// iterate to starting position	
	int start = filde_array[fildes].file;
	//int i;
	for(i = 0; i < position; i++){
		if(start == E_O_F){
			fprintf(stderr, "fs_write: offset is greater than file legnth \n");
			return -1;
		}
		temp = start;
		start = FAT[start];	
	}

	
	// set up block buffer
	char* blk = malloc(BLOCK_SIZE);

	//int i;	
	int bytes_written = 0;
	int buf_offset = 0;
	int disk_full = 0;
	
	// write the byte distance from the offset to the FIRST complete block requested
	if(offset_to_next_byte > 0 && complete_blocks > 0 && reached_end == 0){
		if(block_read(start,blk) == -1){
			fprintf(stderr, "fs_write: failed to read from offset to block \n");
			return -1;
		}
		memcpy((void*) blk + offset_to_next_byte, (void*) buf, (BLOCK_SIZE - offset_to_next_byte));	
		if(block_write(start,blk) == -1){
			fprintf(stderr, "fs_write: failed to write from offset to block \n");
			return -1;
		}
		bytes_written = bytes_written + (BLOCK_SIZE - offset_to_next_byte);
		buf_offset = bytes_written;
		if(start  != E_O_F){		
			temp = start;
			start = FAT[start];			  		
		}
	} 
	// write the complete blocks (after the offset)
	for(i = 0; i < complete_blocks; i++){
		if(start == E_O_F){
			if( (complete_blocks - i) > 0){
				// allocate more blocks
				int k;
				int block_index = -1;
				for(k = 0; k < DISK_BLOCKS; k++){
					if( FAT[k] == DEFAULT){
						block_index = k;
						break;
					}
				}
				if( block_index == -1){
					disk_full = 1;
					break;
				}
				
				FAT[temp] = block_index;	
				start = block_index;
				FAT[block_index] = E_O_F;
			}
		}
		//block_read(start, blk); 
		memcpy((void*) blk, (void*) buf + buf_offset, BLOCK_SIZE);		
		if(block_write(start, blk) == -1){
			fprintf(stderr, "fs_write: failed to write to block \n");
			return -1;
		}
		bytes_written = bytes_written + BLOCK_SIZE;
		buf_offset = bytes_written;
		temp = start;
		start = FAT[start];
	}

	// write the remainder of the block after the LAST complete block requested	
	if(remainder > 0 && disk_full == 0 && complete_blocks > 0){
		if(start == E_O_F){
			// allocate more blocks
			int k;
			int block_index = -1;
			for(k = 0; k < DISK_BLOCKS; k++){
				if( FAT[k] == DEFAULT){
					block_index = k;
					break;
				}
			}
			if( block_index == -1){
				disk_full = 1;
				filde_array[fildes].offset = offset + bytes_written;
				return bytes_written;
			}	
			start = block_index;
			FAT[temp] = block_index;	
			FAT[block_index] = E_O_F;

		}
		if(block_read(start, blk) == -1){
			fprintf(stderr, "fs_write: failed to read from remainder of last block \n");
			return -1;
		}
		memcpy((void*) blk , (void*) buf + buf_offset , remainder); 
		if(block_write(start, blk) == -1){
			fprintf(stderr, "fs_write: failed to write to remainder of last block \n");
			return -1;
		}
		bytes_written = bytes_written + remainder;
		buf_offset = bytes_written;
	} else if(remainder > 0 && disk_full == 0 && complete_blocks == 0){
		if( size == 0){
			size = 1;
		}
		if( remainder + offset >= ( (size / BLOCK_SIZE) + ( (size % BLOCK_SIZE) != 0)) * BLOCK_SIZE){
			if( FAT[start] == E_O_F){
				// allocate more blocks
				int k;
				int block_index = -1;
				for(k = 0; k < DISK_BLOCKS; k++){
					if( FAT[k] == DEFAULT){
						block_index = k;
						break;
					}
				}
				if( block_index == -1){
					disk_full = 1;
					filde_array[fildes].offset = offset + bytes_written;
					return bytes_written;
				}	
				
				temp = start;
				start = block_index;
				FAT[temp] = block_index;	
				FAT[block_index] = E_O_F;

			}	
	
		}		
		if(block_read(start, blk) == -1){
			fprintf(stderr, "fs_write: failed to read from remainder of last block \n");
			return -1;
		}
		memmove((void*)blk + (offset % BLOCK_SIZE) , (void*)buf, remainder); 
		if(block_write(start, blk) == -1){
			fprintf(stderr, "fs_write: failed to write to remainder of last block \n");
			return -1;
		}
		bytes_written = bytes_written + remainder;
		buf_offset = bytes_written;

	}	

	filde_array[fildes].offset = offset + bytes_written;

	// update file size
	DIR[dir_file_index].size = DIR[dir_file_index].size + bytes_written;	


	free(blk);		
	// number of bytes written
	return bytes_written;
}

int fs_get_filesize(int fildes){
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_get_filesize: filde is outside max fildes \n");
		return -1;
	}
	if(filde_array[fildes].used == -1){
		fprintf(stderr, "fs_get_filesize: requested fildes is actually unused \n");
		return -1;
	}
	
	int dir_file_index = -1;
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].head == filde_array[fildes].file){
			dir_file_index = i;
			break;
		}
	}
	if(dir_file_index == -1){
		fprintf(stderr, "fs_get_filesize: could not find filde file in directory \n");
		return -1;
	}

	return DIR[dir_file_index].size;
}

int fs_listfiles(char ***files){	
	int i;
	int list_index = 0;
	char** new_files = malloc(MAX_FILES * sizeof(char*));
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].used == 1){
			new_files[list_index] = malloc((MAX_F_NAME+1) * sizeof(char));
			strcpy(new_files[list_index], DIR[i].name);
			list_index++;
		}
	}		
	
	*files = new_files;
	//free(new_files);

	return 0;
}

int fs_lseek(int fildes, off_t offset){
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_lseek: filde is outside max fildes \n");
		return -1;
	}
	if(filde_array[fildes].used == DEFAULT){
		fprintf(stderr, "fs_lseek: requested fildes is actually unused \n");
		return -1;
	}
	if( offset > fs_get_filesize(fildes)){
		fprintf(stderr, "fs_lseek: offset is larger than file size  \n");
		return -1;
	}
	if( offset < 0){
		fprintf(stderr, "fs_lseek: offset is less than zero \n");
		return -1;
	}

	filde_array[fildes].offset = offset;
	
	return 0;
}

int fs_truncate(int fildes, off_t length){
	if(fildes > MAX_FILDES || fildes < 0){
		fprintf(stderr, "fs_truncate: filde is outside max fildes \n");
		return -1;
	}
	if(filde_array[fildes].used == DEFAULT){
		fprintf(stderr, "fs_truncate: requested fildes is actually unused \n");
		return -1;
	}
	if( length < 0){
		fprintf(stderr, "fs_truncate: attemp to truncate to less than zero \n");
		return -1;
	}
	// find file
	int dir_file_index = -1;
	int i;
	for(i = 0; i < MAX_FILES; i++){
		if( DIR[i].head == filde_array[fildes].file){
			dir_file_index = i;
			break;
		}
	}
	if(dir_file_index == -1){
		fprintf(stderr, "fs_truncate: could not find filde file in directory \n");
		return -1;
	}
	if( length > (DIR + dir_file_index)->size){
		fprintf(stderr, "fs_truncate: length is larger than file size \n");
		return -1;
	}	
	
	int size = DIR[dir_file_index].size;	
	if(size > 4096){
		// find amount of blocks to be deleted 
		int total_blocks = size / BLOCK_SIZE + ((size % BLOCK_SIZE) != 0) ; // ceil
		int order[total_blocks + 1];

		int complete_blocks  = length / BLOCK_SIZE ; // floor
		//int remainder = length % BLOCK_SIZE;	

		int order_index = 0;
		
		// iterate to end
		int start = filde_array[fildes].file;
		while(start != E_O_F){
			order[order_index] = start;
			order_index++;
			start = FAT[start];	
		}	

		//int i;
		for(i = total_blocks; i >= complete_blocks; i--){
			FAT[order[i]] = DEFAULT;
		}
	
		int last = total_blocks - complete_blocks;
		FAT[last] = E_O_F;
	}
	
	filde_array[fildes].offset = length;
	DIR[dir_file_index].size = length;

	return 0;
}


