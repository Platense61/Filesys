#include "crash.h"
#include "parse.h"
#include "userfs.h"
#include <math.h>
#include <dirent.h>

/* GLOBAL  VARIABLES */
int virtual_disk;
/*Use these variable as a staging space before they get writen out to disk*/
superblock sb;
BIT bit_map[BIT_MAP_SIZE];
dir_struct dir;
inode curr_inode;
char buffer[BLOCK_SIZE_BYTES]; /* assert( sizeof(char) ==1)); */

/*
  man 2 read
  man stat
  man memcopy
*/

void usage (char * command) {
	fprintf(stderr, "Usage: %s -reformat disk_size_bytes file_name\n", command);
	fprintf(stderr, "        %s file_ame\n", command);
}

char * buildPrompt() {
	return  "%";
}


int main(int argc, char** argv) {
	char * cmd_line;
	/* info stores all the information returned by parser */
	parseInfo *info;
	/* stores cmd name and arg list for one command */
	struct commandType *cmd;

	init_crasher();

	if ((argc == 4) && (argv[1][1] == 'r')) {
		/* ./userfs -reformat diskSize fileName */
		if (!u_format(atoi(argv[2]), argv[3])) {
			fprintf(stderr, "Unable to reformat\n");
			exit(-1);
		}
	} else if (argc == 2) {
		/* ./userfs fileName will attempt to recover a file. */
		if ((!recover_file_system(argv[1]))) {
			fprintf(stderr, "Unable to recover virtual file system from file: %s\n", argv[1]);
			exit(-1);
		}
	} else {
		usage(argv[0]);
		exit(-1);
	}

	/* before begin processing set clean_shutdown to FALSE */
	sb.clean_shutdown = 0;
	lseek(virtual_disk, BLOCK_SIZE_BYTES*SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();
	fprintf(stderr, "userfs available\n");

	while(1) {
		cmd_line = readline(buildPrompt());
		if (cmd_line == NULL) {
			fprintf(stderr, "Unable to read command\n");
			continue;
		}

		/* calls the parser */
		info = parse(cmd_line);
		if (info == NULL) {
			free(cmd_line);
			continue;
		}

		/* com contains the info. of command before the first "|" */
		cmd = &info->CommArray[0];
		if ((cmd == NULL) || (cmd->command == NULL)) {
			free_info(info);
			free(cmd_line);
			continue;
		}

		/************************   u_import ****************************/
		if (strncmp(cmd->command, "u_import", strlen("u_import")) == 0){
			if (cmd->VarNum != 3){
				fprintf(stderr, "u_import externalFileName userfsFileName\n");
			} else {
				if (!u_import(cmd->VarList[1], cmd->VarList[2])) {
					fprintf(stderr, "Unable to import external file %s into userfs file %s\n", cmd->VarList[1], cmd->VarList[2]);
				}
			}


		/************************   u_export ****************************/
		} else if (strncmp(cmd->command, "u_export", strlen("u_export")) == 0){
			if (cmd->VarNum != 3){
				fprintf(stderr, "u_export userfsFileName externalFileName \n");
			} else {
				if (!u_export(cmd->VarList[1], cmd->VarList[1])) {
					fprintf(stderr, "Unable to export userfs file %s to external file %s\n", cmd->VarList[1], cmd->VarList[2]);
				}
			}


		/************************   u_del ****************************/
		} else if (strncmp(cmd->command, "u_del", strlen("u_export")) == 0) {
			if (cmd->VarNum != 2){
				fprintf(stderr, "u_del userfsFileName \n");
			} else {
				if (!u_del(cmd->VarList[1]) ){
					fprintf(stderr, "Unable to delete userfs file %s\n", cmd->VarList[1]);
				}
			}


		/******************** u_ls **********************/
		} else if (strncmp(cmd->command, "u_ls", strlen("u_ls")) == 0) {
			u_ls();


		/********************* u_quota *****************/
		} else if (strncmp(cmd->command, "u_quota", strlen("u_quota")) == 0) {
			int free_blocks = u_quota();
			fprintf(stderr, "Free space: %d bytes %d blocks\n", free_blocks * BLOCK_SIZE_BYTES, free_blocks);


		/***************** exit ************************/
		} else if (strncmp(cmd->command, "exit", strlen("exit")) == 0) {
			/* 
			 * take care of clean shut down so that u_fs
			 * recovers when started next.
			 */
			if (!u_clean_shutdown()){
				fprintf(stderr, "Shutdown failure, possible corruption of userfs\n");
			}
			exit(1);


		/****************** other ***********************/
		} else {
			fprintf(stderr, "Unknown command: %s\n", cmd->command);
			fprintf(stderr, "\tTry: u_import, u_export, u_ls, u_del, u_quota, exit\n");
		}


		free_info(info);
		free(cmd_line);
	}
}

/*
 * Initializes the bit map.
 */
void init_bit_map() {
	for (int i = 0; i < BIT_MAP_SIZE; i++) {
		bit_map[i] = 0;
	}
}

void allocate_block(int blockNum) {
	assert(blockNum < BIT_MAP_SIZE);
	bit_map[blockNum] = 1;
}

void free_block(int blockNum) {
	assert(blockNum < BIT_MAP_SIZE);
	bit_map[blockNum] = 0;
}

int superblockMatchesCode() {
	if (sb.size_of_super_block != sizeof(superblock)){
		return 0;
	}
	if (sb.size_of_directory != sizeof (dir_struct)){
		return 0;
	}
	if (sb.size_of_inode != sizeof(inode)){
		return 0;
	}
	if (sb.block_size_bytes != BLOCK_SIZE_BYTES){
		return 0;
	}
	if (sb.max_file_name_size != MAX_FILE_NAME_SIZE){
		return 0;
	}
	if (sb.max_blocks_per_file != MAX_BLOCKS_PER_FILE){
		return 0;
	}
	return 1;
}

void init_superblock(int diskSizeBytes) {
	sb.disk_size_blocks  = diskSizeBytes/BLOCK_SIZE_BYTES;
	sb.num_free_blocks = u_quota();
	sb.clean_shutdown = 1;

	sb.size_of_super_block = sizeof(superblock);
	sb.size_of_directory = sizeof (dir_struct);
	sb.size_of_inode = sizeof(inode);

	sb.block_size_bytes = BLOCK_SIZE_BYTES;
	sb.max_file_name_size = MAX_FILE_NAME_SIZE;
	sb.max_blocks_per_file = MAX_BLOCKS_PER_FILE;
}

int compute_inode_loc(int inode_number) {
	int whichInodeBlock;
	int whichInodeInBlock;
	int inodeLocation;

	whichInodeBlock = inode_number/INODES_PER_BLOCK;
	whichInodeInBlock = inode_number%INODES_PER_BLOCK;

	inodeLocation = (INODE_BLOCK + whichInodeBlock) *BLOCK_SIZE_BYTES +
		whichInodeInBlock*sizeof(inode);

	return inodeLocation;
}

int write_inode(int inode_number, inode * in) {
	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);

	lseek(virtual_disk, inodeLocation, SEEK_SET);
	crash_write(virtual_disk, in, sizeof(inode));

	sync();

	return 1;
}


