catch2_dep = dependency('catch2')

core_tests = executable('core-tests',
  'main.cpp',
  'test_spsc_ring.cpp',
  'test_take_file.cpp',
  'test_peak_pyramid.cpp',
  'test_playback_feed.cpp',
  'test_render.cpp',
  'test_spectrogram.cpp',
  'test_tuning.cpp',
  dependencies: [core_dep, catch2_dep, dependency('threads')])

test('core', core_tests)
