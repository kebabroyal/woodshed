core_inc = include_directories('include')
pipewire_dep = dependency('libpipewire-0.3')
fftw_dep = dependency('fftw3f')
rubberband_dep = dependency('rubberband')
sndfile_dep = dependency('sndfile')
json_dep = dependency('nlohmann_json')

core_lib = static_library('woodshed-core',
  'src/audio_take.cpp',
  'src/capture.cpp',
  'src/peak_pyramid.cpp',
  'src/playback_feed.cpp',
  'src/player.cpp',
  'src/pw_session.cpp',
  'src/render.cpp',
  'src/spectrogram.cpp',
  'src/stream_format.cpp',
  'src/take_file.cpp',
  'src/time_stretcher.cpp',
  'src/tuning.cpp',
  include_directories: core_inc,
  dependencies: [pipewire_dep, fftw_dep, rubberband_dep, sndfile_dep, json_dep],
  pic: true)

core_dep = declare_dependency(
  include_directories: core_inc,
  link_with: core_lib,
  dependencies: [pipewire_dep, fftw_dep, rubberband_dep, sndfile_dep, json_dep])

if get_option('tests')
  subdir('tests')
endif
