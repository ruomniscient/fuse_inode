#include "fs.h"

// meta -> inode_bitmap -> inode -> disk_bitmap -> disk

void fs_create(int disk_blocks, int total_inodes){
    // adjusting total disk blocks to fit in the allocated bitmap
    int disk_bitmap_size = bitmap_get_blocks_count(disk_blocks);
    disk_blocks = bitmap_bits_from_blocks(disk_bitmap_size);

    int inode_blocks = inode_get_size(total_inodes);
    int inode_bitmap_size = bitmap_get_blocks_count(total_inodes);
    meta *meta_i = (meta *)malloc(sizeof(meta));
    meta_i->magic_number = FS_MAGIC;
    meta_i->blocks_count = 1 + inode_blocks + disk_bitmap_size + inode_bitmap_size + disk_blocks;
    meta_i->inode_bitmap_fist_block = 1;
    meta_i->inode_bitmap_last_block = meta_i->inode_bitmap_fist_block + inode_bitmap_size - 1;
    meta_i->inode_first_block = meta_i->inode_bitmap_last_block + 1;
    meta_i->inode_last_block = meta_i->inode_first_block + inode_blocks - 1;
    meta_i->disk_bitmap_first_block = meta_i->inode_last_block + 1;
    meta_i->disk_bitmap_last_block = meta_i->disk_bitmap_first_block + disk_bitmap_size - 1;
    meta_i->disk_first_block = meta_i->disk_bitmap_last_block + 1;
    meta_i->disk_bitmap_last_block = meta_i->disk_bitmap_last_block + disk_blocks - 1;
    meta_i->root_inode = 0;
    meta_i->used_inodes = 0;

    char tmp[BLOCK_SIZE];
    memset(tmp, 0, BLOCK_SIZE);

    int i = 0;
    for (; i < meta_i->blocks_count; i++){
        device_write_block(i, tmp);
    }

    // root inode
    inode_init(meta_i->inode_first_block, meta_i->inode_last_block);
    bitmap_instance *inode_bitmap = bitmap_init(meta_i->inode_bitmap_fist_block, meta_i->inode_bitmap_last_block);
    inode_t *node = inode_make(inode_bitmap);
    node->type = INODE_DIRECTORY;
    node->tmp = 123;
    inode_save(node);

    meta_i->root_inode = node->id;
    meta_i->used_inodes++;
    device_write_block_ofs(0, (char *)meta_i, 0, sizeof(meta));

    inode_free(node);
    bitmap_free(inode_bitmap);
    free(meta_i);
}

fs_info *fs_open(){
    meta *meta_i = (meta *)malloc(sizeof(meta));
    device_read_block_ofs(0, (char *)meta_i, 0, sizeof(meta));
    if (meta_i->magic_number != FS_MAGIC){
        free(meta_i);
        return NULL;
    }

    bitmap_instance *inode_bitmap = bitmap_init(meta_i->inode_bitmap_fist_block, meta_i->inode_bitmap_last_block);
    bitmap_instance *disk_bitmap = bitmap_init(meta_i->disk_bitmap_first_block, meta_i->disk_bitmap_last_block);

    inode_init(meta_i->inode_first_block, meta_i->inode_last_block);

    fs_info *fs = (fs_info *)malloc(sizeof(fs_info));
    fs->disk_bitmap = disk_bitmap;
    fs->inode_bitmap = inode_bitmap;
    fs->meta = meta_i;

    inode_t *root_inode = inode_find(meta_i->root_inode);
    if (root_inode == NULL){
        free(meta_i);
        free(fs);
        bitmap_free(disk_bitmap);
        bitmap_free(inode_bitmap);
        return NULL;
    }
    fs->root_inode = fs_open_inode(fs, root_inode);
    return fs;
}

void fs_flush(fs_info *info){
    device_write_block_ofs(0, (char *)(info->meta), 0, sizeof(meta));
    bitmap_flush(info->disk_bitmap);
    bitmap_flush(info->inode_bitmap);
    inode_flush_data(info->root_inode);
}

opened_file *fs_open_inode(fs_info *info, inode_t *inode){
    opened_file *handle = (opened_file *)malloc(sizeof(opened_file));
    handle->flushed = 1;
    handle->inode = inode;
    handle->inode_bitmap = info->inode_bitmap;
    handle->disk_bitmap = info->disk_bitmap;
    handle->meta = info->meta;
    int i = 0;
    for (; i < CACHED_COUNT; i++){
        handle->cached_block_ids[i] = -1;
    }

    return handle;
}

