#include <stdio.h>

#include "../../util/util.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/evsel.h"


static size_t callchain__fprintf_left_margin(FILE *fp, int left_margin)
{
	int i;
	int ret = fprintf(fp, "            ");

	for (i = 0; i < left_margin; i++)
		ret += fprintf(fp, " ");

	return ret;
}

static size_t ipchain__fprintf_graph_line(FILE *fp, int depth, int depth_mask,
					  int left_margin)
{
	int i;
	size_t ret = callchain__fprintf_left_margin(fp, left_margin);

	for (i = 0; i < depth; i++)
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|          ");
		else
			ret += fprintf(fp, "           ");

	ret += fprintf(fp, "\n");

	return ret;
}

static size_t ipchain__fprintf_graph(FILE *fp, struct callchain_node *node,
				     struct callchain_list *chain,
				     int depth, int depth_mask, int period,
				     u64 total_samples, int left_margin)
{
	int i;
	size_t ret = 0;
	char bf[1024];

	ret += callchain__fprintf_left_margin(fp, left_margin);
	for (i = 0; i < depth; i++) {
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|");
		else
			ret += fprintf(fp, " ");
		if (!period && i == depth - 1) {
			ret += fprintf(fp, "--");
			ret += callchain_node__fprintf_value(node, fp, total_samples);
			ret += fprintf(fp, "--");
		} else
			ret += fprintf(fp, "%s", "          ");
	}
	fputs(callchain_list__sym_name(chain, bf, sizeof(bf), false), fp);
	fputc('\n', fp);
	return ret;
}

static struct symbol *rem_sq_bracket;
static struct callchain_list rem_hits;

static void init_rem_hits(void)
{
	rem_sq_bracket = malloc(sizeof(*rem_sq_bracket) + 6);
	if (!rem_sq_bracket) {
		fprintf(stderr, "Not enough memory to display remaining hits\n");
		return;
	}

	strcpy(rem_sq_bracket->name, "[...]");
	rem_hits.ms.sym = rem_sq_bracket;
}

static size_t __callchain__fprintf_graph(FILE *fp, struct rb_root *root,
					 u64 total_samples, int depth,
					 int depth_mask, int left_margin)
{
	struct rb_node *node, *next;
	struct callchain_node *child = NULL;
	struct callchain_list *chain;
	int new_depth_mask = depth_mask;
	u64 remaining;
	size_t ret = 0;
	int i;
	uint entries_printed = 0;
	int cumul_count = 0;

	remaining = total_samples;

	node = rb_first(root);
	while (node) {
		u64 new_total;
		u64 cumul;

		child = rb_entry(node, struct callchain_node, rb_node);
		cumul = callchain_cumul_hits(child);
		remaining -= cumul;
		cumul_count += callchain_cumul_counts(child);

		/*
		 * The depth mask manages the output of pipes that show
		 * the depth. We don't want to keep the pipes of the current
		 * level for the last child of this depth.
		 * Except if we have remaining filtered hits. They will
		 * supersede the last child
		 */
		next = rb_next(node);
		if (!next && (callchain_param.mode != CHAIN_GRAPH_REL || !remaining))
			new_depth_mask &= ~(1 << (depth - 1));

		/*
		 * But we keep the older depth mask for the line separator
		 * to keep the level link until we reach the last child
		 */
		ret += ipchain__fprintf_graph_line(fp, depth, depth_mask,
						   left_margin);
		i = 0;
		list_for_each_entry(chain, &child->val, list) {
			ret += ipchain__fprintf_graph(fp, child, chain, depth,
						      new_depth_mask, i++,
						      total_samples,
						      left_margin);
		}

		if (callchain_param.mode == CHAIN_GRAPH_REL)
			new_total = child->children_hit;
		else
			new_total = total_samples;

		ret += __callchain__fprintf_graph(fp, &child->rb_root, new_total,
						  depth + 1,
						  new_depth_mask | (1 << depth),
						  left_margin);
		node = next;
		if (++entries_printed == callchain_param.print_limit)
			break;
	}

	if (callchain_param.mode == CHAIN_GRAPH_REL &&
		remaining && remaining != total_samples) {
		struct callchain_node rem_node = {
			.hit = remaining,
		};

		if (!rem_sq_bracket)
			return ret;

		if (callchain_param.value == CCVAL_COUNT && child && child->parent) {
			rem_node.count = child->parent->children_count - cumul_count;
			if (rem_node.count <= 0)
				return ret;
		}

		new_depth_mask &= ~(1 << (depth - 1));
		ret += ipchain__fprintf_graph(fp, &rem_node, &rem_hits, depth,
					      new_depth_mask, 0, total_samples,
					      left_margin);
	}

	return ret;
}

