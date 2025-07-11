#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fb.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <linux/input.h> 
#include <dirent.h>
#include <ctype.h>

#include <jpeglib.h>
#include <jerror.h>

// 全局变量
int lcd_fd;
int *memp;
int tsFd;

// 链表结构
struct list 
{
    char *file_path;           // 文件路径
    struct list *next;          // 下一个节点
};

// 全局链表头指针
struct list *head_ref = NULL;
struct list *current_node = NULL; // 当前显示的图片节点
char picture[1920 * 1080 * 4];     // JPEG图片缓冲区

// 函数声明
void lcd_init();
void lcd_free();
void show_black();
void lcd_show_bmp(const char *path, int x, int y, int new_width, int new_height);
void show_jpeg(const char *path, int x, int y, int new_width, int new_height);
int touch_wait(int *start_x, int *start_y, int *end_x, int *end_y);
void show_current_picture();

// 初始化链表头节点 
struct list *init_head() 
{
    struct list *head = malloc(sizeof(struct list));
    if (head == NULL) return NULL;
    head->file_path = NULL;
    head->next = NULL;
    return head;
}

// 初始化节点
struct list *init_node(const char *path) 
{
    struct list *node = malloc(sizeof(struct list));
    if (node == NULL) return NULL;
    
    node->file_path = strdup(path);  
    if (node->file_path == NULL) 
    {
        free(node);
        return NULL;
    }
    
    node->next = NULL;
    return node;
}

void insert_tail(struct list *head, struct list *node) 
{
    if (head->next == NULL) 
    {
        head->next = node;
        return;
    }
    
    struct list *p = head->next;
    while (p->next != NULL) p = p->next;
    p->next = node;
}

// 遍历链表
void display(struct list *head) 
{
    printf("\n图片文件列表:\n");
    struct list *p = head->next;
    while (p != NULL) 
    {
        printf("%s\n", p->file_path);
        p = p->next;
    }
}

// 释放链表内存
void release(struct list *head) 
{
    struct list *tmp;
    struct list *p = head->next;
    
    while (p != NULL) 
    {
        tmp = p;
        p = p->next;
        free(tmp->file_path);
        free(tmp);
    }
    
    free(head);
    printf("链表资源已释放\n");
}

// 检查文件扩展名
bool has_extension(const char *filename, const char *extension) 
{
    int len = strlen(filename);
    int ext_len = strlen(extension);
    
    if (len < ext_len) return false;
    
    const char *ext = filename + len - ext_len;
    for (int i = 0; i < ext_len; i++) {
        if (tolower(ext[i]) != tolower(extension[i])) 
            return false;
    }
    return true;
}

// 递归读取目录
int read_all_dir(const char *base_path) 
{ 
    DIR *dir = opendir(base_path);
    if (dir == NULL) return -1;
    
    struct dirent *info;
    while ((info = readdir(dir)) != NULL) 
    {
        // 跳过当前目录和上级目录
        if (strcmp(info->d_name, ".") == 0 || strcmp(info->d_name, "..") == 0) 
            continue;
        
        // 构建完整路径
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", base_path, info->d_name);
        
        // 处理目录
        if (info->d_type == DT_DIR) 
        {
            read_all_dir(path);
        }
        // 处理文件
        else if (info->d_type == DT_REG) 
        {
            // 检查文件扩展名
            if (has_extension(info->d_name, ".bmp") || 
                has_extension(info->d_name, ".jpg") || 
                has_extension(info->d_name, ".jpeg")) 
            {
                printf("找到图片文件: %s\n", path);
                
                // 创建节点并添加到链表
                struct list *node = init_node(path);
                if (node != NULL) 
                {
                    insert_tail(head_ref, node);
                }
            }
        }
    }
    
    closedir(dir);
    return 0;
}

void show_black()
{
    memset(memp, 0, 800 * 480 * 4);
}

