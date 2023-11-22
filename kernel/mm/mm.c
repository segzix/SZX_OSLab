#include <pgtable.h>
#include <os/mm.h>
#include <os/string.h>
#include <os/sched.h>
#include <os/kernel.h>
// #include <stdint.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
page_allocated page_general[PAGE_NUM];
// unsigned page_head = 0;

//用户给出一个虚地址和根目录页，将映射全部建立好(如果已经有映射则不用建立)，最终返回末级页表的地址
//所有的对页表的分配都是在这个函数里进行(被封装起来)，并且页表一定会被pin住
//注意页表的分配，其中的va并不像分配物理页中的va具有任何意义
PTE * search_and_set_PTE(uintptr_t va, uintptr_t pgdir,int pid)
{
    va &= VA_MASK;
    PTE * pgdir_t = (PTE *)pgdir;
    uint64_t vpn2 = (va   >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS));//页目录虚地址
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^
                    (va   >> (NORMAL_PAGE_SHIFT + PPN_BITS));//二级页表虚地址
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^
                    (vpn1 << (PPN_BITS)) ^
                    (va   >> (NORMAL_PAGE_SHIFT));//三级页表虚地址
    
    if(pgdir_t[vpn2] % 2 == 0){//页表对应的p位必然是1，除非没有被分配过
        // printl("IN ! %d\n",pgdir_t[vpn2]);
        // alloc second - level page
        pgdir_t[vpn2] = 0;
        set_pfn(&pgdir_t[vpn2], kva2pa(allocPage(1,1,va,1,pid)) >> NORMAL_PAGE_SHIFT);//allocpage作为内核中的函数是虚地址，此时为二级页表分配了空间
        set_attribute(&pgdir_t[vpn2],_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        clear_pgdir(pa2kva(get_pa(pgdir_t[vpn2])));//事实上就是将刚刚allocpage的页清空
    }

    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir_t[vpn2]));
    
    if(pmd[vpn1] % 2 == 0){//然后对二级页表的虚地址进行操作//可能会出现前面几级页表一样，最后一级不一样
        // alloc third - level page
        pmd[vpn1] = 0;
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1,1,va,1,pid)) >> NORMAL_PAGE_SHIFT);//这里分配出去的时页表页，并且一定会被pin住
        set_attribute(&pmd[vpn1],_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }

    PTE *pmd2 = (PTE *)pa2kva(get_pa(pmd[vpn1]));  

    return (pmd2 + vpn0);//直接返回了对应的页表项地址
}

PTE * search_PTE(uintptr_t va, uintptr_t pgdir)//这个函数只会单纯的去找表项，不会在没有找到的时候分配页表
{
    va &= VA_MASK;
    PTE * pgdir_t = (PTE *)pgdir;
    uint64_t vpn2 = (va   >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS));//页目录虚地址
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^
                    (va   >> (NORMAL_PAGE_SHIFT + PPN_BITS));//二级页表虚地址
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^
                    (vpn1 << (PPN_BITS)) ^
                    (va   >> (NORMAL_PAGE_SHIFT));//三级页表虚地址
    
    if(pgdir_t[vpn2] % 2 == 0){
        while(1){
            printk("searchPTE error!");
        }
    }

    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir_t[vpn2]));
    
    if(pmd[vpn1] % 2 == 0){//然后对二级页表的虚地址进行操作//可能会出现前面几级页表一样，最后一级不一样
        while(1){
            printk("searchPTE error!");
        }
    }

    PTE *pmd2 = (PTE *)pa2kva(get_pa(pmd[vpn1]));  

    return (pmd2 + vpn0);//直接返回了对应的页表项地址
}

void clear_pagearray(uint32_t node_index){
    page_general[node_index].valid = 0;
    page_general[node_index].pin = 0;
    //page_general[node_index].using
    //page_general[node_index].kva

    page_general[node_index].pid = -1;
    page_general[node_index].pgdir = -1;
    page_general[node_index].va  = -1;
    page_general[node_index].table_not  = 0;
}

