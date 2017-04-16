/*
 * Copyright 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "Tag.hxx"
#include "Pool.hxx"
#include "Builder.hxx"

#include <assert.h>

void
Tag::Clear()
{
	duration = SignedSongTime::Negative();
	has_playlist = false;

	tag_pool_lock.lock();
	for (unsigned i = 0; i < num_items; ++i)
		tag_pool_put_item(items[i]);
	tag_pool_lock.unlock();

	delete[] items;
	items = nullptr;
	num_items = 0;
}

Tag::Tag(const Tag &other)
	:duration(other.duration), has_playlist(other.has_playlist),
	 num_items(other.num_items),
	 items(nullptr)
{
	if (num_items > 0) {
		items = new TagItem *[num_items];

		tag_pool_lock.lock();
		for (unsigned i = 0; i < num_items; i++)
			items[i] = tag_pool_dup_item(other.items[i]);
		tag_pool_lock.unlock();
	}
}

Tag *
Tag::Merge(const Tag &base, const Tag &add)
{
	TagBuilder builder(add);
	builder.Complement(base);
	return builder.CommitNew();
}

Tag *
Tag::MergeReplace(Tag *base, Tag *add)
{
	if (add == nullptr)
		return base;

	if (base == nullptr)
		return add;

	Tag *tag = Merge(*base, *add);
	delete base;
	delete add;

	return tag;
}

const char *
Tag::GetValue(TagType type) const
{
	assert(type < TAG_NUM_OF_ITEM_TYPES);

	for (const auto &item : *this)
		if (item.type == type)
			return item.value;

	return nullptr;
}

bool
Tag::HasType(TagType type) const
{
	return GetValue(type) != nullptr;
}

static TagType
DecaySort(TagType type)
{
	switch (type) {
	case TAG_ARTIST_SORT:
		return TAG_ARTIST;

	case TAG_ALBUM_SORT:
		return TAG_ALBUM;

	case TAG_ALBUM_ARTIST_SORT:
		return TAG_ALBUM_ARTIST;

	default:
		return TAG_NUM_OF_ITEM_TYPES;
	}
}

static TagType
Fallback(TagType type)
{
	switch (type) {
	case TAG_ALBUM_ARTIST:
		return TAG_ARTIST;

	case TAG_MUSICBRAINZ_ALBUMARTISTID:
		return TAG_MUSICBRAINZ_ARTISTID;

	default:
		return TAG_NUM_OF_ITEM_TYPES;
	}
}

const char *
Tag::GetSortValue(TagType type) const
{
	const char *value = GetValue(type);
	if (value != nullptr)
		return value;

	/* try without *_SORT */
	const auto no_sort_type = DecaySort(type);
	if (no_sort_type != TAG_NUM_OF_ITEM_TYPES) {
		value = GetValue(no_sort_type);
		if (value != nullptr)
			return value;
	}

	/* fall back from TAG_ALBUM_ARTIST to TAG_ALBUM */

	type = Fallback(type);
	if (type != TAG_NUM_OF_ITEM_TYPES)
		return GetSortValue(type);

	if (no_sort_type != TAG_NUM_OF_ITEM_TYPES) {
		type = Fallback(no_sort_type);
		if (type != TAG_NUM_OF_ITEM_TYPES)
			return GetSortValue(type);
	}

	/* finally fall back to empty string */

	return "";
}