/*
 * If have one single callchain root, don't bother printing
 * its percentage (100 % in fractal mode and the same percentage
 * than the hist in graph mode). This also avoid one level of column.
 *
 * However when percent-limit applied, it's possible that single callchain
 * node have different (non-100% in fractal mode) percentage.
 */
static bool need_percent_display(struct rb_node *node, u64 parent_samples)
{
	struct callchain_node *cnode;

	if (rb_next(node))
		return true;

	cnode = rb_entry(node, struct callchain_node, rb_node);
	return callchain_cumul_hits(cnode) != parent_samples;
}

static size_t callchain__fprintf_graph(FILE *fp, struct rb_root *root,
				       u64 total_samples, u64 parent_samples,
				       int left_margin)
{
	struct callchain_node *cnode;
	struct callchain_list *chain;
	u32 entries_printed = 0;
	bool printed = false;
	struct rb_node *node;
	int i = 0;
	int ret = 0;
	char bf[1024];

	node = rb_first(root);
	if (node && !need_percent_display(node, parent_samples)) {
		cnode = rb_entry(node, struct callchain_node, rb_node);
		list_for_each_entry(chain, &cnode->val, list) {
			/*
			 * If we sort by symbol, the first entry is the same than
			 * the symbol. No need to print it otherwise it appears as
			 * displayed twice.
			 */
			if (!i++ && field_order == NULL &&
			    sort_order && !prefixcmp(sort_order, "sym"))
				continue;
			if (!printed) {
				ret += callchain__fprintf_left_margin(fp, left_margin);
				ret += fprintf(fp, "|\n");
				ret += callchain__fprintf_left_margin(fp, left_margin);
				ret += fprintf(fp, "---");
				left_margin += 3;
				printed = true;
			} else
				ret += callchain__fprintf_left_margin(fp, left_margin);

			ret += fprintf(fp, "%s\n", callchain_list__sym_name(chain, bf, sizeof(bf),
							false));

			if (++entries_printed == callchain_param.print_limit)
				break;
		}
		root = &cnode->rb_root;
	}

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		total_samples = parent_samples;

	ret += __callchain__fprintf_graph(fp, root, total_samples,
					  1, 1, left_margin);
	if (ret) {
		/* do not add a blank line if it printed nothing */
		ret += fprintf(fp, "\n");
	}

	return ret;
}

static size_t __callchain__fprintf_flat(FILE *fp, struct callchain_node *node,
					u64 total_samples)
{
	struct callchain_list *chain;
	size_t ret = 0;
	char bf[1024];

	if (!node)
		return 0;

	ret += __callchain__fprintf_flat(fp, node->parent, total_samples);


	list_for_each_entry(chain, &node->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		ret += fprintf(fp, "                %s\n", callchain_list__sym_name(chain,
					bf, sizeof(bf), false));
	}

	return ret;
}

static size_t callchain__fprintf_flat(FILE *fp, struct rb_root *tree,
				      u64 total_samples)
{
	size_t ret = 0;
	u32 entries_printed = 0;
	struct callchain_node *chain;
	struct rb_node *rb_node = rb_first(tree);

	while (rb_node) {
		chain = rb_entry(rb_node, struct callchain_node, rb_node);

		ret += fprintf(fp, "           ");
		ret += callchain_node__fprintf_value(chain, fp, total_samples);
		ret += fprintf(fp, "\n");
		ret += __callchain__fprintf_flat(fp, chain, total_samples);
		ret += fprintf(fp, "\n");
		if (++entries_printed == callchain_param.print_limit)
			break;

		rb_node = rb_next(rb_node);
	}

	return ret;
}