opened_file *fs_create_file(fs_info *info){
    inode_t *inode = inode_make(info->inode_bitmap);
    if (inode == NULL){
        return NULL;
    }

    opened_file *handle = (opened_file *)malloc(sizeof(opened_file));
    handle->flushed = 0;
    handle->inode = inode;
    handle->inode_bitmap = info->inode_bitmap;
    handle->disk_bitmap = info->disk_bitmap;
    handle->meta = info->meta;
    int i = 0;
    for (; i < CACHED_COUNT; i++){
        handle->cached_block_ids[i] = -1;
    }

    inode_save(handle->inode);

    return handle;
}

void fs_close_file(opened_file *opened){
    inode_flush_data(opened);
    free(opened);
}

int fs_dir_add_file(opened_file *opened, char *name, int inode_n){
    if (opened->inode->type != INODE_DIRECTORY){
        return 0;
    }

    int len = strlen(name), struct_len = len + sizeof(int) + sizeof(char);
    char *add = (char *)malloc(struct_len), *pos = add;
    *((int *)pos) = inode_n; pos += sizeof(int);
    *pos++ = (char)len;
    memcpy(pos, name, len);
    fs_io(opened, opened->inode->size, struct_len, add, FS_IO_WRITE);
    free(add);
    return 1;
}

linked_file_list *fs_readdir(opened_file *opened){
    if (opened->inode->type != INODE_DIRECTORY){
        return NULL;
    }

    char *data = (char *)malloc(sizeof(char) * opened->inode->size + 256 + sizeof(block_n));
    fs_io(opened, 0, opened->inode->size, data, FS_IO_READ);

    int blid;
    char len;
    linked_file_list *list = NULL;

    char *pos = data, *last = data + opened->inode->size;
    while ( pos < last){
        len = *(pos++);
        blid = *pos; pos += sizeof(blid);

        linked_file_list *item = (linked_file_list *)malloc(sizeof(linked_file_list));
        item->inode_n = blid;
        item->name = (char *)malloc(len + 1);
        memcpy(item->name, pos, len);
        *(item->name + len) = 0;

        item->next = list;
        list = item;
        pos += len;
    }

    free(data);
    return list;
}

int fs_find_file(opened_file *directory, char *file){
    linked_file_list *list = fs_readdir(directory), *curr = list;
    int result = -1;
    while (curr != NULL){
        if (stricmp(curr->name, file) == 0){
            result = curr->inode_n;
            break;
        }
        curr = curr->next;
    }
    fs_free_readdir(list);
    return result;
}

int fs_find_inode(fs_info *info, char *path){
    if (*path == '/'){
        path++;
    }

    int found_inode = -1, last_inode = -1;
    opened_file *curr = info->root_inode;
    char buf[FS_MAX_FILE_NAME + 1];

    while(1){
        int len = str_take_till(path, buf, '/', FS_MAX_FILE_NAME);
        if (len <= 0){
            found_inode = last_inode;
            break;
        }
        path += len + 1;
        buf[len] = 0;

        int inode = fs_find_file(curr, buf);
        if (inode < 0){
            break;
        }

        if (curr != info->root_inode){
            fs_close_file(curr);
        }

        curr = fs_open_inode(info, inode);
        last_inode = inode;
    }

    return found_inode;
}

int fs_io(opened_file *opened, size_t offset, size_t count, char *buf, int dir){
    int tmp_ofs = offset % BLOCK_SIZE, bl_ofs = 0;

    int pos = offset, maxpos = offset + count, processed = 0;
    if (maxpos > opened->inode->size && dir == FS_IO_READ){
        maxpos = opened->inode->size;
    }

    for(; pos < maxpos; pos++)
    {
        int seq_block = pos / BLOCK_SIZE;
        if (seq_block >= opened->inode->blocks_count && dir == FS_IO_READ){
            break; // no reason to continue
        }

        int real_block = fs_get_disk_block_id(opened, seq_block);
        if (real_block < 0){
            return -1; // error
        }

        int from = pos % BLOCK_SIZE,
            count = BLOCK_SIZE;
        if (count + from > BLOCK_SIZE) count = BLOCK_SIZE - from;
        if (count + pos > maxpos) count = maxpos - pos;

        if (dir == FS_IO_WRITE){
            device_write_block_ofs(
               real_block + opened->meta->disk_first_block, buf + (pos - offset),
               from, count
            );
        }
        else if (dir == FS_IO_READ){
            device_read_block_ofs(
               real_block + opened->meta->disk_first_block, buf + (pos - offset),
               from, count
            );
        }

        pos += count;
        processed += count;

        if(pos > opened->inode->size){
            opened->inode->size = pos;
            opened->flushed = 0;
        }
    }

    if (!opened->flushed){
        inode_flush_data(opened);
    }

    return processed;
}

void fs_free_readdir(linked_file_list *list){
    linked_file_list *curr = list, *tmp;
    while(curr != NULL){
        tmp = curr;
        curr = curr->next;
        free(tmp->name);
        free(tmp);
    }
}
