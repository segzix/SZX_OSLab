
#include "os/task.h"
#include "screen.h"
#include <os/fs.h>
#include <os/sched.h>
#include <os/time.h>
#include <printk.h>
// #include <stdint.h>
#include <os/string.h>

char cat_buff[1024];

int do_mkfs(void)
{
    // TODO [P6-task1]: Implement do_mkfs
    // init_fdesc_array();
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;

    superblock_t *superblock = (superblock_t *) super_block;//超级块不能用tempblock，因为这个信息后面还有用

    printk("\n> [FS] Start initialize filesystem!\n");

    superblock->magic = SUPER_MAGIC;
    superblock->start = SUPER_START;
    superblock->siz = FS_MAX_BLOCKSIZ;

    superblock->blockbitmap_offset = 1;
    superblock->blockbitmap_siz = FS_MAX_BLOCKSIZ/BLOCK_BIT_SIZ;

    superblock->inodebytemap_offset = superblock->blockbitmap_offset + superblock->blockbitmap_siz;
    superblock->inodebytemap_siz = 1;//只有前1kbyte有效

    superblock->inodetable_offset = superblock->inodebytemap_offset + superblock->inodebytemap_siz;
    superblock->inodetable_siz = 8;//一共可以提供1k个inode

    superblock->datablock_offset = superblock->inodetable_offset + superblock->inodetable_siz;
    superblock->datablock_siz =  superblock->siz - 
                                (superblock->inodetable_siz + superblock->inodebytemap_siz + superblock->blockbitmap_siz + 1);

    printk("> [FS] Setting superblock...\n");
    bios_sd_write(kva2pa((uintptr_t)super_block), 1, blockid2sectorid(0));
    memset(super_block, 0, BLOCK_SIZ);
    bios_sd_read(kva2pa((uintptr_t)super_block), 1, blockid2sectorid(0));
    //本来要读一个数据块，但superblock块单独处理，因此这个地方其实只用对一个扇区进行操作就可以
	
    printk("       magic : 0x%x\n", superblock->magic);
    printk("       num sector : 0x%x, start sector : 0x%x\n", superblock->siz, superblock->start);
	printk("       block bitmap offset : %d (%d)\n", superblock->blockbitmap_offset,superblock->blockbitmap_siz);
	printk("       inode bitmap offset : %d (%d)\n", superblock->inodebytemap_offset,superblock->inodebytemap_siz);
	printk("       inode table offset : %d (%d)\n", superblock->inodetable_offset,superblock->inodetable_siz);
	printk("       data offset : %d (%d)\n", superblock->datablock_offset,superblock->datablock_siz);
	printk("       inode entry size : %dB dir entry size : %dB\n", sizeof(inode_t), sizeof(dentry_t));

    init_bcache();

    printk("> [FS] Setting blockbit-map...\n");
    uint8_t* blockbit = init_block(superblock->blockbitmap_offset);
    blockbit[0] = 0xff;
    blockbit[1] = 0x3f;//前14个块已经被占了
    bwrite(superblock->blockbitmap_offset,blockbit);//block对应的掩码
    for(int i = 1; i<superblock->blockbitmap_siz; i++)
        init_block(superblock->blockbitmap_offset + i);//其它块全部填0

    printk("> [FS] Setting inodebyte-map...\n");
    for(int i=0; i<superblock->inodebytemap_siz; i++)
        init_block(superblock->inodebytemap_offset + i);

    //inode bytemap
    uint8_t* inodebyte = init_block(superblock->inodebytemap_offset);
    inodebyte[0] = 1;
    bwrite(superblock->inodebytemap_offset,inodebyte);//inode对应的掩码


    printk("> [FS] Setting inode...\n");
    inode_t* inode = (inode_t*)init_block(superblock->inodetable_offset);
    inode->mode = INODE_DIR;
    inode->owner_pid = (*current_running)->pid;
    inode->hardlinks = 0;
    inode->fd_index = 0xff;
    inode->filesz = 0;
    inode->mtime = get_timer();
    bwrite(superblock->inodetable_offset,(uint8_t*)inode);//inode分配一个

    //data块的表项分配
    dentry_t dentry[2];
    memset(dentry, 0, 2*sizeof(dentry_t));
    dentry[0].dentry_valid = 1;
    dentry[0].ino = 0;
    strcpy((char*)(dentry[0].name), (char *)".");//name拷贝

    dentry[1].dentry_valid = 1;
    dentry[1].ino = 0;
    strcpy((char *)(dentry[1].name), (char *)"..");//name拷贝

    bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(superblock->datablock_offset));//测试
    write_file(inode, (char*)dentry, inode->filesz, 2*sizeof(dentry_t));
    bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(superblock->datablock_offset));//测试

    printk("> Initialize filesystem finished!\n");

    for(int i = 0;i < TASK_MAXNUM;i++){
        pcb[i].pwd = 0;
        strcpy(pcb[i].pwd_dir,"/");
    }

    return 13;  // do_mkfs succeeds
}

