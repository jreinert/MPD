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
#include "DatabasePrint.hxx"
#include "Selection.hxx"
#include "SongFilter.hxx"
#include "SongPrint.hxx"
#include "DetachedSong.hxx"
#include "TimePrint.hxx"
#include "TagPrint.hxx"
#include "client/Response.hxx"
#include "Partition.hxx"
#include "tag/Tag.hxx"
#include "tag/Mask.hxx"
#include "LightSong.hxx"
#include "LightDirectory.hxx"
#include "PlaylistInfo.hxx"
#include "Interface.hxx"
#include "fs/Traits.hxx"

#include <functional>

static const char *
ApplyBaseFlag(const char *uri, bool base)
{
	if (base)
		uri = PathTraitsUTF8::GetBase(uri);
	return uri;
}

static void
PrintDirectoryURI(Response &r, bool base, const LightDirectory &directory)
{
	r.Format("directory: %s\n",
		 ApplyBaseFlag(directory.GetPath(), base));
}

static void
PrintDirectoryBrief(Response &r, bool base, const LightDirectory &directory)
{
	if (!directory.IsRoot())
		PrintDirectoryURI(r, base, directory);
}

static void
PrintDirectoryFull(Response &r, bool base, const LightDirectory &directory)
{
	if (!directory.IsRoot()) {
		PrintDirectoryURI(r, base, directory);

		if (directory.mtime > 0)
			time_print(r, "Last-Modified", directory.mtime);
	}
}

static void
print_playlist_in_directory(Response &r, bool base,
			    const char *directory,
			    const char *name_utf8)
{
	if (base || directory == nullptr)
		r.Format("playlist: %s\n",
			 ApplyBaseFlag(name_utf8, base));
	else
		r.Format("playlist: %s/%s\n",
			 directory, name_utf8);
}

static void
print_playlist_in_directory(Response &r, bool base,
			    const LightDirectory *directory,
			    const char *name_utf8)
{
	if (base || directory == nullptr || directory->IsRoot())
		r.Format("playlist: %s\n", name_utf8);
	else
		r.Format("playlist: %s/%s\n",
			 directory->GetPath(), name_utf8);
}

static void
PrintSongBrief(Response &r, bool base, const LightSong &song)
{
	song_print_uri(r, song, base);

	if (song.tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(r, base,
					    song.directory, song.uri);
}

static void
PrintSongFull(Response &r, bool base, const LightSong &song)
{
	song_print_info(r, song, base);

	if (song.tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(r, base,
					    song.directory, song.uri);
}

static void
PrintPlaylistBrief(Response &r, bool base,
		   const PlaylistInfo &playlist,
		   const LightDirectory &directory)
{
	print_playlist_in_directory(r, base,
				    &directory, playlist.name.c_str());
}

static void
PrintPlaylistFull(Response &r, bool base,
		  const PlaylistInfo &playlist,
		  const LightDirectory &directory)
{
	print_playlist_in_directory(r, base,
				    &directory, playlist.name.c_str());

	if (playlist.mtime > 0)
		time_print(r, "Last-Modified", playlist.mtime);
}

static bool
CompareNumeric(const char *a, const char *b)
{
	long a_value = strtol(a, nullptr, 10);
	long b_value = strtol(b, nullptr, 10);

	return a_value < b_value;
}

static bool
CompareTags(TagType type, const Tag &a, const Tag &b)
{
	const char *a_value = a.GetSortValue(type);
	const char *b_value = b.GetSortValue(type);

	switch (type) {
	case TAG_DISC:
	case TAG_TRACK:
		return CompareNumeric(a_value, b_value);

	default:
		return strcmp(a_value, b_value) < 0;
	}
}

void
db_selection_print(Response &r, Partition &partition,
		   const DatabaseSelection &selection,
		   bool full, bool base,
		   TagType sort,
		   unsigned window_start, unsigned window_end)
{
	const Database &db = partition.GetDatabaseOrThrow();

	unsigned i = 0;

	using namespace std::placeholders;
	const auto d = selection.filter == nullptr
		? std::bind(full ? PrintDirectoryFull : PrintDirectoryBrief,
			    std::ref(r), base, _1)
		: VisitDirectory();
	VisitSong s = std::bind(full ? PrintSongFull : PrintSongBrief,
				std::ref(r), base, _1);
	const auto p = selection.filter == nullptr
		? std::bind(full ? PrintPlaylistFull : PrintPlaylistBrief,
			    std::ref(r), base, _1, _2)
		: VisitPlaylist();

	if (sort == TAG_NUM_OF_ITEM_TYPES) {
		if (window_start > 0 ||
		    window_end < (unsigned)std::numeric_limits<int>::max())
			s = [s, window_start, window_end, &i](const LightSong &song){
				const bool in_window = i >= window_start && i < window_end;
				++i;
				if (in_window)
					s(song);
			};

		db.Visit(selection, d, s, p);
	} else {
		// TODO: allow the database plugin to sort internally

		/* the client has asked us to sort the result; this is
		   pretty expensive, because instead of streaming the
		   result to the client, we need to copy it all into
		   this std::vector, and then sort it */
		std::vector<DetachedSong> songs;

		{
			auto collect_songs = [&songs](const LightSong &song){
				songs.emplace_back(song);
			};

			db.Visit(selection, d, collect_songs, p);
		}

		std::stable_sort(songs.begin(), songs.end(),
				 [sort](const DetachedSong &a, const DetachedSong &b){
					 return CompareTags(sort, a.GetTag(),
							    b.GetTag());
				 });

		if (window_end < songs.size())
			songs.erase(std::next(songs.begin(), window_end),
				    songs.end());

		if (window_start >= songs.size())
			return;

		songs.erase(songs.begin(),
			    std::next(songs.begin(), window_start));

		for (const auto &song : songs)
			s((LightSong)song);
	}
}

void
db_selection_print(Response &r, Partition &partition,
		   const DatabaseSelection &selection,
		   bool full, bool base)
{
	db_selection_print(r, partition, selection, full, base,
			   TAG_NUM_OF_ITEM_TYPES,
			   0, std::numeric_limits<int>::max());
}

static void
PrintSongURIVisitor(Response &r, const LightSong &song)
{
	song_print_uri(r, song);
}

static void
PrintUniqueTag(Response &r, TagType tag_type,
	       const Tag &tag)
{
	const char *value = tag.GetValue(tag_type);
	assert(value != nullptr);
	tag_print(r, tag_type, value);

	const auto tag_mask = r.GetTagMask();
	for (const auto &item : tag)
		if (item.type != tag_type && tag_mask.Test(item.type))
			tag_print(r, item.type, item.value);
}

void
PrintUniqueTags(Response &r, Partition &partition,
		unsigned type, TagMask group_mask,
		const SongFilter *filter)
{
	const Database &db = partition.GetDatabaseOrThrow();

	const DatabaseSelection selection("", true, filter);

	if (type == LOCATE_TAG_FILE_TYPE) {
		using namespace std::placeholders;
		const auto f = std::bind(PrintSongURIVisitor,
					 std::ref(r), _1);
		db.Visit(selection, f);
	} else {
		assert(type < TAG_NUM_OF_ITEM_TYPES);

		using namespace std::placeholders;
		const auto f = std::bind(PrintUniqueTag, std::ref(r),
					 (TagType)type, _1);
		db.VisitUniqueTags(selection, (TagType)type,
				   group_mask, f);
	}
}