void lcd_init()
{
    lcd_fd = open("/dev/fb0", O_RDWR);
    if(lcd_fd == -1)
    {
        perror("open lcd error");
        exit(1);
    }
    memp = mmap(NULL, 800 * 480 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, lcd_fd, 0);
    if(memp == MAP_FAILED)
    {
        perror("mmap error");
        exit(1);
    }
    tsFd = open("/dev/input/event0", O_RDWR);
    if(tsFd == -1)
    {
        perror("open touch error");
        exit(1);
    }
}

void lcd_free()
{
    munmap(memp, 800 * 480 * 4);
    close(lcd_fd);
    close(tsFd);
}

void lcd_show_bmp(const char *path, int x, int y, int new_width, int new_height)
{
    char buf[3] = {};
    int offset = 0, width = 0, height = 0;
    short bpp = 0;  

    int pic_fd = open(path, O_RDWR);
    if(pic_fd < 0)
    {
        perror("open");
        return;
    }

    // 读取位图文件参数
    lseek(pic_fd, 10, SEEK_SET);
    read(pic_fd, &offset, 4);
    lseek(pic_fd, 18, SEEK_SET);
    read(pic_fd, &width, 4);
    read(pic_fd, &height, 4);
    lseek(pic_fd, 28, SEEK_SET);
    read(pic_fd, &bpp, 2);

    // 计算需要跳过的字节数
    int skip = (4 - (width * bpp / 8) % 4) % 4;
    char *pic_buffer = malloc(width * height * bpp / 8 + skip * height);
    if (!pic_buffer) {
        perror("malloc");
        close(pic_fd);
        return;
    }
    
    lseek(pic_fd, offset, SEEK_SET);
    read(pic_fd, pic_buffer, width * height * bpp / 8 + skip * height);

    // 缩放显示
    for (int i = 0; i < new_height; i++) {
        for (int j = 0; j < new_width; j++) {
            int i0 = i * height / new_height;
            int j0 = j * width / new_width;
            int idx = (height - 1 - i0) * (width * 3 + skip) + j0 * 3;
            
            int color = pic_buffer[idx] |
                        pic_buffer[idx + 1] << 8 |
                        pic_buffer[idx + 2] << 16;
            
            *(memp + 800 * (i + y) + j + x) = color;
        }
    }
    
    free(pic_buffer);
    close(pic_fd);
}

// 解析jpeg图片
void show_jpeg(const char *path, int x, int y, int new_width, int new_height)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    // 分配内存存储解码数据
    unsigned char *buffer = malloc(cinfo.output_width * cinfo.output_height * cinfo.output_components);
    if (!buffer) {
        fclose(fp);
        return;
    }
    
    unsigned char *p = buffer;
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row_pointer[1] = {p};
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
        p += cinfo.output_width * cinfo.output_components;
    }

    // 缩放显示
    for (int i = 0; i < new_height; i++) {
        for (int j = 0; j < new_width; j++) {
            int i0 = i * cinfo.output_height / new_height;
            int j0 = j * cinfo.output_width / new_width;
            int idx = (i0 * cinfo.output_width + j0) * 3;
            
            int color = buffer[idx] << 16 |
                        buffer[idx + 1] << 8 |
                        buffer[idx + 2];
            
            *(memp + 800 * (i + y) + j + x) = color;
        }
    }

    free(buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
}

// 显示当前图片
void show_current_picture()
{
    if (!current_node || !current_node->file_path) return;
    
    show_black();
    
    const char *path = current_node->file_path;
    const char *ext = strrchr(path, '.');
    
    if (ext && (strcasecmp(ext, ".bmp") == 0)) 
    {
        lcd_show_bmp(path, 0, 0, 800, 480);
    }
    else if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) 
    {
        show_jpeg(path, 0, 0, 800, 480);
    }
}

