kind: stack

depends:

- qt5.bst
- test-infra.bst
- filtered.bst
- filtered-openh264.bst

# replaced (patched) elements:
- sdk/adwaita-icon-theme.bst
- sdk/apitrace.bst
- sdk/breakpad.bst
- sdk/capnp.bst
- sdk/cargo-c.bst
- sdk/eigen.bst
- sdk/enchant-2.bst
- sdk/flite.bst
- sdk/fmtlib.bst
- sdk/gi-docgen.bst
- sdk/graphviz.bst
- sdk/gst-plugin-audiofx.bst
- sdk/gst-plugin-closedcaption.bst
- sdk/gst-plugin-dav1d.bst
- sdk/gst-plugin-fmp4.bst
- sdk/gst-plugin-livesync.bst
- sdk/gst-plugin-rtp.bst
- sdk/gtk.bst
- sdk/hiredis.bst
- sdk/icecc.bst
- sdk/krb5.bst
- sdk/libavif.bst
- sdk/libbacktrace.bst
- sdk/libevent.bst
- sdk/libjxl.bst
- sdk/libmanette.bst
- sdk/libportal.bst
- sdk/libsecret.bst
- sdk/libspiel.bst
- sdk/libusrsctp.bst
- sdk/libwpe.bst
- sdk/mold.bst
- sdk/monado.bst
- sdk/openxr.bst
- sdk/os-release.bst
- sdk/patchelf.bst
- sdk/psmisc.bst
- sdk/ruby-json.bst
- sdk/ruby-highline.bst
- sdk/ruby-webrick.bst
- sdk/sccache.bst
- sdk/sparkle-cdm.bst
- sdk/svt-av1.bst
- sdk/sysprof-capture.bst
- sdk/unifdef.bst
- sdk/woff2.bst
- sdk/wpebackend-fdo.bst
- sdk/xdg-dbus-proxy.bst
- sdk/xorg-server.bst