int read_inode(int inode_number, inode * in) {
	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);

	lseek(virtual_disk, inodeLocation, SEEK_SET);
	/* changed the read to crash read*/
	crash_read(virtual_disk, in, sizeof(inode));

	return 1;
}


/*
 * Initializes the directory.
 */
void init_dir() {
	for (int i = 0; i< MAX_FILES_PER_DIRECTORY; i++) {
		dir.u_file[i].free = 1;
	}

}


/*
 * Returns the number of free blocks in the file system.
 */
int u_quota() {
	int freeCount=0;

	/* It might be advantageous to return sb.num_free_blocks if you keep it up to
	   date, which is up to you to do. Do keep in mind when that number might be
	   invalid and thus requires the following. */

	// Calculate the number of free blocks
	for (int i = 0; i < sb.disk_size_blocks; i++) {
		/* Right now we are using a full unsigned int
		   to represent each bit - we really should use
		   all the bits in there for more efficient storage */
		if (bit_map[i] == 0) {
			freeCount++;
		}
	}
	return freeCount;
}

/*
 * Imports a linux file into the u_fs
 * Need to take care in the order of modifying the data structures 
 * so that it can be revored consistently.
 *
 * Returns 1 on success, 0 on failure.
 *
 * TODO: Implement the rest of this function.
 check slides
 find free in bitmap so iterate through
 */