unsigned swap_out(){//swapout函数只负责选中一页然后换出，不负责将某一页重新填入或者分配出去

    static swap_block_id = 0x200;
    PTE * swap_PTE;//将要换出去的物理页对应的表项
    for(unsigned i = 0;i < PAGE_NUM;i++)
    {
        if(!page_general[i].pin && !page_general[i].table_not){
            page_general[i].valid = 0;

            // page_general[i].pin   = pin;
            // page_general[i].using = 1;

            // page_general[i].pid   = (*current_running)->pid;
            // page_general[i].pgdir = (*current_running)->pgdir;
            // page_general[i].va    = va;
            swap_PTE = search_and_set_PTE(page_general[i].va, page_general[i].pgdir,0);
            *swap_PTE = 0;

            set_pfn(swap_PTE, swap_block_id);//将硬盘上的扇区号写入ppn
            set_attribute(swap_PTE, _PAGE_SOFT | _PAGE_USER);//软件位拉高
            local_flush_tlb_all();

            bios_sd_write(kva2pa(page_general[i].kva),8,swap_block_id);//将物理地址和所写的扇区号传入
            swap_block_id += 8;//扇区号加8

            return i;//返回数组中的下标
        }
    }
    // while(am_siz > 0 && am_pool[am_head].valid == 0){
    //     am_head ++;
    //     am_siz--;
    // }
    // if(am_siz == 0){
    //     swap_error();
    //     return;
    // }
    // for(int i=0; i<4096; i++){
    //     if(sw_pool[i].valid == 0){
    //         if(i > sw_top)
    //             sw_top = i;
    //         uint64_t pa = am_pool[am_head].pa;
    //         sw_pool[i].valid = 1;
    //         sw_pool[i].pid   = (*current_running)->parent_id ? (*current_running)->parent_id : (*current_running)->pid;
    //         sw_pool[i].va    = am_pool[am_head].va;
    //         sw_pool[i].pmd3  = am_pool[am_head].pmd3;
    //         am_head++;
    //         if(am_head == 4096)
    //             am_head = 0;
    //         am_siz--;
    //         *(sw_pool[i].pmd3) = (PTE) 0; 
    //         local_flush_tlb_all();
    //         uint64_t bias = padding_ADDR/512;
    //         bios_sdwrite(pa,8,bias+8*i); 
    //         printl("swap successful ! %ld\n",sw_pool[i].va);
    //         return pa2kva(pa);          
    //     }
    // }    
    // swap_error();
    return 0;
}