static size_t __callchain__fprintf_folded(FILE *fp, struct callchain_node *node)
{
	const char *sep = symbol_conf.field_sep ?: ";";
	struct callchain_list *chain;
	size_t ret = 0;
	char bf[1024];
	bool first;

	if (!node)
		return 0;

	ret += __callchain__fprintf_folded(fp, node->parent);

	first = (ret == 0);
	list_for_each_entry(chain, &node->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		ret += fprintf(fp, "%s%s", first ? "" : sep,
			       callchain_list__sym_name(chain,
						bf, sizeof(bf), false));
		first = false;
	}

	return ret;
}

static size_t callchain__fprintf_folded(FILE *fp, struct rb_root *tree,
					u64 total_samples)
{
	size_t ret = 0;
	u32 entries_printed = 0;
	struct callchain_node *chain;
	struct rb_node *rb_node = rb_first(tree);

	while (rb_node) {

		chain = rb_entry(rb_node, struct callchain_node, rb_node);

		ret += callchain_node__fprintf_value(chain, fp, total_samples);
		ret += fprintf(fp, " ");
		ret += __callchain__fprintf_folded(fp, chain);
		ret += fprintf(fp, "\n");
		if (++entries_printed == callchain_param.print_limit)
			break;

		rb_node = rb_next(rb_node);
	}

	return ret;
}

static size_t hist_entry_callchain__fprintf(struct hist_entry *he,
					    u64 total_samples, int left_margin,
					    FILE *fp)
{
	u64 parent_samples = he->stat.period;

	if (symbol_conf.cumulate_callchain)
		parent_samples = he->stat_acc->period;

	switch (callchain_param.mode) {
	case CHAIN_GRAPH_REL:
		return callchain__fprintf_graph(fp, &he->sorted_chain, total_samples,
						parent_samples, left_margin);
		break;
	case CHAIN_GRAPH_ABS:
		return callchain__fprintf_graph(fp, &he->sorted_chain, total_samples,
						parent_samples, left_margin);
		break;
	case CHAIN_FLAT:
		return callchain__fprintf_flat(fp, &he->sorted_chain, total_samples);
		break;
	case CHAIN_FOLDED:
		return callchain__fprintf_folded(fp, &he->sorted_chain, total_samples);
		break;
	case CHAIN_NONE:
		break;
	default:
		pr_err("Bad callchain mode\n");
	}

	return 0;
}

static int hist_entry__snprintf(struct hist_entry *he, struct perf_hpp *hpp)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	char *start = hpp->buf;
	int ret;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	hists__for_each_format(he->hists, fmt) {
		if (perf_hpp__should_skip(fmt, he->hists))
			continue;

		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (perf_hpp__use_color() && fmt->color)
			ret = fmt->color(fmt, hpp, he);
		else
			ret = fmt->entry(fmt, hpp, he);

		ret = hist_entry__snprintf_alignment(he, hpp, fmt, ret);
		advance_hpp(hpp, ret);
	}

	return hpp->buf - start;
}

static int hist_entry__hierarchy_fprintf(struct hist_entry *he,
					 struct perf_hpp *hpp,
					 int nr_sort_key, struct hists *hists,
					 FILE *fp)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	char *buf = hpp->buf;
	int ret, printed = 0;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	ret = scnprintf(hpp->buf, hpp->size, "%*s", he->depth * HIERARCHY_INDENT, "");
	advance_hpp(hpp, ret);

	hists__for_each_format(he->hists, fmt) {
		if (perf_hpp__is_sort_entry(fmt) || perf_hpp__is_dynamic_entry(fmt))
			break;

		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (perf_hpp__use_color() && fmt->color)
			ret = fmt->color(fmt, hpp, he);
		else
			ret = fmt->entry(fmt, hpp, he);

		ret = hist_entry__snprintf_alignment(he, hpp, fmt, ret);
		advance_hpp(hpp, ret);
	}

	if (sep)
		ret = scnprintf(hpp->buf, hpp->size, "%s", sep);
	else
		ret = scnprintf(hpp->buf, hpp->size, "%*s",
				(nr_sort_key - 1) * HIERARCHY_INDENT + 2, "");
	advance_hpp(hpp, ret);

	/*
	 * No need to call hist_entry__snprintf_alignment() since this
	 * fmt is always the last column in the hierarchy mode.
	 */
	fmt = he->fmt;
	if (perf_hpp__use_color() && fmt->color)
		fmt->color(fmt, hpp, he);
	else
		fmt->entry(fmt, hpp, he);

	printed += fprintf(fp, "%s\n", buf);

	if (symbol_conf.use_callchain && he->leaf) {
		u64 total = hists__total_period(hists);

		printed += hist_entry_callchain__fprintf(he, total, 0, fp);
		goto out;
	}