int u_import(char* linux_file, char* u_file) {
	int free_space;
	free_space = u_quota();

	//lets first check that the u_file does not exceed the maximum length specified by our filesystem
	if(strlen(u_file) > MAX_FILE_NAME_SIZE){
		printf("Error, file name is to long max file name length is 15\n");
		return 0;
	}

	//now lets make sure we have not reached max number of files in directory
	if(dir.num_files > MAX_FILES_PER_DIRECTORY){
		printf("Error, max number of files in directory reached\n");
		return 0;
	}

	//lets check to see if we have an available inode
	int check = -5;
	for(int i = 0; i < MAX_INODES; i++){
		read_inode(i, &curr_inode);
		if (curr_inode.free == 1){
			check = i;
			break;
		}
	}

	if(check > MAX_INODES){
		printf("Error: ran out of inodes\n");
		return 0;
	}

	int handle = open(linux_file, O_RDONLY);
	if (handle == -1) {
		printf("error, reading file %s\n", linux_file);
		return 0;
	}

	/*we need to get information on input file, following is link to man page for stat structure
		https://man7.org/linux/man-pages/man2/lstat.2.html
	*/
	struct stat buf;
	int err = fstat(handle, &buf);
	if(err == -1){
		printf("Error: problem with reading file info\n");
		return 0;
	}

	int new_size = (int) buf.st_size;
	if(new_size > free_space*BLOCK_SIZE_BYTES){
		printf("Error: Not enough space in File System\n");
		return 0;
	}

	/*we now need to find how many blocks are in the new file
	  to do this we would just need to divide the size of the file by the block size (use ceil and doubles for simplicity)
	*/
	int number_of_blocks  = ceil(new_size/BLOCK_SIZE_BYTES);

	int itr = 8;
	for(int i = 0; i <= number_of_blocks; i++){
		
		for(itr; itr < BLOCK_SIZE_BYTES/sizeof(BIT); itr++){
			if(bit_map[itr] == 0){
				break;
			}
		}
		
		DISK_LBA loc = itr * BLOCK_SIZE_BYTES;
		read(handle, &buffer, BLOCK_SIZE_BYTES);
		allocate_block(itr);
		lseek(virtual_disk, loc, SEEK_SET);
		crash_write(virtual_disk, &buffer, BLOCK_SIZE_BYTES);
		curr_inode.blocks[i] = loc;
		//read_inode(itr, &curr_inode);
		
	}

	/*
	Now just update all relevant meta data stuff
	*/
	int lseek_prob = lseek(virtual_disk, BLOCK_SIZE_BYTES*BIT_MAP_BLOCK, SEEK_SET);
	if(lseek_prob == -1)
		printf("Error: lseek failes to return offset of bitmap location");
	crash_write(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE);
	sync();

	curr_inode.num_blocks = number_of_blocks;
	curr_inode.file_size_bytes = new_size;
	time(&curr_inode.last_modified);
	curr_inode.free = FALSE;
	write_inode(check, &curr_inode);

	file_struct meta;
	meta.inode_number = check;
	memcpy(meta.file_name, u_file, strlen(u_file)+1);
	meta.free = 0;

	dir.u_file[dir.num_files] = meta;
	dir.num_files++;
	

	lseek(virtual_disk, BLOCK_SIZE_BYTES*DIRECTORY_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &dir, sizeof(dir_struct));
	sync();
	
	
	//read(handle,&buffer,BLOCK_SIZE_BYTES);

	//crash_write(virtual_disk, &buffer, 1999);

	/* Here are some things to think about as you write this code. This isn't an
	 * exhaustive list nor is this the order of operations you should follow.
	 *
	 * - Is there free space within the metadata allocations (indoes, directory,
	 *   etc) for a new file?
	 * - Is the file data too big to import?
	 * - What order should file system metadata be updated?
	 */
 
	return 1;
}