ptr_t allocPage(int numPage,int pin,uintptr_t va,int table_not,int pid)//返回的是物理地址对应的内核虚地址
{
    uintptr_t ret = 0;

    for(int j = 0;j < numPage;j++)
    {
        for(unsigned i = 0;i < PAGE_NUM;i++)
        {
            if(page_general[i].valid == 0){
                page_general[i].valid = 1;

                page_general[i].pin   = pin;
                page_general[i].using = 1;

                page_general[i].pid   = pid;
                page_general[i].pgdir = pcb[pid-2].pgdir;//这里找寻的还是pcb数组
                page_general[i].va    = va;
                page_general[i].table_not    = table_not;

                clear_pgdir(page_general[i].kva);
                if(j == 0)
                    ret = page_general[i].kva;//kva初始化的时候已经被设置好了
                break;
            }
        }
        if(ret != 0)//如果前面已经被分配出，则直接结束该循环
            continue;
        unsigned index= swap_out();//如果没有空余的，则在换出之后返回一个被换出的数组下标//这里只用单纯的去找能换出的页表项即可

        page_general[index].valid = 1;

        page_general[index].pin   = pin;
        page_general[index].using = 1;

        page_general[index].pid   = pid;
        page_general[index].pgdir = pcb[pid-2].pgdir;//这里找寻的还是pcb数组
        page_general[index].va    = va;
        page_general[index].table_not    = table_not;
        ret = page_general[index].kva;
    }

    return ret;
    // ptr_t ret;
    // if(fm_head >= 0){
    //     int temp = fm_head;
    //     ret = fm_head * PAGE_SIZE + FREEMEM_KERNEL;
    //     fm_head = fm_pool[fm_head].nxt;
    //     fm_pool[temp].nxt = -1;
    //     fm_pool[temp].valid = 0;
    //     // printl("ret node %ld\n",ret);
    //     return ret;
    // }
    
    // ret = ROUND(kernMemCurr, PAGE_SIZE);
    // if(ret + numPage * PAGE_SIZE <= FREEMEM_END){
    //     kernMemCurr = ret + numPage * PAGE_SIZE;
    //     return ret;        
    // }
    // ret = swap_page();
    // return ret;
    // // align PAGE_SIZE
    // ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    // kernMemCurr = ret + numPage * PAGE_SIZE;
    // return ret;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void free_all_pagemapping(ptr_t baseAddr){//通过根目录页基址，取消所有映射
    PTE *pgdir = (PTE *)baseAddr;
    PTE *pmd, *pmd2, *pmd3;
    // int last = fm_head;
    for(int i=0; i<512; i++){
        if(pgdir[i] % 2){
            pmd = (PTE *)pa2kva(get_pa(pgdir[i]));
            
            if(pmd < FREEMEM_KERNEL) continue; //kernal no release
            // printl("pmd %ld \n",pmd);

            for(int j = 0; j<512; j++){
                if(pmd[j] % 2){
                    pmd2 = (PTE *)pa2kva(get_pa(pmd[j]));
                    for(int k = 0; k<512; ++k){
                        if(pmd2[k] % 2){
                            pmd3 = (PTE *)pa2kva(get_pa(pmd2[k]));
                            uint32_t node_index = ((uint64_t)pmd3 - FREEMEM_KERNEL)/PAGE_SIZE;//由此得知数组中的对应页的下标

                            set_attribute(pmd2, get_attribute(*pmd2,PA_ATTRIBUTE_MASK) & ~_PAGE_PRESENT);
                            //取消映射

                            //取消映射不代表取消物理页，如果using为0则取消物理页
                            page_general[node_index].using--;
                            if(page_general[node_index].using == 0)
                                clear_pagearray(node_index);
                        }
                    }
                }
            }
        }
    }
}

void free_all_pagetable(ptr_t baseAddr){//取消并回收所有的页表
    PTE *pgdir = (PTE *)baseAddr;
    PTE *pmd, *pmd2;
    // int last = fm_head;
    for(int i=0; i<512; i++){
        if(pgdir[i] % 2){//如果该位拉高则证明对应的页表有映射
            pmd = (PTE *)pa2kva(get_pa(pgdir[i]));
            
            if(pmd < FREEMEM_KERNEL) continue; //kernal no release
            // printl("pmd %ld \n",pmd);

            for(int j = 0; j<512; j++){
                if(pmd[j] % 2){//如果该位拉高则证明对应的页表有映射
                    pmd2 = (PTE *)pa2kva(get_pa(pmd[j]));
                    uint32_t node_index = ((uint64_t)pmd2 - FREEMEM_KERNEL)/PAGE_SIZE;//由此得知数组中的对应页的下标

                    clear_pagearray(node_index);
                    page_general[node_index].using = 0;
                }
            }

            uint32_t node_index = ((uint64_t)pmd - FREEMEM_KERNEL)/PAGE_SIZE;//由此得知数组中的对应页的下标

            clear_pagearray(node_index);
            page_general[node_index].using = 0;
        }
    }

    uint32_t node_index = ((uint64_t)pgdir - FREEMEM_KERNEL)/PAGE_SIZE;//由此得知数组中的对应页的下标

    clear_pagearray(node_index);
    page_general[node_index].using = 0;

    local_flush_tlb_all();
}

// typedef struct page_allocated{
//     int valid;//是否被分配，已经被分配则置为1
    
//     int pin;//如果置成1，则该页不允许被换出
//     int using;//如果是共享内存，该变量记录有多少个进程正在共享

//     uintptr_t kva;//对应的内核虚地址