int do_statfs(void)
{
    bios_sd_read(kva2pa((uintptr_t)super_block), 1, blockid2sectorid(0));
    superblock_t *superblock = (superblock_t *)super_block;

    int used_inode = 0;
    int used_block = 0;
    
    for(int i=0;i<superblock->inodebytemap_siz;i++){
        uint8_t* inodebytemap = (uint8_t*)bread(superblock->inodebytemap_offset + i);
        used_inode += countbyte(inodebytemap);
    }
    //计算inodebytemap中的有效数

    for(int i=0;i<superblock->blockbitmap_siz;i++){
        uint8_t* blockbitmap = (uint8_t*)bread(superblock->blockbitmap_offset + i);
        used_block += countbit(blockbitmap);
    }
    //计算blockbitmap中的有效数

    printk("\n       magic : 0x%x\n", superblock->magic);
    printk("       num sector : %d, start sector : %d\n", superblock->siz, superblock->start);
	printk("       block bitmap offset : %d (%d)\n", superblock->blockbitmap_offset,superblock->blockbitmap_siz);
	printk("       inode bitmap offset : %d (%d)\n", superblock->inodebytemap_offset,superblock->inodebytemap_siz);
	printk("       inode table offset : %d (%d/%d)\n", superblock->inodetable_offset,used_inode,superblock->inodetable_siz*(BLOCK_SIZ/sizeof(inode_t)));
	printk("       data offset : %d (%d/%d)\n", superblock->datablock_offset,used_block,superblock->siz);
	printk("       inode entry size : %dB dir entry size : %dB\n", sizeof(inode_t), sizeof(dentry_t));

    return 7;  // do_statfs succeeds
}

int do_cd(char *path)
{
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;

    char* name = path;
    if(name[0] == '/'){
        (*current_running)->pwd = 0;//为什么这里只能存ino号？因为如果后面进行修改，那么必然是应该在inodetable上进行修改
        name++;
    }

    uint32_t search_ino;
    if((search_ino = inopath2ino((*current_running)->pwd, name)) == -1){
        return 0;
    }
    else{
        if(!check_dir(ino2inode_t(search_ino))){
            printk("> [FS] Cannot cd to a file\n");
            return 0;
        }
        (*current_running)->pwd = search_ino;
    }

    char* pwd_name = path;
    if (*pwd_name == '/') {
        strcpy((*current_running)->pwd_dir, pwd_name);//绝对路径则直接拷贝
    }
    else{
        if(*pwd_name == '.'){//代表有.可能需要将工作目录退回
            while(*pwd_name == '.')
                pwd_name++;
            //退出时要么为'/'要么为'\0
            int catsign = !(*pwd_name == '\0');//如果为'\0'那么不再需要粘贴后面
            *pwd_name = '\0';//清零方便进行字符串比较
            if (!strcmp(path,"..") && strcmp((*current_running)->pwd_dir, "/")) {//此种情况需要返回上一级目录,即为..且进程路径不为根目录
                char* pwd_temp = (*current_running)->pwd_dir;
                while(*pwd_temp != '\0')//准备将最后的/给变为'\0'
                    pwd_temp++;
                while(*pwd_temp != '/')
                    pwd_temp--;

                if(pwd_temp == (*current_running)->pwd_dir){//考虑到如果顶到头了，那么必须往后走一位置为0(即已经为根目录了)
                    pwd_temp++;
                }
                *pwd_temp = '\0';
            }
            int rootsign = strcmp((*current_running)->pwd_dir, "/") && catsign;//如果不为'/'那么需要粘上'/'
            //并且前提是后面的东西也想粘贴上去
            if(rootsign)
                strcat((*current_running)->pwd_dir, "/");
            if (catsign) {
                pwd_name++;
                strcat((*current_running)->pwd_dir, pwd_name);//将两者路径拼接在一起
            }
        }
        else {
            if(strcmp((*current_running)->pwd_dir, "/"))
                strcat((*current_running)->pwd_dir, "/");
            strcat((*current_running)->pwd_dir, pwd_name);
        }
    }

    return 1;  // do_cd succeeds
}