/*
 * Exports a u_file to linux.
 * Need to take care in the order of modifying the data structures 
 * so that it can be recovered consistently.
 *
 * Return 1 on success, 0 on failure.
 *
 * TODO: Implement this function.
 */
int u_export(char* u_file, char* linux_file) {
	//crash read atleast once
	if(strlen(u_file) > MAX_FILE_NAME_SIZE){
		printf("Error, file name is to long max file name length is 15\n");
		return 0;
	}

	/* This code segment below verifies that the u_file does not already exist in linux dir */
	DIR *check;
  	struct dirent *myDir;
  	check = opendir(".");
  	if (check) {
    	while ((myDir = readdir(check)) != NULL) {
			if((strcmp(myDir->d_name, linux_file)) == 0){
				printf("Error: Linux file exists already\n");
				return 0;
			}
    	}
    	closedir(check);
  	}

	/* open the linux file*/
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int src = open(linux_file, O_WRONLY | O_APPEND | O_CREAT, mode);
	if(src < 0){
		printf("error, reading file %s\n", linux_file);
		return 0;
	}

	for (int i = 0; i< MAX_FILES_PER_DIRECTORY; i++) {
		if ((strcmp(dir.u_file[i].file_name, u_file)) == 0) {
			
			//read_inode(dir.u_file[i].inode_number, &curr_inode);
			read_inode(dir.u_file[i].inode_number, &curr_inode);
			int number_of_blocks = ceil(curr_inode.file_size_bytes/BLOCK_SIZE_BYTES);
			
			for(int x = 0; x <= number_of_blocks; x++){

				lseek(virtual_disk, curr_inode.blocks[x], SEEK_SET);
				crash_read(virtual_disk, &buffer, BLOCK_SIZE_BYTES);
				write(src, &buffer, BLOCK_SIZE_BYTES-1);
			}
			sync();
			close(src);
			return 1;
		}
	
	}


	return 1;
	
}


/*
 * Deletes the file from u_fs.
 * Keep the order of updates to data structures in mind to ensure consistency.
 *
 * Return 1 on success, 0 on failure.
 *
 * TODO: Implement this function.
 */
int u_del(char* u_file) {
	
	for(int i = 0; i < dir.num_files; i++){
		if((strcmp(dir.u_file[i].file_name, u_file) == 0)){
			dir.u_file[i].free = 1;
			dir.num_files--;

			//lets free the inode
			read_inode(dir.u_file[i].inode_number, &curr_inode);
			curr_inode.free = 1;

			//must free the bitmap aswell, believe this is how we do it
			for(int i = 0; i < curr_inode.num_blocks; i++){
				free_block(curr_inode.blocks[i]/BLOCK_SIZE_BYTES);
			}

			//not sure if following 2 lines are correct steps (atleast in order)
			//lseek(virtual_disk, BLOCK_SIZE_BYTES*INODE_BLOCK, SEEK_SET);
			//crash_write(virtual_disk, &dir, sizeof(dir_struct));
			return 1;

		}
	}
	return 0;
}

