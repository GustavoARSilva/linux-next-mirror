#undef TRACE_SYSTEM
#define TRACE_SYSTEM filemap

#if !defined(_TRACE_FILEMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FILEMAP_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/errseq.h>

DECLARE_EVENT_CLASS(mm_filemap_op_page_cache,

	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, i_ino)
		__field(unsigned long, index)
		__field(dev_t, s_dev)
	),

	TP_fast_assign(
		__entry->pfn = page_to_pfn(page);
		__entry->i_ino = page->mapping->host->i_ino;
		__entry->index = page->index;
		if (page->mapping->host->i_sb)
			__entry->s_dev = page->mapping->host->i_sb->s_dev;
		else
			__entry->s_dev = page->mapping->host->i_rdev;
	),

	TP_printk("dev %d:%d ino %lx page=%p pfn=%lu ofs=%lu",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino,
		pfn_to_page(__entry->pfn),
		__entry->pfn,
		__entry->index << PAGE_SHIFT)
);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_delete_from_page_cache,
	TP_PROTO(struct page *page),
	TP_ARGS(page)
	);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_add_to_page_cache,
	TP_PROTO(struct page *page),
	TP_ARGS(page)
	);

TRACE_EVENT(filemap_set_wb_err,
		TP_PROTO(struct address_space *mapping, errseq_t eseq),

		TP_ARGS(mapping, eseq),

		TP_STRUCT__entry(
			__field(unsigned long, i_ino)
			__field(dev_t, s_dev)
			__field(errseq_t, errseq)
		),

		TP_fast_assign(
			__entry->i_ino = mapping->host->i_ino;
			__entry->errseq = eseq;
			if (mapping->host->i_sb)
				__entry->s_dev = mapping->host->i_sb->s_dev;
			else
				__entry->s_dev = mapping->host->i_rdev;
		),

		TP_printk("dev=%d:%d ino=0x%lx errseq=0x%x",
			MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
			__entry->i_ino, __entry->errseq)
);

TRACE_EVENT(filemap_report_wb_err,
		TP_PROTO(struct address_space *mapping, errseq_t old,
				errseq_t new),

		TP_ARGS(mapping, old, new),

		TP_STRUCT__entry(
			__field(unsigned long, i_ino)
			__field(dev_t, s_dev)
			__field(errseq_t, old)
			__field(errseq_t, new)
		),

		TP_fast_assign(
			__entry->i_ino = mapping->host->i_ino;
			if (mapping->host->i_sb)
				__entry->s_dev = mapping->host->i_sb->s_dev;
			else
				__entry->s_dev = mapping->host->i_rdev;
			__entry->old = old;
			__entry->new = new;
		),

		TP_printk("dev=%d:%d ino=0x%lx old=0x%x new=0x%x",
			MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
			__entry->i_ino, __entry->old, __entry->new)
);
#endif /* _TRACE_FILEMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
