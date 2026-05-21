#pragma once

#include <vlc/vlc.h>
#include <memory>

// -----------------------------------------------------------------------------
// RAII wrappers for libvlc handles.
//
// Each typedef is a std::unique_ptr with a stateless deleter that calls the
// matching libvlc *_release function. The two player deleters also stop
// playback first, since libvlc requires a clean stop before release.
// -----------------------------------------------------------------------------

struct VlcInstanceDeleter
{
	void operator()(libvlc_instance_t* p) const noexcept
	{
		if (p) libvlc_release(p);
	}
};

struct VlcMediaDeleter
{
	void operator()(libvlc_media_t* p) const noexcept
	{
		if (p) libvlc_media_release(p);
	}
};

struct VlcMediaListDeleter
{
	void operator()(libvlc_media_list_t* p) const noexcept
	{
		if (p) libvlc_media_list_release(p);
	}
};

struct VlcMediaPlayerDeleter
{
	void operator()(libvlc_media_player_t* p) const noexcept
	{
		if (p)
		{
			libvlc_media_player_stop(p);
			libvlc_media_player_release(p);
		}
	}
};

struct VlcMediaListPlayerDeleter
{
	void operator()(libvlc_media_list_player_t* p) const noexcept
	{
		if (p)
		{
			libvlc_media_list_player_stop(p);
			libvlc_media_list_player_release(p);
		}
	}
};

using VlcInstance        = std::unique_ptr<libvlc_instance_t,          VlcInstanceDeleter>;
using VlcMedia           = std::unique_ptr<libvlc_media_t,             VlcMediaDeleter>;
using VlcMediaList       = std::unique_ptr<libvlc_media_list_t,        VlcMediaListDeleter>;
using VlcMediaPlayer     = std::unique_ptr<libvlc_media_player_t,      VlcMediaPlayerDeleter>;
using VlcMediaListPlayer = std::unique_ptr<libvlc_media_list_player_t, VlcMediaListPlayerDeleter>;
