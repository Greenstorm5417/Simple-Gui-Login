project('green-login', 'c', version: '0.2.0', license: 'MIT', meson_version: '>=1.1.0', default_options: [
  'c_std=c11', 'buildtype=minsize', 'warning_level=3', 'werror=true',
  'prefix=/usr/local', 'b_ndebug=if-release',
])
cc = meson.get_compiler('c')
python = import('python').find_installation('python3')
add_project_arguments('-D_GNU_SOURCE', '-D_FORTIFY_SOURCE=3', '-fstack-protector-strong', language: 'c')
add_project_link_arguments('-Wl,-z,relro,-z,now', language: 'c')
image = get_option('image')
if not image.startswith('/')
  image = meson.project_source_root() / image
endif
logo = custom_target('logo', input: image, output: 'logo.h',
  command: [python, files('tools/embed-image.py'), '@INPUT@', '@OUTPUT@'])
executable('green-login', ['main.c', 'platform.c', 'draw.c', 'session.c', logo],
  dependencies: cc.find_library('pam'), install: true)