/*
 * Checks the file system for consistency.
 * Detects if there are orphaned inodes or blocks, and tries its best
 * to recover these objects.
 *
 * Return 1 on success, 0 on failure.
 *
 * TODO: Implement this function.
 */
int u_fsck() {
	return 0;
}

/*
 * Iterates through the directory and prints the 
 * file names, size and last modified date and time.
 */
void u_ls() {
	struct tm *loc_tm;
	int numFilesFound = 0;

	for (int i = 0; i< MAX_FILES_PER_DIRECTORY; i++) {
		if (!(dir.u_file[i].free)) {
			numFilesFound++;
			/* file_name size last_modified */
			
			read_inode(dir.u_file[i].inode_number, &curr_inode);
			loc_tm = localtime(&curr_inode.last_modified);
			fprintf(stderr,"%s\t%d\t%d/%d\t%d:%d\n",dir.u_file[i].file_name, 
				curr_inode.num_blocks*BLOCK_SIZE_BYTES, 
				loc_tm->tm_mon, loc_tm->tm_mday, loc_tm->tm_hour, loc_tm->tm_min);

		}
	}

	if (numFilesFound == 0){
		fprintf(stdout, "Directory empty\n");
	}

}

/*
 * Formats the virtual disk. Saves the superblock
 * bit map and the single level directory.
 */
