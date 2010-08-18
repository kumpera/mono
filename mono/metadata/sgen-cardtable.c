/*
 * sgen-cardtable.c: Card table implementation for sgen
 *
 * Author:
 * 	Rodrigo Kumpera (rkumpera@novell.com)
 *
 * Copyright 2010 Novell, Inc (http://www.novell.com)
 *
 */

#define USE_FINE_GRAINED_CARD_TABLE_REFINEMENTS
//#define CARD_TABLE_STATS
//#define CARD_TABLE_PAGE_FAULT_STATS

#define CARD_COUNT_BITS (32 - CARD_BITS)
#define CARD_COUNT_IN_BYTES (1 << CARD_COUNT_BITS)
#define CARD_MASK ((1 << CARD_COUNT_BITS) - 1)

static guint8 *cardtable;

#ifdef OVERLAPPING_CARDS

static guint8 *shadow_cardtable;

static guint8*
sgen_card_table_get_shadow_card_address (mword address)
{
	return shadow_cardtable + ((address >> CARD_BITS) & CARD_MASK);
}

gboolean
sgen_card_table_card_begin_scanning (mword address)
{
	return *sgen_card_table_get_shadow_card_address (address) != 0;
}

static guint8*
sgen_card_table_get_card_address (mword address)
{
	return cardtable + ((address >> CARD_BITS) & CARD_MASK);
}

gboolean
sgen_card_table_region_begin_scanning (mword start, mword end)
{
	while (start <= end) {
		if (sgen_card_table_card_begin_scanning (start))
			return TRUE;
		start += CARD_SIZE_IN_BYTES;
	}
	return FALSE;
}

#else

static guint8*
sgen_card_table_get_card_address (mword address)
{
	return cardtable + (address >> CARD_BITS);
}

static gboolean
sgen_card_table_address_is_marked (mword address)
{
	return *sgen_card_table_get_card_address (address) != 0;
}

gboolean
sgen_card_table_card_begin_scanning (mword address)
{
	guint8 *card = sgen_card_table_get_card_address (address);
	gboolean res = *card;
	*card = 0;
	return res;
}

gboolean
sgen_card_table_region_begin_scanning (mword start, mword end)
{
	gboolean res = FALSE;
	mword old_start = start;
	while (start <= end) {
		if (sgen_card_table_address_is_marked (start)) {
			res = TRUE;
			break;
		}
		start += CARD_SIZE_IN_BYTES;
	}

	sgen_card_table_reset_region (old_start, end);
	return res;
}
#endif

#ifdef USE_FINE_GRAINED_CARD_TABLE_REFINEMENTS
#define REFINEMENT_BITS 3
#define REFINEMENT_EXTRA_SHIFT 0
#else
#define REFINEMENT_BITS 2
#define REFINEMENT_EXTRA_SHIFT 4
#define SEPARATE_MUTATOR_BIT 1
#endif

#define REFINEMENT_BIT_COUNT (1 << REFINEMENT_BITS)
#define REFINEMENT_SHIFT (9 - REFINEMENT_BITS)
#define REFINEMENT_MASK ((1 << REFINEMENT_BITS) - 1)

void
sgen_card_table_mark_address (mword address)
{
	*sgen_card_table_get_card_address (address) = 1;
}


void*
sgen_card_table_align_pointer (void *ptr)
{
	return (void*)((mword)ptr & ~(CARD_SIZE_IN_BYTES - 1));
}

void
sgen_card_table_reset_region (mword start, mword end)
{
	memset (sgen_card_table_get_card_address (start), 0, (end - start) >> CARD_BITS);
}

void
sgen_card_table_mark_range (mword address, mword size)
{
	mword end = address + size;
	do {
		sgen_card_table_mark_address (address);
		address += CARD_SIZE_IN_BYTES;
	} while (address < end);
}

static void
card_table_init (void)
{
	cardtable = mono_sgen_alloc_os_memory (CARD_COUNT_IN_BYTES, TRUE);
#ifdef OVERLAPPING_CARDS
	shadow_cardtable = mono_sgen_alloc_os_memory (CARD_COUNT_IN_BYTES, TRUE);
#endif
}


