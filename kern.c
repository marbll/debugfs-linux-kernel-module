#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/filter.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/mutex.h>

#include "common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Margarita Blinova");
MODULE_DESCRIPTION("A Linux kernel module for printing kernel structures (vfsmount, pt_regs) through debugfs");
MODULE_VERSION("0.01");

#define BUFFER_SIZE 256

// Сообщение для вывода полей структуры task_cputime_atomic
#define VFSMOUNT_MESSAGE 	"mnt_flags = %d\n" \
				"mnt_sb -> s_blocksize = %u\n" \
				"mnt_sb -> s_count = %u\n"

#define PTREGS_MESSAGE 		"General registers: \n" \
				"rax = %ld\n" \
				"rdi = %ld\n" \
				"rsi = %ld\n" \
				"rcx = %ld\n" \
				"rdx = %ld\n" \
				"rbx = %ld\n" \
				"rbp = %ld\n" \
				"rsp = %ld\n" \
				"r10 = %ld\n" \
				"r11 = %ld\n" \
				"r12 = %ld\n" \
				"r13 = %ld\n" \
				"r14 = %ld\n" \
				"r15 = %ld\n" \
					
// debugfs-директория и файлы в ней - для передачи аргументов (fd), доступа к структурам
static struct dentry *debugfs_dir;
static struct dentry *debugfs_args;
static struct dentry *debugfs_vfs;
static struct dentry *debugfs_ptregs;

static char lab2_write_buffer[BUFFER_SIZE];

// Переменная - флаг корректной инициализации модуля 
static bool init_valid = false;
// Файловый дескриптор для vfsmount
static int fd = 1;

struct mutex lab2mutex;

// Вытаскиваем pt_regs
static struct pt_regs *get_ptregs(void) {
	return task_pt_regs(current);
}

// Вытаскиваем vfsmount по fd 
static struct vfsmount *get_vfsmount(int fdesc) {
	pr_warning("Trying to extract vfsmount by fd = %d", fdesc);
	struct fd f = fdget(fdesc);
	if (!f.file) {
		pr_warning("Error opening a file by fd");
		return NULL;
	}
	struct vfsmount *vfs = f.file->f_path.mnt;
	return vfs;
}

// пишем пользователю в буффер структуру pt_regs
static ssize_t ptregs_to_user(struct file *flip, char __user *usr_buffer, size_t length, loff_t *offset) {
	char kern_buffer[BUFFER_SIZE];
	pr_info("Sending pt_regs to user...\n");	

	if (*offset > 0 || BUFFER_SIZE > length) return -EFAULT;

	if (init_valid == false) strcpy(kern_buffer, "Module wasn't initialized correctly. Check kernel logs (dmesg)\n");
	else {
		struct pt_regs *regs = get_ptregs();
		sprintf(kern_buffer, PTREGS_MESSAGE, 
					regs->ax,
					regs->di,
					regs->si,
					regs->cx,
					regs->dx,
					regs->bx,
					regs->bp,
					regs->sp,
					regs->r10, 
					regs->r11,
					regs->r12,
					regs->r13,
					regs->r14,
					regs->r15
		);		
	}

	length = strlen(kern_buffer) + 1;
	*offset += length;

	if (copy_to_user(usr_buffer, kern_buffer, length)) return -EFAULT;
	return *offset;
}


// пишем пользователю в буффер структуру vfsmount
static ssize_t vfs_to_user(struct file *flip, char __user *usr_buffer, size_t length, loff_t *offset) {
	char kern_buffer[BUFFER_SIZE];
	pr_info("Sending vfsmount to user...\n");	

	if (*offset > 0 || BUFFER_SIZE > length) return -EFAULT;

	struct vfsmount *vfs = get_vfsmount(fd);
	if (init_valid == false) strcpy(kern_buffer, "Module wasn't initialized correctly. Check kernel logs (dmesg)\n");
	else if (vfs == NULL) strcpy(kern_buffer, "Could't extract vfsmount by provided fd\n");
	else {
		sprintf(kern_buffer, VFSMOUNT_MESSAGE, vfs->mnt_flags, vfs->mnt_sb->s_blocksize, vfs->mnt_sb->s_count);		
	}

	length = strlen(kern_buffer) + 1;
	*offset += length;

	if (copy_to_user(usr_buffer, kern_buffer, length)) return -EFAULT;
	return *offset;
}