// 删除当前图片节点
void delete_current_node()
{
    if (!head_ref || !current_node) return;
    
    struct list *prev = head_ref;
    struct list *curr = head_ref->next;
    
    // 查找前一个节点
    while (curr && curr != current_node) 
    {
        prev = curr;
        curr = curr->next;
    }
    
    if (!curr) return; // 当前节点不在链表中
    
    // 从链表中移除当前节点
    prev->next = current_node->next;
    
    // 更新当前节点
    struct list *to_delete = current_node;
    
    if (current_node->next) {
        current_node = current_node->next;
    } else if (prev != head_ref) {
        current_node = prev;
    } else {
        current_node = NULL; // 链表为空
    }
    
    // 释放资源
    free(to_delete->file_path);
    free(to_delete);
}

int touch_wait(int *start_x, int *start_y, int *end_x, int *end_y)
{
    bool pressed = false;
    int cur_x = 0, cur_y = 0;
    
    while(1)
    {
        struct input_event info;
        ssize_t bytes = read(tsFd, &info, sizeof(info));
        if(bytes <= 0) continue;
        
        if(info.type == EV_ABS)
        {
            if(info.code == ABS_X)
            {
                cur_x = info.value;
            }
            else if(info.code == ABS_Y)
            {
                cur_y = info.value;
            }
        }
        
        if(info.type == EV_KEY && info.code == BTN_TOUCH)
        {
            if(info.value == 1)  
            {
                pressed = true;
                *start_x = (cur_x * 800) / 1024;
                *start_y = (cur_y * 480) / 600;
            }
            else if(info.value == 0)  
            {
                *end_x = (cur_x * 800) / 1024;
                *end_y = (cur_y * 480) / 600;
                break;
            }
        }
    }
    
    return 0;
}

int main(int argc, char*argv[]) 
{
    if (argc !=2)
    {
        printf("请输入参数\n");
        return -1;
    }
    // 初始化LCD和触摸屏
    lcd_init();
    
    // 初始化链表头
    head_ref = init_head();
    if (head_ref == NULL) 
    {
        printf("链表初始化失败\n");
        lcd_free();
        return -1;
    }
    
    // 扫描目录获取图片文件
    read_all_dir(argv[1]);
    
    // 设置当前节点为第一个图片
    if (head_ref->next) 
    {
        current_node = head_ref->next;
    } else {
        printf("未找到图片文件\n");
        release(head_ref);
        lcd_free();
        return 0;
    }
    
    // 显示当前图片
    show_current_picture();
    
    int start_x, start_y, end_x, end_y;
    
    // 主循环
    while (1) 
    {   
        if (touch_wait(&start_x, &start_y, &end_x, &end_y) == -1)
            continue;
        
        int delta_x = end_x - start_x;
        int delta_y = end_y - start_y;
        
        // 上划删除（Y轴向上滑动>100像素）
        if (delta_y < 0 && -delta_y >= 100) 
        {
            printf("上划删除图片\n");
            delete_current_node();
            
            // 检查是否还有图片
            if (!current_node) 
            {
                printf("所有图片已删除\n");
                break;
            }
            
            show_current_picture();
        } 
        // 左滑上一张（X轴向左滑动>150像素）
        else if (delta_x < 0 && -delta_x >= 150) 
        {
            printf("左滑上一张\n");
            // 查找当前节点的前一个节点
            struct list *prev = head_ref;
            struct list *curr = head_ref->next;
            
            while (curr && curr != current_node) 
            {
                prev = curr;
                curr = curr->next;
            }
            
            if (prev != head_ref && prev != current_node) 
            {
                current_node = prev;
                show_current_picture();
            }
        }
        // 右滑下一张（X轴向右滑动>150像素）
        else if (delta_x > 0 && delta_x >= 150) 
        {
            printf("右滑下一张\n");
            if (current_node->next) 
            {
                current_node = current_node->next;
                show_current_picture();
            }
        }
    }
    
    // 释放资源
    if (head_ref) 
    {
        release(head_ref);
    }
    lcd_free();
    
    return 0;
}