int do_mkdir(char *dir_name){
    //create a new dir in current dir
    //NOTE: dentries are in the data block now
    //Child dir has the same name as parent dir?

    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;
    if(inopath2ino((*current_running)->pwd, dir_name) != -1)
        return 0;
    //pwd是当前的父目录，然后直接把名字送进去，会返回对应的ino号或者返回0

    unsigned new_ino = alloc_inode(INODE_DIR);
    inode_t* new_inode = ino2inode_t(new_ino);
    //初始化inode，返回ino号，并将其中所有的信息全部写好

    dentry_t dentry[2];
    memset(dentry, 0, 2*sizeof(dentry_t));
    dentry[0].dentry_valid = 1;
    dentry[0].ino = new_ino;
    strcpy((char *)(dentry[0].name), (char *)".");//name拷贝

    dentry[1].dentry_valid = 1;
    dentry[1].ino = (*current_running)->pwd;
    strcpy((char *)(dentry[1].name), (char *)"..");//name拷贝

    write_file(new_inode, (char*)dentry, new_inode->filesz, 2*sizeof(dentry_t));
    //作为一个目录项，其中的dentry号全部设置好

    inode_t* father_inode = ino2inode_t((*current_running)->pwd);
    dentry_t father_dentry;

    uint32_t fatherinode_bcacheid = ((uint8_t*)father_inode-(uint8_t*)bcaches)/BLOCK_SIZ;
    bwrite(bcaches[fatherinode_bcacheid].block_id, bcaches[fatherinode_bcacheid].bcache_block);
    //这里对父亲的inode进行了修改，因此一定要落盘

    father_dentry.dentry_valid = 1;
    father_dentry.ino = new_ino;
    strcpy((char*)(father_dentry.name), (char*)dir_name);//name拷贝

    write_file(father_inode, (char*)(&father_dentry), father_inode->filesz, sizeof(dentry_t));
    //作为一个目录项，其中的dentry号全部设置好

    return 1;
}

int do_ls(int argc, char *argv[]){
    //Update current directory
    //read_inode(&cur_dir_inode, cur_dir_inode.inode_id);
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;

    int option = 0;//default
    if((argc == 1) && (strcmp(argv[0], "-l") == 0)){
        option = 1;
    }

    dentry_t* dentry;
    inode_t* curr_inode;
    uint32_t dentry_offset = 0;

    curr_inode = ino2inode_t((*current_running)->pwd);

    if(option == 0){
        dentry = (dentry_t*)search_datapoint(curr_inode, dentry_offset);//这里的item_num针对的是目录条目数
        printk("\n");
        while(dentry->dentry_valid && (dentry_offset < curr_inode->filesz)) {//不能比filesz大
            printk("%s ",dentry->name);
            dentry_offset += sizeof(dentry_t);
            dentry = (dentry_t*)search_datapoint(curr_inode, dentry_offset);
        }
        printk("\n");
    }else if(option == 1){
        printk("INODE SIZE    LINKS  NAME\n");
        dentry = (dentry_t*)search_datapoint(curr_inode, dentry_offset);
        while(dentry->dentry_valid && (dentry_offset < curr_inode->filesz)) {
            inode_t* child_inode = ino2inode_t(dentry->ino);
            printk("\t\t%u\t\t\t%dB\t\t\t\t\t\t%u\t\t\t\t%s\n",dentry->ino, child_inode->filesz, child_inode->hardlinks, dentry->name);
            dentry_offset += sizeof(dentry_t);
            dentry = (dentry_t*)search_datapoint(curr_inode, dentry_offset);
            // screen_reflush();
        }
    }

    return 1;
}

