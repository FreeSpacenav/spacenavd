cc = meson.get_compiler('c')
conf_data = configuration_data()
has_x11 = get_option('x11')
has_hotplug = get_option('hotplug')
conf_data.set('USE_X11', has_x11)
conf_data.set('USE_NETLINK', has_hotplug)
conf_data.set_quoted('VERSION', meson.project_version())
conf_data.set_quoted('CFGDIR', '/etc')
conf_data.set('HAVE_ALLOCA_H', cc.has_header('alloca.h'))
conf_data.set('HAVE_MALLOC_H', cc.has_header('malloc.h'))

if (not has_x11)
  message('WARNING: you have disabled the X11 interface, the resulting daemon won\'t be compatible with applications written for the proprietary 3Dconnexion daemon (3dxserv)!')
else
  conf_data.set('HAVE_XINPUT2_H', cc.has_header('X11/extensions/XInput2.h'))
  has_xtest = cc.has_header('X11/extensions/XTest.h')
  conf_data.set('HAVE_XTEST_H', has_xtest)

  if (not has_xtest)
    message('WARNING: building without XTEST support, makes keyboard emulation less reliable (fallback to XSendEvent).')
  endif
endif

configure_file(output : 'config.h',
               configuration : conf_data)

executable('spacenavd',
           'proto_x11.c',
           'kbemu.c',
           'proto_unix.c',
           'hotplug_freebsd.c',
           'dev_usb.c',
           'xdetect_freebsd.c',
           'dev_usb_freebsd.c',
           'dev_usb_linux.c',
           'dev.c',
           'client.c',
           'dev_usb_darwin.c',
           'hotplug_darwin.c',
           'dummy_usb.c',
           'xdetect_linux.c',
           'spnavd.c',
           'cfgfile.c',
           'logger.c',
           'event.c',
           'hotplug_linux.c',
           'spnavd_win32.c',
           'dev_serial.c',
           dependencies: [
             dependency('x11'),
             dependency('xi'),
             dependency('xtst')
           ],
           install : true)
