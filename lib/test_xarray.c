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

static void check_xas_retry(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);

	xa_store_value(xa, 0, GFP_KERNEL);
	xa_store_value(xa, 1, GFP_KERNEL);

	XA_BUG_ON(xa, xas_find(&xas, ULONG_MAX) != xa_mk_value(0));
	xa_erase_value(xa, 1);
	XA_BUG_ON(xa, !xa_is_retry(xas_reload(&xas)));
	XA_BUG_ON(xa, xas_retry(&xas, NULL));
	XA_BUG_ON(xa, xas_retry(&xas, xa_mk_value(0)));
	xas_reset(&xas);
	XA_BUG_ON(xa, xas.xa_node != XAS_RESTART);
	XA_BUG_ON(xa, xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	XA_BUG_ON(xa, xas.xa_node != NULL);

	XA_BUG_ON(xa, xa_store_value(xa, 1, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, !xa_is_internal(xas_reload(&xas)));
	xas.xa_node = XAS_RESTART;
	XA_BUG_ON(xa, xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	xa_erase_value(xa, 0);
	xa_erase_value(xa, 1);
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

static void check_xa_tag_2(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	unsigned long index;
	unsigned int count = 0;
	void *entry;

	xa_store_value(xa, 0, GFP_KERNEL);
	xa_set_tag(xa, 0, XA_TAG_0);
	xas_load(&xas);
	xas_init_tags(&xas);
	XA_BUG_ON(xa, !xa_get_tag(xa, 0, XA_TAG_0) == 0);

	for (index = 3500; index < 4500; index++) {
		xa_store_value(xa, index, GFP_KERNEL);
		xa_set_tag(xa, index, XA_TAG_0);
	}

	xas_reset(&xas);
	xas_for_each_tagged(&xas, entry, ULONG_MAX, XA_TAG_0)
		count++;
	XA_BUG_ON(xa, count != 1000);

	xas_for_each(&xas, entry, ULONG_MAX) {
		xas_init_tags(&xas);
		XA_BUG_ON(xa, !xa_get_tag(xa, xas.xa_index, XA_TAG_0));
		XA_BUG_ON(xa, !xas_get_tag(&xas, XA_TAG_0));
	}

	xa_destroy(xa);
}

static void check_xa_tag(struct xarray *xa)
{
	check_xa_tag_1(xa, 0);
	check_xa_tag_1(xa, 4);
	check_xa_tag_1(xa, 64);
	check_xa_tag_1(xa, 4096);
	check_xa_tag_2(xa);
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
	XA_BUG_ON(xa, node->slots[0] != xa_mk_value(0));
	rcu_read_lock();
	XA_BUG_ON(xa, xas_store(&xas, NULL) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 1) != NULL);
	XA_BUG_ON(xa, xas.xa_node != XAS_BOUNDS);
	XA_BUG_ON(xa, node->slots[0] != XA_RETRY_ENTRY);
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

static void check_xas_erase(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	void *entry;
	unsigned long i, j;

	for (i = 0; i < 200; i++) {
		for (j = i; j < 2 * i + 17; j++) {
			xas_set(&xas, j);
			do {
				xas_store(&xas, xa_mk_value(j));
			} while (xas_nomem(&xas, GFP_KERNEL));
		}

		xas_set(&xas, ULONG_MAX);
		do {
			xas_store(&xas, xa_mk_value(0));
		} while (xas_nomem(&xas, GFP_KERNEL));
		xas_store(&xas, NULL);

		xas_set(&xas, 0);
		j = i;
		xas_for_each(&xas, entry, ULONG_MAX) {
			XA_BUG_ON(xa, entry != xa_mk_value(j));
			xas_store(&xas, NULL);
			j++;
		}
		XA_BUG_ON(xa, !xa_empty(xa));
	}
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

static void __check_store_iter(struct xarray *xa, unsigned long start,
			unsigned int order, unsigned int present)
{
	XA_STATE_ORDER(xas, xa, start, order);
	void *entry;
	unsigned int count = 0;

retry:
	xas_for_each_conflict(&xas, entry) {
		XA_BUG_ON(xa, !xa_is_value(entry));
		XA_BUG_ON(xa, entry < xa_mk_value(start));
		XA_BUG_ON(xa, entry > xa_mk_value(start + (1UL << order) - 1));
		count++;
	}
	xas_store(&xas, xa_mk_value(start));
	if (xas_nomem(&xas, GFP_KERNEL)) {
		count = 0;
		goto retry;
	}
	XA_BUG_ON(xa, xas_error(&xas));
	XA_BUG_ON(xa, count != present);
	XA_BUG_ON(xa, xa_load(xa, start) != xa_mk_value(start));
	XA_BUG_ON(xa, xa_load(xa, start + (1UL << order) - 1) !=
			xa_mk_value(start));
	xa_erase_value(xa, start);
}

static void check_store_iter(struct xarray *xa)
{
	unsigned int i, j;

	for (i = 0; i < 20; i++) {
		unsigned int min = 1 << i;
		unsigned int max = (2 << i) - 1;
		__check_store_iter(xa, 0, i, 0);
		XA_BUG_ON(xa, !xa_empty(xa));
		__check_store_iter(xa, min, i, 0);
		XA_BUG_ON(xa, !xa_empty(xa));

		xa_store_value(xa, min, GFP_KERNEL);
		__check_store_iter(xa, min, i, 1);
		XA_BUG_ON(xa, !xa_empty(xa));
		xa_store_value(xa, max, GFP_KERNEL);
		__check_store_iter(xa, min, i, 1);
		XA_BUG_ON(xa, !xa_empty(xa));

		for (j = 0; j < min; j++)
			xa_store_value(xa, j, GFP_KERNEL);
		__check_store_iter(xa, 0, i, min);
		XA_BUG_ON(xa, !xa_empty(xa));
		for (j = 0; j < min; j++)
			xa_store_value(xa, min + j, GFP_KERNEL);
		__check_store_iter(xa, min, i, min);
		XA_BUG_ON(xa, !xa_empty(xa));
	}
	xa_store_value(xa, 63, GFP_KERNEL);
	xa_store_value(xa, 65, GFP_KERNEL);
	__check_store_iter(xa, 64, 2, 1);
	xa_erase_value(xa, 63);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_multi_find(struct xarray *xa)
{
	unsigned long index;

	xa_store_order(xa, 12, 2, xa_mk_value(12), GFP_KERNEL);
	XA_BUG_ON(xa, xa_store_value(xa, 16, GFP_KERNEL) != NULL);

	index = 0;
	XA_BUG_ON(xa, xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	XA_BUG_ON(xa, index != 12);
	index = 13;
	XA_BUG_ON(xa, xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	XA_BUG_ON(xa, (index < 12) || (index >= 16));
	XA_BUG_ON(xa, xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(16));
	XA_BUG_ON(xa, index != 16);

	xa_erase_value(xa, 12);
	xa_erase_value(xa, 16);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_find(struct xarray *xa)
{
	unsigned long i, j, k;

	XA_BUG_ON(xa, !xa_empty(xa));

	for (i = 0; i < 100; i++) {
		XA_BUG_ON(xa, xa_store_value(xa, i, GFP_KERNEL) != NULL);
		xa_set_tag(xa, i, XA_TAG_0);
		for (j = 0; j < i; j++) {
			XA_BUG_ON(xa, xa_store_value(xa, j, GFP_KERNEL) !=
					NULL);
			xa_set_tag(xa, j, XA_TAG_0);
			for (k = 0; k < 100; k++) {
				unsigned long index = k;
				void *entry = xa_find(xa, &index, ULONG_MAX,
								XA_PRESENT);
				if (k <= j)
					XA_BUG_ON(xa, index != j);
				else if (k <= i)
					XA_BUG_ON(xa, index != i);
				else
					XA_BUG_ON(xa, entry != NULL);

				index = k;
				entry = xa_find(xa, &index, ULONG_MAX,
								XA_TAG_0);
				if (k <= j)
					XA_BUG_ON(xa, index != j);
				else if (k <= i)
					XA_BUG_ON(xa, index != i);
				else
					XA_BUG_ON(xa, entry != NULL);
			}
			xa_erase_value(xa, j);
			XA_BUG_ON(xa, xa_get_tag(xa, j, XA_TAG_0));
			XA_BUG_ON(xa, !xa_get_tag(xa, i, XA_TAG_0));
		}
		xa_erase_value(xa, i);
		XA_BUG_ON(xa, xa_get_tag(xa, i, XA_TAG_0));
	}
	XA_BUG_ON(xa, !xa_empty(xa));
	check_multi_find(xa);
}

static void check_move_small(struct xarray *xa, unsigned long idx)
{
	XA_STATE(xas, xa, 0);
	unsigned long i;

	xa_store_value(xa, 0, GFP_KERNEL);
	xa_store_value(xa, idx, GFP_KERNEL);

	for (i = 0; i < idx * 4; i++) {
		void *entry = xas_next(&xas);
		if (i <= idx)
			XA_BUG_ON(xa, xas.xa_node == XAS_RESTART);
		XA_BUG_ON(xa, xas.xa_index != i);
		if (i == 0 || i == idx)
			XA_BUG_ON(xa, entry != xa_mk_value(i));
		else
			XA_BUG_ON(xa, entry != NULL);
	}
	xas_next(&xas);
	XA_BUG_ON(xa, xas.xa_index != i);

	do {
		void *entry = xas_prev(&xas);
		i--;
		if (i <= idx)
			XA_BUG_ON(xa, xas.xa_node == XAS_RESTART);
		XA_BUG_ON(xa, xas.xa_index != i);
		if (i == 0 || i == idx)
			XA_BUG_ON(xa, entry != xa_mk_value(i));
		else
			XA_BUG_ON(xa, entry != NULL);
	} while (i > 0);

	xas_set(&xas, ULONG_MAX);
	XA_BUG_ON(xa, xas_next(&xas) != NULL);
	XA_BUG_ON(xa, xas.xa_index != ULONG_MAX);
	XA_BUG_ON(xa, xas_next(&xas) != xa_mk_value(0));
	XA_BUG_ON(xa, xas.xa_index != 0);
	XA_BUG_ON(xa, xas_prev(&xas) != NULL);
	XA_BUG_ON(xa, xas.xa_index != ULONG_MAX);

	xa_erase_value(xa, 0);
	xa_erase_value(xa, idx);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static void check_move(struct xarray *xa)
{
	XA_STATE(xas, xa, (1 << 16) - 1);
	unsigned long i;

	for (i = 0; i < (1 << 16); i++)
		XA_BUG_ON(xa, xa_store_value(xa, i, GFP_KERNEL) != NULL);

	do {
		void *entry = xas_prev(&xas);
		i--;
		XA_BUG_ON(xa, entry != xa_mk_value(i));
		XA_BUG_ON(xa, i != xas.xa_index);
	} while (i != 0);

	XA_BUG_ON(xa, xas_prev(&xas) != NULL);
	XA_BUG_ON(xa, xas.xa_index != ULONG_MAX);

	do {
		void *entry = xas_next(&xas);
		XA_BUG_ON(xa, entry != xa_mk_value(i));
		XA_BUG_ON(xa, i != xas.xa_index);
		i++;
	} while (i < (1 << 16));

	for (i = (1 << 8); i < (1 << 15); i++)
		xa_erase_value(xa, i);

	i = xas.xa_index;

	do {
		void *entry = xas_prev(&xas);
		i--;
		if ((i < (1 << 8)) || (i >= (1 << 15)))
			XA_BUG_ON(xa, entry != xa_mk_value(i));
		else
			XA_BUG_ON(xa, entry != NULL);
		XA_BUG_ON(xa, i != xas.xa_index);
	} while (i != 0);

	XA_BUG_ON(xa, xas_prev(&xas) != NULL);
	XA_BUG_ON(xa, xas.xa_index != ULONG_MAX);

	do {
		void *entry = xas_next(&xas);
		if ((i < (1 << 8)) || (i >= (1 << 15)))
			XA_BUG_ON(xa, entry != xa_mk_value(i));
		else
			XA_BUG_ON(xa, entry != NULL);
		XA_BUG_ON(xa, i != xas.xa_index);
		i++;
	} while (i < (1 << 16));

	xa_destroy(xa);

	for (i = 0; i < 16; i++)
		check_move_small(xa, 1UL << i);

	for (i = 2; i < 16; i++)
		check_move_small(xa, (1UL << i) - 1);
}

static LIST_HEAD(shadow_nodes);

static void test_update_node(struct xa_node *node)
{
	if (node->count && node->count == node->nr_values) {
		if (list_empty(&node->private_list))
			list_add(&shadow_nodes, &node->private_list);
	} else {
		if (!list_empty(&node->private_list))
			list_del_init(&node->private_list);
	}
}

static void shadow_remove(struct xarray *xa)
{
	struct xa_node *node;

	while ((node = list_first_entry_or_null(&shadow_nodes,
					struct xa_node, private_list))) {
		XA_STATE(xas, node->array, 0);
		XA_BUG_ON(xa, node->array != xa);
		list_del_init(&node->private_list);
		xas.xa_node = xa_parent_locked(node->array, node);
		xas.xa_offset = node->offset;
		xas.xa_shift = node->shift + XA_CHUNK_SHIFT;
		xas_set_update(&xas, test_update_node);
		xas_store(&xas, NULL);
	}
}

static void check_workingset(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	xas_set_update(&xas, test_update_node);

	do {
		xas_store(&xas, xa_mk_value(0));
	} while (xas_nomem(&xas, GFP_KERNEL));

	xas_next(&xas);
	do {
		xas_store(&xas, xa_mk_value(1));
	} while (xas_nomem(&xas, GFP_KERNEL));
	XA_BUG_ON(xa, list_empty(&shadow_nodes));

	xas_next(&xas);
	xas_store(&xas, &xas);
	XA_BUG_ON(xa, !list_empty(&shadow_nodes));

	xas_store(&xas, xa_mk_value(2));
	XA_BUG_ON(xa, list_empty(&shadow_nodes));

	shadow_remove(xa);
	XA_BUG_ON(xa, !list_empty(&shadow_nodes));
	XA_BUG_ON(xa, !xa_empty(xa));
}

static int xarray_checks(void)
{
	DEFINE_XARRAY(array);

	check_xa_err(&array);
	check_xas_retry(&array);
	check_xa_load(&array);
	check_xa_tag(&array);
	check_xa_shrink(&array);
	check_xas_erase(&array);
	check_cmpxchg(&array);
	check_multi_store(&array);
	check_find(&array);
	check_move(&array);
	check_store_iter(&array);

	check_workingset(&array, 0);
	check_workingset(&array, 64);
	check_workingset(&array, 4096);

	printk("XArray: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run != tests_passed) ? 0 : -EINVAL;
}

static void xarray_exit(void)
{
}

module_init(xarray_checks);
module_exit(xarray_exit);
