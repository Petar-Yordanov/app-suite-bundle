#pragma once
#if defined(_MSC_VER)
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif
#include <vlc/vlc.h>
#include <QLibrary>
#include <QString>

// Type-safe runtime resolution against the official libVLC 3 headers. No
// compiler-dependent import-library format and no private libVLC ABI.
#define VLC_API_SYMBOLS(X) \
    X(libvlc_get_version) X(libvlc_new) X(libvlc_release) X(libvlc_errmsg) \
    X(libvlc_media_new_path) X(libvlc_media_add_option) X(libvlc_media_release) \
    X(libvlc_media_parse_with_options) X(libvlc_media_parse_stop) X(libvlc_media_get_parsed_status) \
    X(libvlc_media_tracks_get) X(libvlc_media_tracks_release) X(libvlc_media_get_duration) X(libvlc_media_get_stats) X(libvlc_media_get_type) \
    X(libvlc_media_player_new) X(libvlc_media_player_release) X(libvlc_media_player_set_media) \
    X(libvlc_media_player_play) X(libvlc_media_player_set_pause) X(libvlc_media_player_stop) \
    X(libvlc_media_player_get_state) X(libvlc_media_player_get_time) X(libvlc_media_player_set_time) \
    X(libvlc_media_player_get_length) X(libvlc_media_player_is_seekable) X(libvlc_media_player_has_vout) \
    X(libvlc_audio_set_volume) X(libvlc_audio_set_mute) X(libvlc_audio_get_track) X(libvlc_audio_set_track) \
    X(libvlc_audio_get_track_description) X(libvlc_video_get_spu) X(libvlc_video_set_spu) \
    X(libvlc_video_get_spu_description) X(libvlc_track_description_list_release) \
    X(libvlc_media_player_add_slave) X(libvlc_video_set_aspect_ratio) X(libvlc_video_set_key_input) X(libvlc_video_set_mouse_input) \
    X(libvlc_video_set_callbacks) X(libvlc_video_set_format_callbacks)
#if defined(Q_OS_WIN)
#define VLC_PLATFORM_SYMBOLS(X) X(libvlc_media_player_set_hwnd)
#elif defined(Q_OS_MACOS)
#define VLC_PLATFORM_SYMBOLS(X) X(libvlc_media_player_set_nsobject)
#else
#define VLC_PLATFORM_SYMBOLS(X) X(libvlc_media_player_set_xwindow)
#endif

class VlcApi {
public:
    bool load(QString& error);
    QString version() const;
    QString lastError() const;
    QString path() const{return library_.fileName();}
#define DECLARE(name) decltype(&::name) name=nullptr;
    VLC_API_SYMBOLS(DECLARE)
    VLC_PLATFORM_SYMBOLS(DECLARE)
#undef DECLARE
private:
    QLibrary library_;
    bool ready_=false;
};