//     int pid;//对应的进程号
//     uintptr_t pgdir;//对应进程的根目录页
//     uintptr_t va;//对应进程对该物理地址的虚地址
// }page_allocated;
void free_all_pageframe(ptr_t baseAddr){//取消并回收所有的物理页
    PTE *pgdir = (PTE *)baseAddr;
    PTE *pmd, *pmd2, *pmd3;
    // int last = fm_head;
    for(int i=0; i<512; i++){
        if(pgdir[i] % 2){
            pmd = (PTE *)pa2kva(get_pa(pgdir[i]));
            
            if(pmd < FREEMEM_KERNEL) continue; //kernal no release
            // printl("pmd %ld \n",pmd);

            for(int j = 0; j<512; j++){
                if(pmd[j] % 2){
                    pmd2 = (PTE *)pa2kva(get_pa(pmd[j]));
                    for(int k = 0; k<512; ++k){
                        if(pmd2[k] % 2){
                            pmd3 = (PTE *)pa2kva(get_pa(pmd2[k]));
                            uint32_t node_index = ((uint64_t)pmd3 - FREEMEM_KERNEL)/PAGE_SIZE;//由此得知数组中的对应页的下标

                            page_general[node_index].valid = 0;
                            page_general[node_index].pin = 0;

                            page_general[node_index].pid = -1;
                            page_general[node_index].pgdir = -1;
                            page_general[node_index].va  = -1;

                            //取消映射
                        }
                    }
                }
            }
        }
    }
}
// void freePage(ptr_t baseAddr)
// {
//     PTE *pgdir = (PTE *)baseAddr;
//     PTE *pmd, *pmd2, *pmd3;
//     int last = fm_head;
//     current_running = get_current_cpu_id() ? &current_running_1 : &current_running_0;
//     for(int i=0; i<512; i++){
//         if(pgdir[i] % 2){
//             pmd = (PTE *)pa2kva(get_pa(pgdir[i]));
            
//             if(pmd < FREEMEM_KERNEL) continue; //kernal no release
//             // printl("pmd %ld \n",pmd);

//             for(int j = 0; j<512; ++j){
//                 if(pmd[j] % 2){
//                     pmd2 = (PTE *)pa2kva(get_pa(pmd[j]));
//                     for(int k = 0; k<512; ++k){
//                         if(pmd2[k] % 2){
//                             pmd3 = (PTE *)pa2kva(get_pa(pmd2[k]));
//                             uint32_t node_index = ((uint64_t)pmd3 - FREEMEM_KERNEL)/PAGE_SIZE;
//                             if(fm_pool[node_index].valid)continue;
//                             fm_pool[node_index].valid = 1;
//                             fm_pool[node_index].nxt = -1;
//                             if(fm_head == -1){
//                                 fm_head = node_index;
//                                 last = fm_head;
//                             }
//                             else{
//                                 for(;fm_pool[last].nxt >= 0; last = fm_pool[last].nxt);
//                                 fm_pool[last].nxt = node_index;
//                             }
//                         }
//                     }
//                     uint32_t node_index = ((uint64_t)pmd2 - FREEMEM_KERNEL)/PAGE_SIZE;
//                     if(fm_pool[node_index].valid)continue;
//                     fm_pool[node_index].valid = 1;
//                     fm_pool[node_index].nxt = -1;
//                     if(fm_head == -1){
//                         fm_head = node_index;
//                         last = fm_head;
//                     }
//                     else{
//                         for(;fm_pool[last].nxt >= 0;last = fm_pool[last].nxt);
//                         fm_pool[last].nxt = node_index;
//                     }
//                 }
//             }
//             uint32_t node_index = ((uint64_t)pmd - FREEMEM_KERNEL)/PAGE_SIZE;
//             if(fm_pool[node_index].valid)continue;
//             fm_pool[node_index].valid = 1;
//             fm_pool[node_index].nxt = -1;
//             if(fm_head == -1){
//                 fm_head = node_index;
//                 last = fm_head;
//             }
//             else{
//                 for(;fm_pool[last].nxt >= 0;last = fm_pool[last].nxt);
//                 fm_pool[last].nxt = node_index;
//             }
//         }
//     }
//     uint32_t node_index = ((uint64_t)pgdir - FREEMEM_KERNEL)/PAGE_SIZE;
//     if(fm_pool[node_index].valid == 0){
//         fm_pool[node_index].valid = 1;
//         fm_pool[node_index].nxt = -1;
//         if(fm_head == -1){
//             fm_head = node_index;
//             last = fm_head;
//         }
//         else{
//             for(;fm_pool[last].nxt >= 0;last = fm_pool[last].nxt);
//             fm_pool[last].nxt=node_index;
//         }        
//     }

