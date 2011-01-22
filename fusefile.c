/*
 * File:    fusefile.c
 * Copyright (c) 2007 Vitaly "_Vi" Shukela
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Contact Email: public_vi@tut.by
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *   
 * gcc -O2 `pkg-config fuse --cflags --libs` fusefile.c -o fusefile
 */

#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <time.h>
#include <stdlib.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>  // for fprintf
#include <malloc.h> // for malloc


int fd;

char mode;
off_t st_offset;
off_t st_size; // Have different meanings when mode is 'a' or 'A'
int st_mode;

static int fusefile_getattr(const char *path, struct stat *stbuf)
{

    if(strcmp(path, "/") != 0)
        return -ENOENT;
	
	
    if(-1 == fstat(fd, stbuf)) {
	return -errno;
    }
	    fprintf(stderr, "st_mode=%d\n", stbuf->st_mode);

    if(st_size!=-1 && mode != 'a' && mode!='A') {
	stbuf->st_size=st_size;
    } else {
	stbuf->st_size-=st_offset;
    }

    if(st_mode!=-1) {
	stbuf->st_mode = st_mode;
    }

    return 0;
}

static int fusefile_truncate(const char *path, off_t size)
{
    (void) size;

    if(strcmp(path, "/") != 0)
        return -ENOENT;
    
    if(st_size!=-1 && size != st_size) {
	return 0; // Ignore invalid truncate attempts
    }
    if(mode=='r') {
	return -EACCES;
    }

    size+=st_offset;

    if(ftruncate(fd, size)==-1) {
	return -errno;
    }

    return 0;

}

static int fusefile_open(const char *path, struct fuse_file_info *fi)
{
    (void) fi;


    if(strcmp(path, "/") != 0)
        return -ENOENT;

    return 0;
}

static int fusefile_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    int res;

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    if(mode=='W' || mode=='A') {
	return -EACCES;
    }

    if(st_size!=-1 && offset+size>st_size) {
	if(mode!='a') {
	    size=st_size-offset; // Do not allow reading beyound st_size
	}
    }

    offset+=st_offset;
    res=pread64(fd, buf, size, offset);

    if (res == -1)
        res = -errno;

    return res;
    
}


static int fusefile_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    if(mode=='r') {
	return -EACCES;
    }

    if(mode=='a' || mode=='A') {
	if(offset != st_size) {
	    return -EACCES;
	}
    } else {
	if(st_size!=-1 && offset+size>st_size) {
	    size=st_size-offset; // Do not allow writing beyound st_size
	    if(size<=0) {
		return -ENOSPC;
	    }
	}
    }

    offset+=st_offset;
    int res=pwrite64(fd, buf, size, offset);

    if (res == -1) {
        res = -errno;
    } else {
	if(st_size!=-1) {
	    if(offset+res > st_offset+st_size) {
		st_size=offset+res-st_offset;
	    }
	}
    }

    return res;
}

static int fusefile_utimens(const char *path, const struct timespec ts[2]){

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    return 0;
}       

static int fusefile_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    if(strcmp(path, "/") != 0)
        return -ENOENT;

    int ret=fsync(fd);
    if(ret==-1)ret=-errno;

    return ret;
}


static struct fuse_operations fusefile_oper = {
    .getattr	= fusefile_getattr,
    .truncate	= fusefile_truncate,
    .open	= fusefile_open,
    .read	= fusefile_read,
    .write	= fusefile_write,   
    .utimens    = fusefile_utimens, 
    .fsync	= fusefile_fsync,
};

int main(int argc, char *argv[])
{
    int ret,i;
    char* argv2[argc-1+2];  // File name removed, "-o nonempty,direct_io" added
    int our_arguments_count=3; /* argv[0], source file and mount point */

    char* source_file;
    char* mountpoint_file;

    
    st_size=-1;
    st_mode=-1;
    st_offset=0;

    if(argc<3){
	fprintf(stderr,"fusefile alpha version. Created by _Vi.\n");
	fprintf(stderr,"Usage: %s file_or_device mountpoint_file [{-w|-r|-W|-a|-A}] [-O offset] [-S size] [-M st_mode] [FUSE_options]\n",argv[0]);
	fprintf(stderr,"readwrite, readonly, writeonly, append_only+read, append_only\n");
	fprintf(stderr,"Examples:\n");
	fprintf(stderr,"    fusefile source mountpoint\n");
	fprintf(stderr,"    fusefile /dev/frandom mountpoint -r -S 1024 -M 0100666\n");
	fprintf(stderr,"    fusefile /dev/sda sda1 -r -S 1024 -O $((63*512)) -S $((626535*512)) -M 0100600\n");
	fprintf(stderr,"    fusefile /var/log/mylog publiclog -a -M 0100777 -o allow_other\n");
	fprintf(stderr,"Remember to \"touch\" your mountpoints, not \"mkdir\" them.\n");
	return 1;
    }

    mode='w';
    if(argc>=4 && *argv[3]=='-') {
	++our_arguments_count;
	mode=argv[3][1];
    }

    for(;argv[our_arguments_count];) {
	if(!strcmp(argv[our_arguments_count], "-O")) {
	    ++our_arguments_count;
	    sscanf(argv[our_arguments_count], "%lli", &st_offset);
	    ++our_arguments_count;
	} else
	if(!strcmp(argv[our_arguments_count], "-M")) {
	    ++our_arguments_count;
	    sscanf(argv[our_arguments_count], "%i", &st_mode);
	    ++our_arguments_count;
	} else
	if(!strcmp(argv[our_arguments_count], "-S")) {
	    if(mode=='A' || mode=='a') {
		fprintf(stderr, "Append mode and size specification are incompatible\n");
		return 1;
	    }
	    ++our_arguments_count;
	    sscanf(argv[our_arguments_count], "%lli", &st_size);
	    ++our_arguments_count;
	} else {
	    break;
	}
    }
    {
        int openmode;
	switch(mode) {
	    case 'r':
		openmode=O_RDONLY;
		break;
	    case 'w':
		openmode=O_RDWR;
		break;
	    case 'W':
		openmode=O_WRONLY;
		break;
	    case 'a':
		openmode=O_RDWR;
		break;
	    case 'A':
		openmode=O_WRONLY;
		break;
	    default:
		fprintf(stderr, "Invalid mode '%c'\n", mode);
		return 1;
	}
	fd=open(argv[1],openmode);
    }
    if(fd<0){
	if(mode=='w' || mode=='a') {
	    mode='r';
	    fprintf(stderr, "Trying to opening read-only\n");
	    fd=open(argv[1],O_RDONLY);
	}
    }
    if(fd<0){
	perror("open");
	return 1;
    }
    
    if(mode=='a' || mode=='A') {
	struct stat stbuf;
	fstat(fd, &stbuf);
	st_size = stbuf.st_size - st_offset;
    }

    int argc2=0;
    argv2[argc2++]=argv[0];
    argv2[argc2++]=argv[2]; // mount point file
    for(i=our_arguments_count;i<argc;++i)argv2[argc2++]=argv[i];
    argv2[argc2++]="-o";
    argv2[argc2++]="nonempty,direct_io";
    argv2[argc2]=0;

    ret=fuse_main(argc2, argv2, &fusefile_oper, NULL);

    close(fd);

    return ret;
}
