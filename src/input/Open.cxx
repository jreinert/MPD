/*
 * Copyright 2003-2016 The Music Player Daemon Project
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
#include "InputStream.hxx"
#include "Registry.hxx"
#include "InputPlugin.hxx"
#include "LocalOpen.hxx"
#include "Domain.hxx"
#include "plugins/RewindInputPlugin.hxx"
#include "fs/Traits.hxx"
#include "fs/Path.hxx"
#include "fs/AllocatedPath.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"

InputStreamPtr
InputStream::Open(const char *url,
		  Mutex &mutex, Cond &cond,
		  Error &error)
{
	if (PathTraitsUTF8::IsAbsolute(url)) {
		const auto path = AllocatedPath::FromUTF8(url, error);
		if (path.IsNull())
			return nullptr;

		return OpenLocalInputStream(path,
					    mutex, cond, error);
	}

	input_plugins_for_each_enabled(plugin) {
		InputStream *is;

		is = plugin->open(url, mutex, cond, error);
		if (is != nullptr) {
			is = input_rewind_open(is);

			return InputStreamPtr(is);
		} else if (error.IsDefined())
			return nullptr;
	}

	error.Set(input_domain, "Unrecognized URI");
	return nullptr;
}

InputStreamPtr
InputStream::OpenReady(const char *uri,
		       Mutex &mutex, Cond &cond,
		       Error &error)
{
	auto is = Open(uri, mutex, cond, error);
	if (is == nullptr)
		return nullptr;

	mutex.lock();
	is->WaitReady();
	bool success = is->Check(error);
	mutex.unlock();

	if (!success)
		is.reset();

	return is;
}
