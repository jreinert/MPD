/*
 * Copyright (C) 2016 The Music Player Daemon Project
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

#include <errno.h>
#include <sndio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "SndioOutputPlugin.hxx"
#include "config/ConfigError.hxx"
#include "../OutputAPI.hxx"
#include "../Wrapper.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"

#ifndef SIO_DEVANY
/* this macro is missing in libroar-dev 1.0~beta2-3 (Debian Wheezy) */
#define SIO_DEVANY "default"
#endif

class SndioOutput {
	friend struct AudioOutputWrapper<SndioOutput>;
	AudioOutput base;
	const char *device;
	struct sio_hdl *sio_hdl;

public:
	SndioOutput()
		:base(sndio_output_plugin) {}
	~SndioOutput() {}

	bool Configure(const ConfigBlock &block, Error &error);

	static SndioOutput *Create(const ConfigBlock &block, Error &error);

	bool Open(AudioFormat &audio_format, Error &error);
	void Close();
	unsigned Delay() const;
	size_t Play(const void *chunk, size_t size, Error &error);
	void Cancel();
};

static constexpr Domain sndio_output_domain("sndio_output");

bool
SndioOutput::Configure(const ConfigBlock &block, Error &error)
{
	if (!base.Configure(block, error))
		return false;
	device = block.GetBlockValue("device", SIO_DEVANY);
	return true;
}

SndioOutput *
SndioOutput::Create(const ConfigBlock &block, Error &error)
{
	SndioOutput *ao = new SndioOutput();

	if (!ao->Configure(block, error)) {
		delete ao;
		return nullptr;
	}

	return ao;
}

static bool
sndio_test_default_device()
{
	struct sio_hdl *sio_hdl;

	sio_hdl = sio_open(SIO_DEVANY, SIO_PLAY, 0);
	if (!sio_hdl) {
		FormatError(sndio_output_domain,
		            "Error opening default sndio device");
		return false;
	}

	sio_close(sio_hdl);
	return true;
}

bool
SndioOutput::Open(AudioFormat &audio_format, gcc_unused Error &error)
{
	struct sio_par par;
	unsigned bits, rate, chans;

	sio_hdl = sio_open(device, SIO_PLAY, 0);
	if (!sio_hdl) {
		error.Format(sndio_output_domain, -1,
		             "Failed to open default sndio device");
		return false;
	}

	switch (audio_format.format) {
	case SampleFormat::S16:
		bits = 16;
		break;
	case SampleFormat::S32:
		bits = 32;
		break;
	default:
		audio_format.format = SampleFormat::S16;
		bits = 16;
		break;
	}

	rate = audio_format.sample_rate;
	chans = audio_format.channels;

	sio_initpar(&par);
	par.bits = bits;
	par.rate = rate;
	par.pchan = chans;
	par.sig = 1;
	par.le = SIO_LE_NATIVE;

	if (!sio_setpar(sio_hdl, &par) ||
	    !sio_getpar(sio_hdl, &par)) {
		error.Format(sndio_output_domain, -1,
		             "Failed to set/get audio params");
		goto err;
	}

	if (par.bits != bits ||
	    par.rate < rate * 995 / 1000 ||
	    par.rate > rate * 1005 / 1000 ||
	    par.pchan != chans ||
	    par.sig != 1 ||
	    par.le != SIO_LE_NATIVE) {
		error.Format(sndio_output_domain, -1,
		             "Requested audio params cannot be satisfied");
		goto err;
	}

	if (!sio_start(sio_hdl)) {
		error.Format(sndio_output_domain, -1,
		             "Failed to start audio device");
		goto err;
	}

	return true;
err:
	sio_close(sio_hdl);
	return false;
}

void
SndioOutput::Close()
{
	sio_close(sio_hdl);
}

size_t
SndioOutput::Play(const void *chunk, size_t size, Error &error)
{
	ssize_t n;

	while (1) {
		n = sio_write(sio_hdl, chunk, size);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			error.FormatErrno("Failed to write %zu bytes to sndio output", size);
			return 0;
		}
		return n;
	}
}

typedef AudioOutputWrapper<SndioOutput> Wrapper;

const struct AudioOutputPlugin sndio_output_plugin = {
	"sndio",
	sndio_test_default_device,
	&Wrapper::Init,
	&Wrapper::Finish,
	nullptr,
	nullptr,
	&Wrapper::Open,
	&Wrapper::Close,
	nullptr,
	nullptr,
	&Wrapper::Play,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};