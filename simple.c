/*
 * A Simple Filesystem for the Linux Kernel.
 *
 * Initial author: Sankar P <sankar.curiosity@gmail.com>
 * License: Creative Commons Zero License - http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "super.h"

static int simplefs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	loff_t pos = filp->f_pos;
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct simplefs_inode *sfs_inode;
	struct simplefs_dir_record *record;
	int i;

	printk(KERN_INFO "We are inside readdir. The pos[%lld], inode number[%lu], superblock magic [%lu]\n", pos, inode->i_ino, sb->s_magic);

	sfs_inode = inode->i_private;

	if (unlikely(!S_ISDIR(sfs_inode->mode))) {
		printk(KERN_ERR "inode %llu not a directory", sfs_inode->inode_no);
		return -ENOTDIR;
	}

	bh = (struct buffer_head *)sb_bread(sb, sfs_inode->data_block_number);

	record = (struct simplefs_dir_record *) bh->b_data;
	for (i=0; i < sfs_inode->dir_children_count; i++) {
		printk(KERN_INFO "Got filename: %s\n", record->filename);
		filldir(dirent, record->filename, SIMPLEFS_FILENAME_MAXLEN, pos, record->inode_no, DT_UNKNOWN);
		pos += sizeof(struct simplefs_dir_record);
		record ++;
	}

	return 1;
}

const struct file_operations simplefs_dir_operations = {
	.owner = THIS_MODULE,
	.readdir = simplefs_readdir,
};

struct dentry *simplefs_lookup(struct inode *parent_inode,
			       struct dentry *child_dentry, unsigned int flags)
{
	/* The lookup function is used for dentry association.
	 * As of now, we don't deal with dentries in simplefs.
	 * So we will keep this simple for now and revisit later */
	return NULL;
}

static struct inode_operations simplefs_inode_ops = {
	.lookup = simplefs_lookup,
};

/* This functions returns a simplefs_inode with the given inode_no
 * from the inode store, if it exists. */
struct simplefs_inode * simplefs_get_inode(struct super_block *sb, uint64_t inode_no)
{
	struct simplefs_super_block *sfs_sb = sb->s_fs_info;
	struct simplefs_inode *sfs_inode = NULL;

	int i;
	struct buffer_head *bh;

	/* The inode store can be read once and kept in memory permanently while mounting.
	 * But such a model will not be scalable in a filesystem with
	 * millions or billions of files (inodes) */
	bh = (struct buffer_head *)sb_bread(sb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
	sfs_inode = (struct simplefs_inode *) bh->b_data;

	for (i=0;i < sfs_sb->inodes_count; i++) {
		if (sfs_inode->inode_no == inode_no) {
			/* FIXME: bh->b_data is probably leaking */
			return sfs_inode;
		}
		sfs_inode++;
	}

	return NULL;
}

/* This function, as the name implies, Makes the super_block valid and
 * fills filesystem specific information in the super block */
int simplefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct buffer_head *bh;
	struct simplefs_super_block *sb_disk;

	bh = (struct buffer_head *)sb_bread(sb, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);

	sb_disk = (struct simplefs_super_block *)bh->b_data;
	/* FIXME: bh->b_data is probably leaking */

	printk(KERN_INFO "The magic number obtained in disk is: [%llu]\n",
	       sb_disk->magic);

	if (unlikely(sb_disk->magic != SIMPLEFS_MAGIC)) {
		printk(KERN_ERR
		       "The filesystem that you try to mount is not of type simplefs. Magicnumber mismatch.");
		return -EPERM;
	}

	if (unlikely(sb_disk->block_size != SIMPLEFS_DEFAULT_BLOCK_SIZE)) {
		printk(KERN_ERR
		       "simplefs seem to be formatted using a non-standard block size.");
		return -EPERM;
	}

	printk(KERN_INFO
	       "simplefs filesystem of version [%llu] formatted with a block size of [%llu] detected in the device.\n",
	       sb_disk->version, sb_disk->block_size);

	/* A magic number that uniquely identifies our filesystem type */
	sb->s_magic = SIMPLEFS_MAGIC;

	/* For all practical purposes, we will be using this s_fs_info as the super block */
	sb->s_fs_info = sb_disk;

	root_inode = new_inode(sb);
	root_inode->i_ino = SIMPLEFS_ROOTDIR_INODE_NUMBER;
	inode_init_owner(root_inode, NULL, S_IFDIR);
	root_inode->i_sb = sb;
	root_inode->i_op = &simplefs_inode_ops;
	root_inode->i_fop = &simplefs_dir_operations;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = CURRENT_TIME;

	root_inode->i_private = simplefs_get_inode(sb, SIMPLEFS_ROOTDIR_INODE_NUMBER);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	struct dentry *ret;

	ret = mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);

	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "Error mounting simplefs");
	else
		printk(KERN_INFO "simplefs is succesfully mounted on [%s]\n",
		       dev_name);

	return ret;
}

static void simplefs_kill_superblock(struct super_block *s)
{
	printk(KERN_INFO
	       "simplefs superblock is destroyed. Unmount succesful.\n");
	/* This is just a dummy function as of now. As our filesystem gets matured,
	 * we will do more meaningful operations here */
	return;
}

struct file_system_type simplefs_fs_type = {
	.owner = THIS_MODULE,
	.name = "simplefs",
	.mount = simplefs_mount,
	.kill_sb = simplefs_kill_superblock,
};

static int simplefs_init(void)
{
	int ret;

	ret = register_filesystem(&simplefs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully registered simplefs\n");
	else
		printk(KERN_ERR "Failed to register simplefs. Error:[%d]", ret);

	return ret;
}

static void simplefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&simplefs_fs_type);

	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully unregistered simplefs\n");
	else
		printk(KERN_ERR "Failed to unregister simplefs. Error:[%d]",
		       ret);
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("CC0");
MODULE_AUTHOR("Sankar P");