// читаем аргументы от пользователя (fd для vfsmount)
static ssize_t args_from_user(struct file *filp, const char __user *usr_buffer, size_t length, loff_t *offset) {
	int arg;
	char kern_buffer[BUFFER_SIZE];

	pr_info("Getting args from user...\n");

	if (*offset > 0 || length > BUFFER_SIZE || copy_from_user(kern_buffer, usr_buffer, length)) return -EFAULT;

	int r_status = sscanf(kern_buffer, "%d", &arg);
	pr_info("arg received: %d\n", arg);

	if (r_status < 1) {
		fd = -1;
		return -EFAULT;
	}
	
	fd = arg;
	*offset = strlen(kern_buffer);
	return *offset;
}

// Открытие файла для аргументов
static int open_res(struct inode *inode, struct file *file) {
	mutex_lock(&lab2mutex);
	pr_info("args file opened\n");
	return 0;
}

// Закрытие файла для аргументов
static int release_res(struct inode *inode, struct file *file) {
	mutex_unlock(&lab2mutex);
	pr_info("args file released\n");
	return 0;
}


// Переопределение операций (открытие, запись, закрытие) файла для аргументов
static const struct file_operations args_file_ops = {
	.owner = THIS_MODULE,
	.open = open_res,
	.write = args_from_user
};

// Переопределение операций (чтение) файла для vfsmount
static const struct file_operations res_vfsmount_file_ops = {
	.owner = THIS_MODULE,
	.read = vfs_to_user,
	.release = release_res
};

// Переопределение операций (чтение) файла для pt_regs
static const struct file_operations res_pt_regs_file_ops = {
	.owner = THIS_MODULE,
	.read = ptregs_to_user,
	.release = release_res
};

// Инициализация модуля (создаем debugfs-директорию и файлы в ней, инициализируем мьютекс)
static int __init lab2_init(void) {
	pr_info("Module initialization\n");
	debugfs_dir = debugfs_create_dir(DEBUGFS_DIRNAME, NULL);
	if (debugfs_dir == NULL) {
		pr_warning("Debugfs dir creation failed\n");
		return 1;
	}
	debugfs_args = debugfs_create_file(DEBUGFS_ARGS_FILENAME, 0666, debugfs_dir, NULL, &args_file_ops);
	if (debugfs_args == NULL) {
		pr_warning("Debugfs args file creation failed\n");
		return 2;
	}
	debugfs_vfs = debugfs_create_file(DEBUGFS_VFS_FILENAME, 0666, debugfs_dir, NULL, &res_vfsmount_file_ops);
	if (debugfs_vfs == NULL) {
		pr_warning("Debugfs vfsmount access file creation failed\n");
		return 3;
	}
	debugfs_ptregs = debugfs_create_file(DEBUGFS_PTREGS_FILENAME, 0666, debugfs_dir, NULL, &res_pt_regs_file_ops);
	if (debugfs_ptregs == NULL) {
		pr_warning("Debugfs pt_regs access file creation failed\n");
		return 4;
	}
	mutex_init(&lab2mutex);
	init_valid = true;
	return 0;
}

// Деинициализация модуля (очищаем созданную директорию, мьютекс)
static void __exit lab2_exit(void) {
	mutex_destroy(&lab2mutex);
	debugfs_remove_recursive(debugfs_dir);
}

module_init(lab2_init);
module_exit(lab2_exit);
