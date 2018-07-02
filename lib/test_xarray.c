// SPDX-License-Identifier: GPL-2.0+
/*
 * test_xarray.c: Test the XArray API
 * Copyright (c) 2017-2018 Microsoft Corporation
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/xarray.h>
#include <linux/module.h>

static unsigned int tests_run;
static unsigned int tests_passed;

#ifndef XA_DEBUG
# ifdef __KERNEL__
void xa_dump(const struct xarray *xa) { }
# endif
#undef XA_BUG_ON
#define XA_BUG_ON(xa, x) do {					\
	tests_run++;						\
	if (x) {						\
		xa_dump(xa);					\
		dump_stack();					\
	} else {						\
		tests_passed++;					\
	}							\
} while (0)
#endif

static void *xa_store_value(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	radix_tree_insert(xa, index, xa_mk_value(index));
	return NULL;
}

static void xa_erase_value(struct xarray *xa, unsigned long index)
{
	radix_tree_delete(xa, index);
}

static void check_xa_load(struct xarray *xa)
{
	unsigned long i, j;

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j < i)
				XA_BUG_ON(xa, xa_to_value(entry) != j);
			else
				XA_BUG_ON(xa, entry);
		}
		XA_BUG_ON(xa, xa_store_value(xa, i, GFP_KERNEL) != NULL);
	}

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j >= i)
				XA_BUG_ON(xa, xa_to_value(entry) != j);
			else
				XA_BUG_ON(xa, entry);
		}
		xa_erase_value(xa, i);
	}
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_xa_tag_1(struct xarray *xa, unsigned long index)
{
	/* NULL elements have no tags set */
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_0));
	xa_set_tag(xa, index, XA_TAG_0);
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_0));

	/* Storing a pointer will not make a tag appear */
	XA_BUG_ON(xa, xa_store_value(xa, index, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_0));
	xa_set_tag(xa, index, XA_TAG_0);
	XA_BUG_ON(xa, !xa_get_tag(xa, index, XA_TAG_0));

	/* Setting one tag will not set another tag */
	XA_BUG_ON(xa, xa_get_tag(xa, index + 1, XA_TAG_0));
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_1));

	/* Storing NULL clears tags, and they can't be set again */
	xa_erase_value(xa, index);
	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_0));
	xa_set_tag(xa, index, XA_TAG_0);
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_0));
}

static void check_xa_tag(struct xarray *xa)
{
	check_xa_tag_1(xa, 0);
	check_xa_tag_1(xa, 4);
	check_xa_tag_1(xa, 64);
	check_xa_tag_1(xa, 4096);
}

static int xarray_checks(void)
{
	RADIX_TREE(array, GFP_KERNEL);

	check_xa_load(&array);
	check_xa_tag(&array);

	printk("XArray: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run != tests_passed) ? 0 : -EINVAL;
}

static void xarray_exit(void)
{
}

module_init(xarray_checks);
module_exit(xarray_exit);
MODULE_AUTHOR("Matthew Wilcox <willy@infradead.org>");
MODULE_LICENSE("GPL");