int do_rmdirfile(char *dir_name){
    //create a new dir in current dir
    //NOTE: dentries are in the data block now
    //Child dir has the same name as parent dir?

    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;

    uint32_t rm_ino;
    inode_t* rm_inode;
    inode_t* father_inode;

    if((rm_ino = inopath2ino((*current_running)->pwd, dir_name)) == -1)
        return 0;
    //pwd是当前的父目录，然后直接把名字送进去，会返回对应的ino号或者返回0
    rm_inode = ino2inode_t(rm_ino);
    father_inode = ino2inode_t((*current_running)->pwd);

    dentry_t dentry_last;//最后一个目录项，准备拷贝到前面
    dentry_t* dentry_replace;

    dentry_replace = (dentry_t*)name_search_offset(father_inode, dir_name);//根据父目录的inode_t和name直接锁定要被替换掉的目录项的地址
    read_file(father_inode, (char *)(&dentry_last), father_inode->filesz-sizeof(dentry_t), sizeof(dentry_t));
    write_file(father_inode,(char *)(&dentry_last), (uint32_t)dentry_replace, sizeof(dentry_t));
    //将父目录的data块中的dentry项进行处理

    father_inode->filesz -= sizeof(dentry_t);
    father_inode->mtime = get_timer();
    father_inode->hardlinks--;
    uint32_t fatherinode_id = ((uint8_t*)father_inode-(uint8_t*)bcaches)/BLOCK_SIZ;
    bwrite(bcaches[fatherinode_id].block_id, bcaches[fatherinode_id].bcache_block);
    //这里对父亲的inode进行了修改，因此一定要落盘

    rmfile(rm_ino);
    //将inode项彻底删除掉

    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(1));//测试bitmap1
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(2));//测试bitmap2
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(5));//测试bytemap
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(6));//测试1
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(7));//测试2
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(8));//测试3
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(9));//测试4
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(10));//测试5
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(11));//测试6
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(12));//测试7
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(13));//测试8
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(14));//测试datablock1
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(15));//测试datablock2
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(16));//测试datablock3
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(17));//测试datablock4
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(18));//测试datablock5
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(19));//测试datablock6
    // bios_sd_read(kva2pa((uintptr_t)check_block), 8, blockid2sectorid(20));//测试datablock7

    return 1;
}

void do_getpwdname(char* pwd_name){
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;
    strcpy(pwd_name, (*current_running)->pwd_dir);
}

int do_fopen(char *path, int mode)
{
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;
    // TODO [P6-task2]: Implement do_fopen
    int fd = -1;

    int sign = 0;
    char* name = path;
    if(name[0] == '/'){
        name++;
        sign = 1;
    }
    //注意这里和cd一样的，首先要返回文件的ino号生成inode
    //如果是绝对路径，则从ino号为0开始找；如果是相对路径，则从当前工作目录开始找

    uint32_t file_ino;
    if((file_ino = inopath2ino((sign ? 0 : (*current_running)->pwd), name)) == -1){//没有这个文件
        printk("> [FS] No such file/directory \n");
        return fd;
    }

    inode_t *inode = ino2inode_t(file_ino);
    if(inode->mode == INODE_DIR){
        printk("> [FS] Cannot open a directory!\n");
        return fd;
    }//打开的不是文件是目录

    for(int i=0; i<NUM_FDESCS; i++){
        if(fdescs[i].used == 0){
            fd = i;
            break;
        }
    }

    fdescs[fd].used = 1;
    fdescs[fd].ino = file_ino;
    fdescs[fd].mode = mode;
    fdescs[fd].memsiz = 0;
    fdescs[fd].r_cursor = 0;
    fdescs[fd].w_cursor = 0;

    inode->fd_index = fd;
    uint32_t inode_id = ((uint8_t*)inode-(uint8_t*)bcaches)/BLOCK_SIZ;
    bwrite(bcaches[inode_id].block_id, bcaches[inode_id].bcache_block);
    //分配了文件的文件描述符，因此inode中必须记录一下，然后落盘

    return fd;  // return the id of file descriptor
}

int do_fread(int fd, char *buff, int length)
{
    if(fd < 0 || fd >= NUM_FDESCS || fdescs[fd].used == 0){
        printk("> [FS] invalid fd number!\n");
        return 0;
    }

    uint32_t file_ino = fdescs[fd].ino;
    inode_t * file_inode = ino2inode_t(file_ino);//得到inode
    uint32_t free_siz = file_inode->filesz - fdescs[fd].r_cursor;
    uint32_t read_siz = (free_siz > length) ? length : free_siz;//如果文件本身都已经不够大了，那么读的数据相对也会减小

    read_file(file_inode, buff, fdescs[fd].r_cursor, read_siz);//读文件
    fdescs[fd].r_cursor += read_siz;//读文件并移动读光标

    return read_siz;  // return the length of trully read data
}