int u_format(int diskSizeBytes, char* file_name) {
	int minimumBlocks;

	/* create the virtual disk */
	if ((virtual_disk = open(file_name, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) < 0) {
		fprintf(stderr, "Unable to create virtual disk file: %s\n", file_name);
		return 0;
	}


	fprintf(stderr, "Formatting userfs of size %d bytes with %d block size in file %s\n", diskSizeBytes, BLOCK_SIZE_BYTES, file_name);

	minimumBlocks = 3+ NUM_INODE_BLOCKS+1;
	if (diskSizeBytes/BLOCK_SIZE_BYTES < minimumBlocks) {
		/* If it's impossible to have the superblock, bitmap,
		   directory, inodes and at least one datablock then
			 there no point attempting to continue. */
		fprintf(stderr, "Minimum size virtual disk is %d bytes %d blocks\n", BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		fprintf(stderr, "Requested virtual disk size %d bytes results in %d bytes %d blocks of usable space\n", diskSizeBytes, BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		return 0;
	}


	/*************************  BIT MAP **************************/
	assert(sizeof(BIT)* BIT_MAP_SIZE <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for bitmap (%ld bytes required)\n", 1, BLOCK_SIZE_BYTES, sizeof(BIT)*BIT_MAP_SIZE);
	fprintf(stderr, "\tImplies Max size of disk is %ld blocks or %ld bytes\n", BIT_MAP_SIZE, BIT_MAP_SIZE*BLOCK_SIZE_BYTES);

	if (diskSizeBytes >= BIT_MAP_SIZE* BLOCK_SIZE_BYTES) {
		fprintf(stderr, "Unable to format a userfs of size %d bytes\n", diskSizeBytes);
		return 0;
	}

	init_bit_map();

	/* first three blocks will be taken with the 
	   superblock, bitmap and directory */
	allocate_block(BIT_MAP_BLOCK);
	allocate_block(SUPERBLOCK_BLOCK);
	allocate_block(DIRECTORY_BLOCK);
	/* next NUM_INODE_BLOCKS will contain inodes */
	for (int i = 3; i < 3+NUM_INODE_BLOCKS; i++) {
		allocate_block(i);
	}

	lseek(virtual_disk, BLOCK_SIZE_BYTES*BIT_MAP_BLOCK, SEEK_SET);
	crash_write(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE);


	/***********************  DIRECTORY  ***********************/
	assert(sizeof(dir_struct) <= BLOCK_SIZE_BYTES);

	fprintf(stderr, "%d blocks %d bytes reserved for the userfs directory (%ld bytes required)\n", 1, BLOCK_SIZE_BYTES, sizeof(dir_struct));
	fprintf(stderr, "\tMax files per directory: %d\n", MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Directory entries limit filesize to %d characters\n", MAX_FILE_NAME_SIZE);

	init_dir();
	lseek(virtual_disk, BLOCK_SIZE_BYTES*DIRECTORY_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &dir, sizeof(dir_struct));


	/***********************  INODES ***********************/
	fprintf(stderr, "userfs will contain %ld inodes (directory limited to %d)\n", MAX_INODES, MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Inodes limit filesize to %d blocks or %d bytes\n", MAX_BLOCKS_PER_FILE, MAX_BLOCKS_PER_FILE* BLOCK_SIZE_BYTES);

	curr_inode.free = 1;
	for (int i = 0; i < MAX_INODES; i++) {
		write_inode(i, &curr_inode);
	}


	/***********************  SUPERBLOCK ***********************/
	assert(sizeof(superblock) <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for superblock (%ld bytes required)\n", 1, BLOCK_SIZE_BYTES, sizeof(superblock));
	init_superblock(diskSizeBytes);
	fprintf(stderr, "userfs will contain %d total blocks: %d free for data\n", sb.disk_size_blocks, sb.num_free_blocks);
	fprintf(stderr, "userfs contains %ld free inodes\n", MAX_INODES);

	lseek(virtual_disk, BLOCK_SIZE_BYTES*SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();

	/* when format complete there better be at 
	   least one free data block */
	assert(u_quota() >= 1);
	fprintf(stderr,"Format complete!\n");

	return 1;
}

/*
 * Attempts to load a file system given the virtual disk name
 * It will perform an automated fsck, which will try to recover any
 * potentially lost data and fix inconsistencies.
 */
int recover_file_system(char *file_name) {
	if ((virtual_disk = open(file_name, O_RDWR)) < 0) {
		printf("virtual disk open error\n");
		return 0;
	}

	/* read the superblock */
	lseek(virtual_disk, BLOCK_SIZE_BYTES*SUPERBLOCK_BLOCK, SEEK_SET);
	read(virtual_disk, &sb, sizeof(superblock));

	/* read the bit_map */
	lseek(virtual_disk, BLOCK_SIZE_BYTES*BIT_MAP_BLOCK, SEEK_SET);
	read(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE);

	/* read the single level directory */
	lseek(virtual_disk, BLOCK_SIZE_BYTES* DIRECTORY_BLOCK, SEEK_SET);
	read(virtual_disk, &dir, sizeof(dir_struct));

	if (!superblockMatchesCode()) {
		fprintf(stderr, "Unable to recover: userfs appears to have been formatted with another code version\n");
		return 0;
	}
	if (!sb.clean_shutdown) {
		/* Try to recover your file system */
		fprintf(stderr, "u_fsck in progress......");
		if (u_fsck()) {
			fprintf(stderr, "Recovery complete\n");
			return 1;
		} else {
			fprintf(stderr, "Recovery failed\n");
			return 0;
		}
	} else {
		fprintf(stderr, "Clean shutdown detected\n");
		return 1;
	}
}

/* Cleanly performs a shutdown and ensures everything is consistent.
 * Pay attention to what order you update values in case a crash occurs during
 * the shutdown.
 *
 * Returns 1 on success, 0 on failure.
 *
 * TODO: Finish implementing this function.
 */
int u_clean_shutdown() {
	sb.num_free_blocks = u_quota();
	sb.clean_shutdown = 1;

	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();

	lseek(virtual_disk, BLOCK_SIZE_BYTES* BIT_MAP_BLOCK, SEEK_SET);
	crash_write(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE);
	sync();

	lseek(virtual_disk, BLOCK_SIZE_BYTES*DIRECTORY_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &dir, sizeof(dir_struct));
	sync();

	close(virtual_disk);
	/* is this all that needs to be done on clean shutdown? */
	return 0;
}