- freedesktop-sdk.bst:components/alsa-lib.bst
- freedesktop-sdk.bst:components/alsa-plugins.bst
- freedesktop-sdk.bst:components/appstream-glib.bst
- freedesktop-sdk.bst:components/aspell.bst
- freedesktop-sdk.bst:components/at-spi2-core.bst
- freedesktop-sdk.bst:components/brotli.bst
- freedesktop-sdk.bst:components/bubblewrap.bst
- freedesktop-sdk.bst:components/ca-certificates.bst
- freedesktop-sdk.bst:components/cairo.bst
- freedesktop-sdk.bst:components/ccache.bst
- freedesktop-sdk.bst:components/cracklib.bst
- freedesktop-sdk.bst:components/cups.bst
- freedesktop-sdk.bst:components/curl.bst
- freedesktop-sdk.bst:components/cyrus-sasl.bst
- freedesktop-sdk.bst:components/dav1d.bst
- freedesktop-sdk.bst:components/dbus.bst
- freedesktop-sdk.bst:components/dejavu-fonts.bst
- freedesktop-sdk.bst:components/dictionaries.bst
- freedesktop-sdk.bst:components/dummy-gbm.bst
- freedesktop-sdk.bst:components/e2fsprogs.bst
- freedesktop-sdk.bst:components/expat.bst
- freedesktop-sdk.bst:components/ffmpeg.bst
- freedesktop-sdk.bst:components/fftw.bst
- freedesktop-sdk.bst:components/flatpak-xdg-utils.bst
- freedesktop-sdk.bst:components/flex.bst
- freedesktop-sdk.bst:components/fontconfig-flatpak-config.bst
- freedesktop-sdk.bst:components/fontconfig.bst
- freedesktop-sdk.bst:components/freetype.bst
- freedesktop-sdk.bst:components/frei0r.bst
- freedesktop-sdk.bst:components/fribidi.bst
- freedesktop-sdk.bst:components/gdbm.bst
- freedesktop-sdk.bst:components/gdk-pixbuf.bst
- freedesktop-sdk.bst:components/gettext.bst
- freedesktop-sdk.bst:components/giflib.bst
- freedesktop-sdk.bst:components/glib.bst
- freedesktop-sdk.bst:components/glib-networking.bst
- freedesktop-sdk.bst:components/gmp.bst
- freedesktop-sdk.bst:components/gnu-free-fonts.bst
- freedesktop-sdk.bst:components/gnupg.bst
- freedesktop-sdk.bst:components/gnutls.bst
- freedesktop-sdk.bst:components/google-crosextra-caladea.bst
- freedesktop-sdk.bst:components/google-crosextra-carlito.bst
- freedesktop-sdk.bst:components/gpgme.bst
- freedesktop-sdk.bst:components/graphite2.bst
- freedesktop-sdk.bst:components/gstreamer-libav.bst
- freedesktop-sdk.bst:components/gstreamer-plugins-bad.bst
- freedesktop-sdk.bst:components/gstreamer-plugins-base.bst
- freedesktop-sdk.bst:components/gstreamer-plugins-good.bst
- freedesktop-sdk.bst:components/gstreamer-plugins-ugly.bst
- freedesktop-sdk.bst:components/gstreamer.bst
- freedesktop-sdk.bst:components/gtk3.bst
- freedesktop-sdk.bst:components/gtk-doc.bst
- freedesktop-sdk.bst:components/gzip.bst
- freedesktop-sdk.bst:components/harfbuzz.bst
- freedesktop-sdk.bst:components/hicolor-icon-theme.bst
- freedesktop-sdk.bst:components/hunspell.bst
- freedesktop-sdk.bst:components/hyphen.bst
- freedesktop-sdk.bst:components/iana-config.bst
- freedesktop-sdk.bst:components/icu.bst
- freedesktop-sdk.bst:components/iso-codes.bst
- freedesktop-sdk.bst:components/jpeg.bst
- freedesktop-sdk.bst:components/ladspa-sdk.bst
- freedesktop-sdk.bst:components/lame.bst
- freedesktop-sdk.bst:components/lcms.bst
- freedesktop-sdk.bst:components/less.bst
- freedesktop-sdk.bst:components/libarchive.bst
- freedesktop-sdk.bst:components/libassuan.bst
- freedesktop-sdk.bst:components/libcap.bst
- freedesktop-sdk.bst:components/libcap-ng.bst
- freedesktop-sdk.bst:components/libdatrie.bst
- freedesktop-sdk.bst:components/libdrm.bst
- freedesktop-sdk.bst:components/libepoxy.bst
- freedesktop-sdk.bst:components/liberation-fonts.bst
- freedesktop-sdk.bst:components/libexif.bst
- freedesktop-sdk.bst:components/libffi.bst
- freedesktop-sdk.bst:components/libflac.bst
- freedesktop-sdk.bst:components/libgcrypt.bst
- freedesktop-sdk.bst:components/libglvnd.bst
- freedesktop-sdk.bst:components/libgpg-error.bst
- freedesktop-sdk.bst:components/libidn2.bst
- freedesktop-sdk.bst:components/libksba.bst
- freedesktop-sdk.bst:components/libnghttp2.bst
- freedesktop-sdk.bst:components/libnice.bst
- freedesktop-sdk.bst:components/libogg.bst
- freedesktop-sdk.bst:components/libpng.bst
- freedesktop-sdk.bst:components/libproxy.bst
- freedesktop-sdk.bst:components/libpsl.bst
- freedesktop-sdk.bst:components/libpulse.bst
- freedesktop-sdk.bst:components/libsamplerate.bst
- freedesktop-sdk.bst:components/libseccomp.bst
- freedesktop-sdk.bst:components/libsoup.bst
- freedesktop-sdk.bst:components/libsoup2.bst
- freedesktop-sdk.bst:components/libsrtp2.bst
- freedesktop-sdk.bst:components/libtasn1.bst
- freedesktop-sdk.bst:components/libthai.bst
- freedesktop-sdk.bst:components/libtheora.bst
- freedesktop-sdk.bst:components/libtiff.bst
- freedesktop-sdk.bst:components/libtool.bst
- freedesktop-sdk.bst:components/libunistring.bst
- freedesktop-sdk.bst:components/libva.bst
- freedesktop-sdk.bst:components/libvorbis.bst
- freedesktop-sdk.bst:components/libvpx.bst
- freedesktop-sdk.bst:components/libwebp.bst
- freedesktop-sdk.bst:components/libxkbcommon.bst
- freedesktop-sdk.bst:components/libxml2.bst
- freedesktop-sdk.bst:components/libxslt.bst
- freedesktop-sdk.bst:components/llvm.bst
- freedesktop-sdk.bst:components/locales.bst
- freedesktop-sdk.bst:components/lzip.bst
- freedesktop-sdk.bst:components/mesa-headers.bst
- freedesktop-sdk.bst:components/mpg123.bst
- freedesktop-sdk.bst:components/mythes.bst
- freedesktop-sdk.bst:components/nano.bst
- freedesktop-sdk.bst:components/nettle.bst
- freedesktop-sdk.bst:components/noto-emoji.bst
- freedesktop-sdk.bst:components/npth.bst
- freedesktop-sdk.bst:components/nspr.bst
- freedesktop-sdk.bst:components/nss.bst
- freedesktop-sdk.bst:components/nsswitch-config.bst
- freedesktop-sdk.bst:components/openal.bst
- freedesktop-sdk.bst:components/opencl.bst
- freedesktop-sdk.bst:components/openjpeg.bst
- freedesktop-sdk.bst:components/openssh.bst
- freedesktop-sdk.bst:components/openssl.bst
- freedesktop-sdk.bst:components/opus.bst
- freedesktop-sdk.bst:components/orc.bst
- freedesktop-sdk.bst:components/p11-kit.bst
- freedesktop-sdk.bst:components/pango.bst
- freedesktop-sdk.bst:components/pciutils.bst
- freedesktop-sdk.bst:components/perf.bst
- freedesktop-sdk.bst:components/pipewire.bst
- freedesktop-sdk.bst:components/pixman.bst
- freedesktop-sdk.bst:components/procps.bst
- freedesktop-sdk.bst:components/pycairo.bst
- freedesktop-sdk.bst:components/pygobject.bst
- freedesktop-sdk.bst:components/python3-pip.bst
- freedesktop-sdk.bst:components/python3-setuptools.bst
- freedesktop-sdk.bst:components/python3-sitecustomize.bst
- freedesktop-sdk.bst:components/python3-six.bst
- freedesktop-sdk.bst:components/python3-virtualenv.bst
- freedesktop-sdk.bst:components/python3.bst
- freedesktop-sdk.bst:components/shared-mime-info.bst
- freedesktop-sdk.bst:components/sndfile.bst
- freedesktop-sdk.bst:components/speex.bst
- freedesktop-sdk.bst:components/speexdsp.bst
- freedesktop-sdk.bst:components/sqlite.bst
- freedesktop-sdk.bst:components/systemd-libs.bst
- freedesktop-sdk.bst:components/tar.bst
- freedesktop-sdk.bst:components/tzdata.bst
- freedesktop-sdk.bst:components/unzip.bst
- freedesktop-sdk.bst:components/util-linux.bst
- freedesktop-sdk.bst:components/v4l-utils.bst
- freedesktop-sdk.bst:components/vulkan-headers.bst
- freedesktop-sdk.bst:components/wayland.bst
- freedesktop-sdk.bst:components/weston.bst
- freedesktop-sdk.bst:components/which.bst
- freedesktop-sdk.bst:components/xcb-util-cursor.bst
- freedesktop-sdk.bst:components/xcb-util-image.bst
- freedesktop-sdk.bst:components/xcb-util-keysyms.bst
- freedesktop-sdk.bst:components/xcb-util-renderutil.bst
- freedesktop-sdk.bst:components/xcb-util-wm.bst
- freedesktop-sdk.bst:components/xcb-util.bst
- freedesktop-sdk.bst:components/xdg-user-dirs.bst
- freedesktop-sdk.bst:components/xorg-app-xkbcomp.bst
- freedesktop-sdk.bst:components/xorg-data-xkeyboard-config.bst
- freedesktop-sdk.bst:components/xorg-lib-ice.bst
- freedesktop-sdk.bst:components/xorg-lib-pciaccess.bst
- freedesktop-sdk.bst:components/xorg-lib-sm.bst
- freedesktop-sdk.bst:components/xorg-lib-x11.bst
- freedesktop-sdk.bst:components/xorg-lib-xau.bst
- freedesktop-sdk.bst:components/xorg-lib-xcb.bst
- freedesktop-sdk.bst:components/xorg-lib-xcomposite.bst
- freedesktop-sdk.bst:components/xorg-lib-xcursor.bst
- freedesktop-sdk.bst:components/xorg-lib-xdamage.bst
- freedesktop-sdk.bst:components/xorg-lib-xdmcp.bst
- freedesktop-sdk.bst:components/xorg-lib-xext.bst
- freedesktop-sdk.bst:components/xorg-lib-xfixes.bst
- freedesktop-sdk.bst:components/xorg-lib-xft.bst
- freedesktop-sdk.bst:components/xorg-lib-xi.bst
- freedesktop-sdk.bst:components/xorg-lib-xinerama.bst
- freedesktop-sdk.bst:components/xorg-lib-xkbfile.bst
- freedesktop-sdk.bst:components/xorg-lib-xpm.bst
- freedesktop-sdk.bst:components/xorg-lib-xrandr.bst
- freedesktop-sdk.bst:components/xorg-lib-xrender.bst
- freedesktop-sdk.bst:components/xorg-lib-xscrnsaver.bst
- freedesktop-sdk.bst:components/xorg-lib-xshmfence.bst
- freedesktop-sdk.bst:components/xorg-lib-xt.bst
- freedesktop-sdk.bst:components/xorg-lib-xtrans.bst
- freedesktop-sdk.bst:components/xorg-lib-xtst.bst
- freedesktop-sdk.bst:components/xorg-lib-xv.bst
- freedesktop-sdk.bst:components/xorg-lib-xxf86vm.bst
- freedesktop-sdk.bst:components/zip.bst
- freedesktop-sdk.bst:components/zstd.bst
- freedesktop-sdk.bst:integration/mtab.bst
- freedesktop-sdk.bst:components/x265.bst
- freedesktop-sdk.bst:components/x265-10bits.bst
- freedesktop-sdk.bst:components/x265-12bits.bst