int do_fwrite(int fd, char *buff, int length)
{
    if(fd < 0 || fd >= NUM_FDESCS || fdescs[fd].used == 0){
        printk("> [FS] invalid fd number!\n");
        return 0;
    }

    uint32_t file_ino = fdescs[fd].ino;
    inode_t* file_inode = ino2inode_t(file_ino);//得到inode
    uint32_t write_siz = length;//这里不用像读的时候考虑文件大小，因为是写，可以扩展文件大小

    write_file(file_inode, buff, fdescs[fd].w_cursor, write_siz);
    fdescs[fd].w_cursor += length;//写文件并移动写光标

    return write_siz;  // return the length of trully written data
}

int do_fclose(int fd)
{
    // TODO [P6-task2]: Implement do_fclose
    if(fd < 0 || fd >= NUM_FDESCS || fdescs[fd].used == 0){
        printk("> [FS] invalid fd number!\n");
        return 0;
    }

    fdescs[fd].used = 0;

    inode_t *inode = ino2inode_t(fdescs[fd].ino);
    inode->fd_index = 0xff;
    uint32_t inode_id = ((uint8_t*)inode-(uint8_t*)bcaches)/BLOCK_SIZ;
    bwrite(bcaches[inode_id].block_id, bcaches[inode_id].bcache_block);
    //关闭了文件的文件描述符，因此inode中必须改为-1，然后落盘

    return 1;  // do_fclose succeeds
}

int do_touch(char *filename)
{
    // TODO [P6-task2]: Implement do_touch
    current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;
    uint32_t len = strlen(filename);//name length
    if(len == 0 || strcheck(filename, '/') || strcheck(filename, ' ') || filename[0] == '-' || filename[0] == '.'){
        printk("> [FS] Illegal name!\n");
        return 0;
    }

    if(inopath2ino((*current_running)->pwd, filename) != -1){
        printk("> [FS] name already existed!\n");
        return 0;
    }//名字已经存在

    uint32_t new_ino = alloc_inode(INODE_FILE);//先给文件分配出一个inode
    //这个时候还没有分配文件描述符，因此不用管，仍为-1

    inode_t* father_inode = ino2inode_t((*current_running)->pwd);
    dentry_t father_dentry;

    father_dentry.dentry_valid = 1;
    father_dentry.ino = new_ino;
    strcpy((char*)(father_dentry.name), (char*)filename);//name拷贝//这里的path不会有多级目录

    write_file(father_inode, (char*)(&father_dentry), father_inode->filesz, sizeof(dentry_t));
    //对父目录的目录项进行修改

    return 1;  // do_touch succeeds
}

int do_cat(char *filename)
{
    uint32_t file_ino;

    int sign = 0;
    char* name = filename;
    if(name[0] == '/'){
        name++;
        sign = 1;
    }
    //注意这里和cd一样的，首先要返回文件的ino号生成inode
    //如果是绝对路径，则从ino号为0开始找；如果是相对路径，则从当前工作目录开始找
    if((file_ino = inopath2ino((sign ? 0 : (*current_running)->pwd), name)) == -1){
        printk("> [FS] No such file/directory \n");
        return 0;
    }

    inode_t* file_inode = ino2inode_t(file_ino);
    if(file_inode->mode == INODE_DIR){
        printk("> [FS] Cannot open a directory!\n");
        return 0;
    }

    memset(cat_buff, 0, 256);//每次用cat以前必须要保证不能有残留
    read_file(file_inode, cat_buff, 0, file_inode->filesz);//读文件//这里把所有的都打印出来，中间有空洞也不管
    printk("\n");
    printk("%s",cat_buff);

    return 10;  // do_cat succeeds
}

int do_lseek(int fd, int offset, int whence)
{
    if(fd < 0 || fd >= NUM_FDESCS || fdescs[fd].used == 0){
        printk("> [FS] invalid fd number!\n");
        return 0;
    }

    uint32_t file_ino = fdescs[fd].ino;
    inode_t* file_inode = ino2inode_t(file_ino);

    switch(whence){
        case SEEK_SET:
            fdescs[fd].r_cursor = offset;
            fdescs[fd].w_cursor = offset;
            break;
        case SEEK_CUR:
            fdescs[fd].r_cursor += offset;
            fdescs[fd].w_cursor += offset;
            break;
        case SEEK_END:
            fdescs[fd].r_cursor = file_inode->filesz + offset;
            fdescs[fd].w_cursor = file_inode->filesz + offset;
            break;
        default:
            break;
    }    

    return fdescs[fd].r_cursor;  // the resulting offset location from the beginning of the file
    //这个操作完之后，读写指针将会一样
}