void los_scan_card_table (GrayQueue *queue);
void los_iterate_live_block_ranges (sgen_cardtable_block_callback callback);

#ifdef OVERLAPPING_CARDS

static void
move_cards_to_shadow_table (mword start, mword size)
{
	guint8 *from = sgen_card_table_get_card_address (start);
	guint8 *to = sgen_card_table_get_shadow_card_address (start);
	size_t bytes = size >> CARD_BITS;
	memcpy (to, from, bytes);
}

#endif

static void
clear_cards (mword start, mword size)
{
	memset (sgen_card_table_get_card_address (start), 0, size >> CARD_BITS);
}

static void
scan_from_card_tables (void *start_nursery, void *end_nursery, GrayQueue *queue)
{
#ifdef OVERLAPPING_CARDS
	/*First we copy*/
	major.iterate_live_block_ranges (move_cards_to_shadow_table);
	los_iterate_live_block_ranges (move_cards_to_shadow_table);

	/*Then we clear*/
	card_table_clear ()
#endif
	major.scan_card_table (queue);
	los_scan_card_table (queue);
}

static void
card_table_clear (void)
{
	/*XXX we could do this in 2 ways. using mincore or iterating over all sections/los objects */
	major.iterate_live_block_ranges (clear_cards);
	los_iterate_live_block_ranges (clear_cards);
}

#ifdef CARD_TABLE_STATS

static int total_cards;
static int total_cards_remarked;
static int total_bits_remarked, total_bits_mutator_free;
static int mutator_cards;
static int bits_remarked[REFINEMENT_BIT_COUNT];

static void
count_marked_bits (mword address, mword size)
{
	int i;
	mword end = address + size;
	do {
		guint8 card = *sgen_card_table_get_card_address (address);
		++total_cards;
		if (card)
			++total_cards_remarked;
		for (i = 0; i < REFINEMENT_BIT_COUNT; ++i) {
			if (card & (1 << (i + REFINEMENT_EXTRA_SHIFT))) {
				++bits_remarked [i];
				++total_bits_remarked;

				if (!(card & 0x1))
					++total_bits_mutator_free;
			}
		}
		if (card & 0x1)
			++mutator_cards;
		address += CARD_SIZE_IN_BYTES;
	} while (address < end);
}

#endif

#ifdef CARD_TABLE_PAGE_FAULT_STATS

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

static void
collect_faulted_cards (void)
{
#define CARD_PAGES (CARD_COUNT_IN_BYTES / 4096)
	int i, count = 0;
	unsigned char faulted [CARD_PAGES] = { 0 };
	mincore (cardtable, CARD_COUNT_IN_BYTES, faulted);

	for (i = 0; i < CARD_PAGES; ++i) {
		if (faulted [i])
			++count;
	}

	printf ("TOTAL card pages %d faulted %d\n", CARD_PAGES, count);
}
#endif

static void
card_table_collect_stats (gboolean beginning)
{
#ifdef CARD_TABLE_STATS
	int i;
	total_cards_remarked = total_bits_remarked = total_cards = mutator_cards = total_bits_mutator_free = 0;
	memset (bits_remarked, 0, sizeof (bits_remarked));

	major.iterate_live_block_ranges (count_marked_bits);
	los_iterate_live_block_ranges (count_marked_bits);

	printf ("CARD STATS %s : cards %d mutator cards %d remarked cards %d bits %d mf-bits %d [",
			beginning ? "START" : "END",
			total_cards, mutator_cards, total_cards_remarked, total_bits_remarked, total_bits_mutator_free);
	for (i = 0; i < REFINEMENT_BIT_COUNT; ++i)
		printf ("%d ", bits_remarked [i]);
	printf ("]\n");
#endif


#ifdef CARD_TABLE_PAGE_FAULT_STATS
	if (beginning)
		collect_faulted_cards ();
#endif
}

