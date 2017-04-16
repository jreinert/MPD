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
#include "Mount.hxx"
#include "PrefixedLightSong.hxx"
#include "db/Selection.hxx"
#include "db/LightDirectory.hxx"
#include "db/Interface.hxx"
#include "fs/Traits.hxx"

#ifdef _LIBCPP_VERSION
/* workaround for "error: incomplete type 'PlaylistInfo' used in type
   trait expression" with libc++ version 3900 (from Android NDK
   r13b) */
#include "db/PlaylistInfo.hxx"
#endif

#include <string>

struct PrefixedLightDirectory : LightDirectory {
	std::string buffer;

	PrefixedLightDirectory(const LightDirectory &directory,
			       const char *base)
		:LightDirectory(directory),
		 buffer(IsRoot()
			? std::string(base)
			: PathTraitsUTF8::Build(base, uri)) {
		uri = buffer.c_str();
	}
};

static void
PrefixVisitDirectory(const char *base, const VisitDirectory &visit_directory,
		     const LightDirectory &directory)
{
	visit_directory(PrefixedLightDirectory(directory, base));
}

static void
PrefixVisitSong(const char *base, const VisitSong &visit_song,
		const LightSong &song)
{
	visit_song(PrefixedLightSong(song, base));
}

static void
PrefixVisitPlaylist(const char *base, const VisitPlaylist &visit_playlist,
		    const PlaylistInfo &playlist,
		    const LightDirectory &directory)
{
	visit_playlist(playlist,
		       PrefixedLightDirectory(directory, base));
}

void
WalkMount(const char *base, const Database &db,
	  bool recursive, const SongFilter *filter,
	  const VisitDirectory &visit_directory, const VisitSong &visit_song,
	  const VisitPlaylist &visit_playlist)
{
	using namespace std::placeholders;

	VisitDirectory vd;
	if (visit_directory)
		vd = std::bind(PrefixVisitDirectory,
			       base, std::ref(visit_directory), _1);

	VisitSong vs;
	if (visit_song)
		vs = std::bind(PrefixVisitSong,
			       base, std::ref(visit_song), _1);

	VisitPlaylist vp;
	if (visit_playlist)
		vp = std::bind(PrefixVisitPlaylist,
			       base, std::ref(visit_playlist), _1, _2);

	db.Visit(DatabaseSelection("", recursive, filter), vd, vs, vp);
}