//     for(int i=0; i <= sw_top; i++){
//         if(sw_pool[i].pid == (*current_running)->pid)
//             sw_pool[i].valid = 0; 

//     }
//     while(sw_top >=0&&sw_pool[sw_top].valid ==0)
//         sw_top--;
//     for(int i=0; i < 4096; i++){
//         if(am_pool[i].pid == (*current_running)->pid)
//             am_pool[i].valid = 0;
//     }
//     // TODO [P4-task1] (design you 'freePage' here if you need):
// }

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    uint64_t *dest_pgdir_addr = (uint64_t *) dest_pgdir;
    uint64_t *src_pgdir_addr  = (uint64_t *) src_pgdir;
    for(int i = 0; i < 512; i++){
        if(i != (TEMP_PAGE_START >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS)))//如果满足不是临时映射则拷贝
            *dest_pgdir_addr = *src_pgdir_addr;
        dest_pgdir_addr++;
        src_pgdir_addr++;
    }
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
//这是专门为用户建立页表，用户进行映射准备的函数，其他的比如建立内核栈，建立映射页表都是用户不可见，因此不会使用该函数
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir, int pin,int pid)//用户给出一个虚地址以及页目录地址，返回给出一个内核分配的物理页的虚地址映射
//helper的作用在于由他分配出去的肯定不是页表，或者说除了例外处理的时候不用他多分配，其他时候都用它帮忙进行分配(要求分配的东西不是内核的东西)
//注意这个函数的精髓之处在于因为是用户，因此要建立映射，而对于内核栈和页表而言，不需要重新建立映射，因此不会调用此函数
{
    PTE * set_PTE;  

    set_PTE = search_and_set_PTE(va, pgdir,pid);//这里不仅会进行寻找表项，如果没有还会将页表全部建立起来

    *set_PTE = 0;
    uint64_t pa = kva2pa(allocPage(1,pin,va,0,pid));//为给页表项分配出一个物理页，并且根据传参确定是否pin住
    
    set_pfn(set_PTE,(pa >> NORMAL_PAGE_SHIFT));
    set_attribute(set_PTE,_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC
                             | _PAGE_ACCESSED| _PAGE_DIRTY| _PAGE_USER);
    // printl("vpn2 %d %d vpn1 %d %d vpn0 %d %d pa %d | %d %d %d\n",vpn2,&pgdir_t[vpn2],vpn1,&pmd[vpn1],vpn0,&pmd2[vpn0],pa,pgdir_t[vpn2],pmd[vpn1],pmd2[vpn0]);                         
    
    // if(swap && am_siz < 4096){
    //     am_pool[am_tail].pid  = (*current_running)->parent_id ? (*current_running)->parent_id : (*current_running)->pid;
    //     am_pool[am_tail].pa   = pa;
    //     am_pool[am_tail].pmd3 = &pmd2[vpn0];
    //     am_pool[am_tail].va   = (va >> 12) << 12;
    //     am_pool[am_tail].valid=1;
    //     am_tail++;
    //     if(am_tail == 4096)
    //         am_tail = 0;
    //     am_siz++;
    //     printl("get valid swap page ! %d %ld\n",va,pmd2[vpn0]);
    // }
    local_flush_tlb_all();
    return pa2kva(pa);
    // TODO [P4-task1] alloc_page_helper:
}

uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
}