out:
	return printed;
}

static int hist_entry__fprintf(struct hist_entry *he, size_t size,
			       struct hists *hists,
			       char *bf, size_t bfsz, FILE *fp)
{
	int ret;
	struct perf_hpp hpp = {
		.buf		= bf,
		.size		= size,
	};
	u64 total_period = hists->stats.total_period;

	if (size == 0 || size > bfsz)
		size = hpp.size = bfsz;

	if (symbol_conf.report_hierarchy) {
		int nr_sort = hists->nr_sort_keys;

		return hist_entry__hierarchy_fprintf(he, &hpp, nr_sort,
						     hists, fp);
	}

	hist_entry__snprintf(he, &hpp);

	ret = fprintf(fp, "%s\n", bf);

	if (symbol_conf.use_callchain)
		ret += hist_entry_callchain__fprintf(he, total_period, 0, fp);

	return ret;
}

static int print_hierarchy_indent(const char *sep, int nr_sort,
				  const char *line, FILE *fp)
{
	if (sep != NULL || nr_sort < 1)
		return 0;

	return fprintf(fp, "%-.*s", (nr_sort - 1) * HIERARCHY_INDENT, line);
}

static int print_hierarchy_header(struct hists *hists, struct perf_hpp *hpp,
				  const char *sep, FILE *fp)
{
	bool first = true;
	int nr_sort;
	unsigned width = 0;
	unsigned header_width = 0;
	struct perf_hpp_fmt *fmt;

	nr_sort = hists->nr_sort_keys;

	/* preserve max indent depth for column headers */
	print_hierarchy_indent(sep, nr_sort, spaces, fp);

	hists__for_each_format(hists, fmt) {
		if (perf_hpp__is_sort_entry(fmt) || perf_hpp__is_dynamic_entry(fmt))
			break;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		fmt->header(fmt, hpp, hists_to_evsel(hists));
		fprintf(fp, "%s", hpp->buf);
	}

	/* combine sort headers with ' / ' */
	first = true;
	hists__for_each_format(hists, fmt) {
		if (!perf_hpp__is_sort_entry(fmt) && !perf_hpp__is_dynamic_entry(fmt))
			continue;
		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (!first)
			header_width += fprintf(fp, " / ");
		else {
			header_width += fprintf(fp, "%s", sep ?: "  ");
			first = false;
		}

		fmt->header(fmt, hpp, hists_to_evsel(hists));
		rtrim(hpp->buf);

		header_width += fprintf(fp, "%s", hpp->buf);
	}

	/* preserve max indent depth for combined sort headers */
	print_hierarchy_indent(sep, nr_sort, spaces, fp);

	fprintf(fp, "\n# ");

	/* preserve max indent depth for initial dots */
	print_hierarchy_indent(sep, nr_sort, dots, fp);

	first = true;
	hists__for_each_format(hists, fmt) {
		if (perf_hpp__is_sort_entry(fmt) || perf_hpp__is_dynamic_entry(fmt))
			break;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		width = fmt->width(fmt, hpp, hists_to_evsel(hists));
		fprintf(fp, "%.*s", width, dots);
	}

	hists__for_each_format(hists, fmt) {
		if (!perf_hpp__is_sort_entry(fmt) && !perf_hpp__is_dynamic_entry(fmt))
			continue;
		if (perf_hpp__should_skip(fmt, hists))
			continue;

		width = fmt->width(fmt, hpp, hists_to_evsel(hists));
		if (width > header_width)
			header_width = width;
	}

	fprintf(fp, "%s%-.*s", sep ?: "  ", header_width, dots);

	/* preserve max indent depth for dots under sort headers */
	print_hierarchy_indent(sep, nr_sort, dots, fp);

	fprintf(fp, "\n#\n");

	return 2;
}

