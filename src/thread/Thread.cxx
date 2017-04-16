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
#include "Thread.hxx"
#include "system/Error.hxx"

#ifdef ANDROID
#include "java/Global.hxx"
#endif

void
Thread::Start()
{
	assert(!IsDefined());

#ifdef WIN32
	handle = ::CreateThread(nullptr, 0, ThreadProc, this, 0, &id);
	if (handle == nullptr)
		throw MakeLastError("Failed to create thread");
#else
#ifndef NDEBUG
	creating = true;
#endif

	int e = pthread_create(&handle, nullptr, ThreadProc, this);

	if (e != 0) {
#ifndef NDEBUG
		creating = false;
#endif
		throw MakeErrno(e, "Failed to create thread");
	}

	defined = true;
#ifndef NDEBUG
	creating = false;
#endif
#endif
}

void
Thread::Join()
{
	assert(IsDefined());
	assert(!IsInside());

#ifdef WIN32
	::WaitForSingleObject(handle, INFINITE);
	::CloseHandle(handle);
	handle = nullptr;
#else
	pthread_join(handle, nullptr);
	defined = false;
#endif
}

inline void
Thread::Run()
{
#ifndef WIN32
#ifndef NDEBUG
	/* this works around a race condition that causes an assertion
	   failure due to IsInside() spuriously returning false right
	   after the thread has been created, and the calling thread
	   hasn't initialised "defined" yet */
	defined = true;
#endif
#endif

	f();

#ifdef ANDROID
	Java::DetachCurrentThread();
#endif
}

#ifdef WIN32

DWORD WINAPI
Thread::ThreadProc(LPVOID ctx)
{
	Thread &thread = *(Thread *)ctx;

	thread.Run();
	return 0;
}

#else

void *
Thread::ThreadProc(void *ctx)
{
	Thread &thread = *(Thread *)ctx;

	thread.Run();

	return nullptr;
}

#endif
