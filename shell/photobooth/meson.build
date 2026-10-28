# Photo Booth (Apple menu): a live picture from the built-in camera or a
# connected webcam, and a button to take a photo. GStreamer drives the
# camera (v4l2src + decodebin, so raw and MJPEG cameras both work) and
# hands frames to the window as RGB.
gst = dependency('gstreamer-1.0')
gstapp = dependency('gstreamer-app-1.0')
gstvideo = dependency('gstreamer-video-1.0')

executable('zacos9-photobooth',
  'main.cpp',
  'photoboothwindow.cpp',
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, gst, gstapp, gstvideo],
  install: true,
)