size_t hists__fprintf(struct hists *hists, bool show_header, int max_rows,
		      int max_cols, float min_pcnt, FILE *fp)
{
	struct perf_hpp_fmt *fmt;
	struct rb_node *nd;
	size_t ret = 0;
	unsigned int width;
	const char *sep = symbol_conf.field_sep;
	int nr_rows = 0;
	char bf[96];
	struct perf_hpp dummy_hpp = {
		.buf	= bf,
		.size	= sizeof(bf),
	};
	bool first = true;
	size_t linesz;
	char *line = NULL;
	unsigned indent;

	init_rem_hits();

	hists__for_each_format(hists, fmt)
		perf_hpp__reset_width(fmt, hists);

	if (symbol_conf.col_width_list_str)
		perf_hpp__set_user_width(symbol_conf.col_width_list_str);

	if (!show_header)
		goto print_entries;

	fprintf(fp, "# ");

	if (symbol_conf.report_hierarchy) {
		nr_rows += print_hierarchy_header(hists, &dummy_hpp, sep, fp);
		goto print_entries;
	}

	hists__for_each_format(hists, fmt) {
		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		fmt->header(fmt, &dummy_hpp, hists_to_evsel(hists));
		fprintf(fp, "%s", bf);
	}

	fprintf(fp, "\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

	if (sep)
		goto print_entries;

	first = true;

	fprintf(fp, "# ");

	hists__for_each_format(hists, fmt) {
		unsigned int i;

		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		width = fmt->width(fmt, &dummy_hpp, hists_to_evsel(hists));
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}

	fprintf(fp, "\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

	fprintf(fp, "#\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

print_entries:
	linesz = hists__sort_list_width(hists) + 3 + 1;
	linesz += perf_hpp__color_overhead();
	line = malloc(linesz);
	if (line == NULL) {
		ret = -1;
		goto out;
	}

	indent = hists__overhead_width(hists) + 4;

	for (nd = rb_first(&hists->entries); nd; nd = __rb_hierarchy_next(nd, HMD_FORCE_CHILD)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent;

		if (h->filtered)
			continue;

		percent = hist_entry__get_percent_limit(h);
		if (percent < min_pcnt)
			continue;

		ret += hist_entry__fprintf(h, max_cols, hists, line, linesz, fp);

		if (max_rows && ++nr_rows >= max_rows)
			break;

		/*
		 * If all children are filtered out or percent-limited,
		 * display "no entry >= x.xx%" message.
		 */
		if (!h->leaf && !hist_entry__has_hierarchy_children(h, min_pcnt)) {
			int nr_sort = hists->nr_sort_keys;

			print_hierarchy_indent(sep, nr_sort + h->depth + 1, spaces, fp);
			fprintf(fp, "%*sno entry >= %.2f%%\n", indent, "", min_pcnt);

			if (max_rows && ++nr_rows >= max_rows)
				break;
		}

		if (h->ms.map == NULL && verbose > 1) {
			__map_groups__fprintf_maps(h->thread->mg,
						   MAP__FUNCTION, fp);
			fprintf(fp, "%.10s end\n", graph_dotted_line);
		}
	}

	free(line);
out:
	zfree(&rem_sq_bracket);

	return ret;
}

size_t events_stats__fprintf(struct events_stats *stats, FILE *fp)
{
	int i;
	size_t ret = 0;

	for (i = 0; i < PERF_RECORD_HEADER_MAX; ++i) {
		const char *name;

		if (stats->nr_events[i] == 0)
			continue;

		name = perf_event__name(i);
		if (!strcmp(name, "UNKNOWN"))
			continue;

		ret += fprintf(fp, "%16s events: %10d\n", name,
			       stats->nr_events[i]);
	}

	return ret;
}
