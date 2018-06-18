/* Filesystem access-by-fd.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/anon_inodes.h>
#include "mount.h"

/*
 * Userspace writes configuration data and commands to the fd and we parse it
 * here.  For the moment, we assume a single option or command per write.  Each
 * line written is of the form
 *
 *	<option_type><space><stuff...>
 *
 *	d /dev/sda1				-- Device name
 *	o noatime				-- Option without value
 *	o cell=grand.central.org		-- Option with value
 *	r /					-- Dir within device to mount
 *	x create				-- Create a superblock
 *	x reconfigure				-- Reconfigure a superblock
 */
static ssize_t fscontext_write(struct file *file,
			       const char __user *_buf, size_t len, loff_t *pos)
{
	struct fs_context *fc = file->private_data;
	char opt[2], *data;
	ssize_t ret;

	if (len < 3 || len > 4095)
		return -EINVAL;

	if (copy_from_user(opt, _buf, 2) != 0)
		return -EFAULT;
	switch (opt[0]) {
	case 's':
	case 'o':
	case 'x':
		break;
	default:
		goto err_bad_cmd;
	}
	if (opt[1] != ' ')
		goto err_bad_cmd;

	data = memdup_user_nul(_buf + 2, len - 2);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* From this point onwards we need to lock the fd against someone
	 * trying to mount it.
	 */
	ret = mutex_lock_interruptible(&fc->uapi_mutex);
	if (ret < 0)
		goto err_free;

	if (fc->phase == FS_CONTEXT_AWAITING_RECONF) {
		if (fc->fs_type->init_fs_context) {
			ret = fc->fs_type->init_fs_context(fc, fc->root);
			if (ret < 0) {
				fc->phase = FS_CONTEXT_FAILED;
				goto err_unlock;
			}
		} else {
			/* Leave legacy context ops in place */
		}

		/* Do the security check last because ->init_fs_context may
		 * change the namespace subscriptions.
		 */
		ret = security_fs_context_alloc(fc, fc->root);
		if (ret < 0) {
			fc->phase = FS_CONTEXT_FAILED;
			goto err_unlock;
		}

		fc->phase = FS_CONTEXT_RECONF_PARAMS;
	}

	ret = -EINVAL;
	switch (opt[0]) {
	case 's':
		if (fc->phase != FS_CONTEXT_CREATE_PARAMS &&
		    fc->phase != FS_CONTEXT_RECONF_PARAMS)
			goto wrong_phase;
		ret = vfs_set_fs_source(fc, data, len - 2);
		if (ret < 0)
			goto err_unlock;
		data = NULL;
		break;

	case 'o':
		if (fc->phase != FS_CONTEXT_CREATE_PARAMS &&
		    fc->phase != FS_CONTEXT_RECONF_PARAMS)
			goto wrong_phase;
		ret = vfs_parse_fs_option(fc, data, len - 2);
		if (ret < 0)
			goto err_unlock;
		break;

	case 'x':
		if (strcmp(data, "create") == 0) {
			if (fc->phase != FS_CONTEXT_CREATE_PARAMS)
				goto wrong_phase;
			fc->phase = FS_CONTEXT_CREATING;
			ret = vfs_get_tree(fc);
			if (ret == 0)
				fc->phase = FS_CONTEXT_AWAITING_MOUNT;
			else
				fc->phase = FS_CONTEXT_FAILED;
		} else {
			ret = -EOPNOTSUPP;
		}
		if (ret < 0)
			goto err_unlock;
		break;

	default:
		goto err_unlock;
	}

	ret = len;
err_unlock:
	mutex_unlock(&fc->uapi_mutex);
err_free:
	kfree(data);
	return ret;
err_bad_cmd:
	return -EINVAL;
wrong_phase:
	ret = -EBUSY;
	goto err_unlock;
}

static int fscontext_release(struct inode *inode, struct file *file)
{
	struct fs_context *fc = file->private_data;

	if (fc) {
		file->private_data = NULL;
		put_fs_context(fc);
	}
	return 0;
}

const struct file_operations fscontext_fs_fops = {
	.write		= fscontext_write,
	.release	= fscontext_release,
	.llseek		= no_llseek,
};

/*
 * Attach a filesystem context to a file and an fd.
 */
static int fsopen_create_fd(struct fs_context *fc, unsigned int o_flags)
{
	int fd;

	fd = anon_inode_getfd("fscontext", &fscontext_fs_fops, fc,
			      O_RDWR | o_flags);
	if (fd < 0)
		put_fs_context(fc);
	return fd;
}

/*
 * Open a filesystem by name so that it can be configured for mounting.
 *
 * We are allowed to specify a container in which the filesystem will be
 * opened, thereby indicating which namespaces will be used (notably, which
 * network namespace will be used for network filesystems).
 */
SYSCALL_DEFINE2(fsopen, const char __user *, _fs_name, unsigned int, flags)
{
	struct file_system_type *fs_type;
	struct fs_context *fc;
	const char *fs_name;

	if (!ns_capable(current->nsproxy->mnt_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	if (flags & ~FSOPEN_CLOEXEC)
		return -EINVAL;

	fs_name = strndup_user(_fs_name, PAGE_SIZE);
	if (IS_ERR(fs_name))
		return PTR_ERR(fs_name);

	fs_type = get_fs_type(fs_name);
	kfree(fs_name);
	if (!fs_type)
		return -ENODEV;

	fc = vfs_new_fs_context(fs_type, NULL, 0, FS_CONTEXT_FOR_USER_MOUNT);
	put_filesystem(fs_type);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	fc->phase = FS_CONTEXT_CREATE_PARAMS;
	return fsopen_create_fd(fc, flags & FSOPEN_CLOEXEC ? O_CLOEXEC : 0);
}
