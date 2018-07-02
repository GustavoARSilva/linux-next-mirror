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
	return xa_store(xa, index, xa_mk_value(index), gfp);
}

static void xa_erase_value(struct xarray *xa, unsigned long index)
{
	XA_BUG_ON(xa, xa_erase(xa, index) != xa_mk_value(index));
	XA_BUG_ON(xa, xa_load(xa, index) != NULL);
}

/*
 * If anyone needs this, please move it to xarray.c.  We have no current
 * users outside the test suite because all current multislot users want
 * to use the advanced API.
 */
static void *xa_store_order(struct xarray *xa, unsigned long index,
		unsigned order, void *entry, gfp_t gfp)
{
	XA_STATE(xas, xa, 0);
	void *curr;

	xas_set_order(&xas, index, order);
	do {
		curr = xas_store(&xas, entry);
	} while (xas_nomem(&xas, gfp));

	return curr;
}

static void check_xa_err(struct xarray *xa)
{
	XA_BUG_ON(xa, xa_err(xa_store_value(xa, 0, GFP_NOWAIT)) != 0);
	XA_BUG_ON(xa, xa_err(xa_erase(xa, 0)) != 0);
#ifndef __KERNEL__
	/* The kernel does not fail GFP_NOWAIT allocations */
	XA_BUG_ON(xa, xa_err(xa_store_value(xa, 1, GFP_NOWAIT)) != -ENOMEM);
	XA_BUG_ON(xa, xa_err(xa_store_value(xa, 1, GFP_NOWAIT)) != -ENOMEM);
#endif
	XA_BUG_ON(xa, xa_err(xa_store_value(xa, 1, GFP_KERNEL)) != 0);
	XA_BUG_ON(xa, xa_err(xa_store(xa, 1, xa_mk_value(0), GFP_KERNEL)) != 0);
	XA_BUG_ON(xa, xa_err(xa_erase(xa, 1)) != 0);
// kills the test-suite :-(
//	XA_BUG_ON(xa, xa_err(xa_store(xa, 0, xa_mk_internal(0), 0)) != -EINVAL);
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

	/*
	 * Storing a multi-index entry over entries with tags gives the
	 * entire entry the union of the tags
	 */
	BUG_ON((index % 4) != 0);
	XA_BUG_ON(xa, xa_store_value(xa, index + 1, GFP_KERNEL) != NULL);
	xa_set_tag(xa, index + 1, XA_TAG_0);
	XA_BUG_ON(xa, xa_store_value(xa, index + 2, GFP_KERNEL) != NULL);
	xa_set_tag(xa, index + 2, XA_TAG_1);
	xa_store_order(xa, index, 2, xa_mk_value(index), GFP_KERNEL);
	XA_BUG_ON(xa, !xa_get_tag(xa, index, XA_TAG_0));
	XA_BUG_ON(xa, !xa_get_tag(xa, index, XA_TAG_1));
	XA_BUG_ON(xa, xa_get_tag(xa, index, XA_TAG_2));
	XA_BUG_ON(xa, !xa_get_tag(xa, index + 1, XA_TAG_0));
	XA_BUG_ON(xa, !xa_get_tag(xa, index + 1, XA_TAG_1));
	XA_BUG_ON(xa, xa_get_tag(xa, index + 1, XA_TAG_2));
	xa_erase_value(xa, index);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_xa_tag(struct xarray *xa)
{
	check_xa_tag_1(xa, 0);
	check_xa_tag_1(xa, 4);
	check_xa_tag_1(xa, 64);
	check_xa_tag_1(xa, 4096);
}

static void check_xa_shrink(struct xarray *xa)
{
	XA_STATE(xas, xa, 1);
	struct xa_node *node;

	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_store_value(xa, 0, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_store_value(xa, 1, GFP_KERNEL) != NULL);

	/*
	 * Check that erasing the entry at 1 shrinks the tree and properly
	 * marks the node as being deleted.
	 */
	XA_BUG_ON(xa, xas_load(&xas) != xa_mk_value(1));
	node = xas.xa_node;
	rcu_read_lock();
	XA_BUG_ON(xa, rcu_dereference(node->slots[0]) != xa_mk_value(0));
	XA_BUG_ON(xa, xas_store(&xas, NULL) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 1) != NULL);
	XA_BUG_ON(xa, xas.xa_node != XAS_BOUNDS);
	XA_BUG_ON(xa, rcu_dereference(node->slots[0]) != XA_RETRY_ENTRY);
	XA_BUG_ON(xa, xas_load(&xas) != NULL);
	rcu_read_unlock();
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	xa_erase_value(xa, 0);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_cmpxchg(struct xarray *xa)
{
	void *FIVE = xa_mk_value(5);
	void *SIX = xa_mk_value(6);
	void *LOTS = xa_mk_value(12345678);

	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_store_value(xa, 12345678, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_insert(xa, 12345678, xa, GFP_KERNEL) != -EEXIST);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, SIX, FIVE, GFP_KERNEL) != LOTS);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, LOTS, FIVE, GFP_KERNEL) != LOTS);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, FIVE, LOTS, GFP_KERNEL) != FIVE);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 5, FIVE, NULL, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 5, NULL, FIVE, GFP_KERNEL) != NULL);
	xa_erase_value(xa, 12345678);
	xa_erase_value(xa, 5);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_multi_store(struct xarray *xa)
{
	unsigned long i, j, k;

	/* Loading from any position returns the same value */
	xa_store_order(xa, 0, 1, xa_mk_value(0), GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 2) != NULL);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 2);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 2);

	/* Storing adjacent to the value does not alter the value */
	xa_store(xa, 3, xa, GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 2) != NULL);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 3);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 2);

	/* Overwriting multiple indexes works */
	xa_store_order(xa, 0, 2, xa_mk_value(1), GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 2) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 3) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 4) != NULL);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 4);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 4);

	/* We can erase multiple values with a single store */
	xa_store_order(xa, 0, 64, NULL, GFP_KERNEL);
	XA_BUG_ON(xa, !xa_empty(xa));

	/* Even when the first slot is empty but the others aren't */
	xa_store_value(xa, 1, GFP_KERNEL);
	xa_store_value(xa, 2, GFP_KERNEL);
	xa_store_order(xa, 0, 2, NULL, GFP_KERNEL);
	XA_BUG_ON(xa, !xa_empty(xa));

	for (i = 0; i < 60; i++) {
		for (j = 0; j < 60; j++) {
			xa_store_order(xa, 0, i, xa_mk_value(i), GFP_KERNEL);
			xa_store_order(xa, 0, j, xa_mk_value(j), GFP_KERNEL);

			for (k = 0; k < 60; k++) {
				void *entry = xa_load(xa, (1UL << k) - 1);
				if ((i < k) && (j < k))
					XA_BUG_ON(xa, entry != NULL);
				else
					XA_BUG_ON(xa, entry != xa_mk_value(j));
			}

			xa_erase(xa, 0);
			XA_BUG_ON(xa, !xa_empty(xa));
		}
	}
}

static int xarray_checks(void)
{
	DEFINE_XARRAY(array);

	check_xa_err(&array);
	check_xa_load(&array);
	check_xa_tag(&array);
	check_xa_shrink(&array);
	check_cmpxchg(&array);
	check_multi_store(&array);

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
