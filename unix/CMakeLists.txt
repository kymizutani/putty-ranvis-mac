set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  utils/arm_arch_queries.c
  utils/block_signal.c
  utils/cloexec.c
  utils/cmdline_arg.c
  utils/dputs.c
  utils/filename.c
  utils/fontspec.c
  utils/getticks.c
  utils/get_username.c
  utils/keysym_to_unicode.c
  utils/make_dir_and_check_ours.c
  utils/make_dir_path.c
  utils/make_spr_sw_abort_errno.c
  utils/nonblock.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints.c
  utils/pollwrap.c
  utils/signal.c
  utils/sleep_ticks.c
  utils/subprocess_waiter.c
  utils/x11_ignore_error.c
  # Language-file support: every program built for this platform may
  # translate its user-interface strings, so this lives in the library
  # that everything links against.
  uxl10n.c
  # Finding the data files that were embedded in a macOS .app bundle,
  # from wherever the program happens to have been started.
  utils/macos_bundle_env.c
  # We want the ISO C implementation of ltime(), because we don't have
  # a local better alternative
  ../utils/ltime.c)
# Compiled icon pixmap files
add_library(puttyxpms STATIC
  putty-xpm.c
  putty-rgb.c
  putty-config-xpm.c
  putty-config-rgb.c)
add_library(ptermxpms STATIC
  pterm-xpm.c
  pterm-rgb.c
  pterm-config-xpm.c
  pterm-config-rgb.c)

# Language files, used by uxl10n.c to translate the user interface.
# These are the same files that the Windows build installs.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/windows/lang/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/putty/lang)
add_sources_from_current_dir(eventloop
  cliloop.c uxsel.c)
add_sources_from_current_dir(console
  console.c)
add_sources_from_current_dir(settings
  storage.c)
add_sources_from_current_dir(network
  network.c fd-socket.c agent-socket.c peerinfo.c local-proxy.c x11.c)
add_sources_from_current_dir(sshcommon
  noise.c)
add_sources_from_current_dir(sshclient
  gss.c agent-client.c sharing.c)
add_sources_from_current_dir(sshserver
  sftpserver.c procnet.c)
add_sources_from_current_dir(sftpclient
  sftp.c)
add_sources_from_current_dir(otherbackends
  serial.c)
add_sources_from_current_dir(agent
  agent-client.c)

add_executable(fuzzterm
  ${CMAKE_SOURCE_DIR}/test/fuzzterm.c
  ${CMAKE_SOURCE_DIR}/stubs/no-print.c
  unicode.c
  no-gtk.c
  $<TARGET_OBJECTS:logging>)
be_list(fuzzterm FuZZterm)
add_dependencies(fuzzterm generated_licence_h)
target_link_libraries(fuzzterm
  guiterminal eventloop charset settings utils)

add_executable(osxlaunch
  osxlaunch.c)

add_sources_from_current_dir(plink unicode.c no-gtk.c)
add_sources_from_current_dir(pscp unicode.c no-gtk.c)
add_sources_from_current_dir(psftp unicode.c no-gtk.c)
add_sources_from_current_dir(psocks no-gtk.c)

add_executable(psusan
  psusan.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  unicode.c
  no-gtk.c
  pty.c)
be_list(psusan psusan)
target_link_libraries(psusan
  eventloop sshserver keygen settings network crypto utils iso2022)
installed_program(psusan)

add_library(puttygen-common OBJECT
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  keygen-noise.c
  no-gtk.c
  noise.c
  storage.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c)

add_executable(puttygen
  ${CMAKE_SOURCE_DIR}/cmdgen.c
  $<TARGET_OBJECTS:puttygen-common>)
target_link_libraries(puttygen keygen console crypto utils)
installed_program(puttygen)

if(GTK_FOUND)
  # The window-based form of the key generator: same engine, Gtk front
  # end. This is the program that goes inside PuTTYgen.app, because
  # unlike the command-line one it's something you can double-click.
  #
  # It doesn't use the "puttygen-common" object library, because that
  # includes the stubs that stand in for Gtk in a command-line program.
  add_executable(puttygen-gtk
    gtkgen.c
    ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
    keygen-noise.c
    noise.c
    storage.c
    ${CMAKE_SOURCE_DIR}/sshpubk.c
    ${CMAKE_SOURCE_DIR}/sshrand.c)
  target_link_libraries(puttygen-gtk
    keygen crypto utils settings eventloop
    ${GTK_LIBRARIES} ${platform_libraries})
  installed_program(puttygen-gtk)
endif()

add_executable(cgtest
  ${CMAKE_SOURCE_DIR}/cgtest.c
  $<TARGET_OBJECTS:puttygen-common>)
target_link_libraries(cgtest keygen console crypto utils)

if(HAVE_ALIGNED_ALLOC)
  add_library(overaligned_alloc OBJECT
    ${CMAKE_SOURCE_DIR}/utils/memory.c)
  target_compile_definitions(overaligned_alloc PRIVATE ALLOCATION_ALIGNMENT=128)
  add_executable(testsc
    ${CMAKE_SOURCE_DIR}/test/testsc.c
    ${CMAKE_SOURCE_DIR}/test/test_random.c
    $<TARGET_OBJECTS:overaligned_alloc>)
  target_link_libraries(testsc keygen crypto utils)
endif()

add_executable(testzlib
  ${CMAKE_SOURCE_DIR}/test/testzlib.c
  ${CMAKE_SOURCE_DIR}/ssh/zlib.c)
target_link_libraries(testzlib utils)

add_executable(uppity
  uppity.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  no-gtk.c
  pty.c
  unicode.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c)
be_list(uppity Uppity)
target_link_libraries(uppity
  eventloop sshserver keygen settings network crypto utils iso2022)

if(GTK_FOUND)
  add_sources_from_current_dir(utils
    utils/align_label_left.c
    utils/buildinfo_gtk_version.c
    utils/get_label_text_dimensions.c
    utils/get_x11_display.c
    utils/our_dialog.c
    utils/string_width.c
    columns.c)
  add_sources_from_current_dir(guiterminal
    window.c unifont.c dialog.c config-gtk.c gtk-common.c config-unix.c unicode.c printing.c)
  add_dependencies(guiterminal generated_licence_h) # dialog.c uses licence.h
  # unix/unicode.c can invoke the ISO-2022 codec, which the build keeps
  # in a library of its own, so everything that links the terminal
  # widget has to be linked against it too.
  target_link_libraries(guiterminal iso2022)

  add_executable(pterm
    pterm.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
    pty.c)
  be_list(pterm pterm)
  target_link_libraries(pterm
    guiterminal eventloop settings utils ptermxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  installed_program(pterm)

  if(GTK_VERSION GREATER_EQUAL 3)
    add_executable(ptermapp
      pterm.c
      main-gtk-application.c
      ${CMAKE_SOURCE_DIR}/stubs/no-cmdline.c
      ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
      ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
      ${CMAKE_SOURCE_DIR}/stubs/no-console.c
      ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
      ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
      pty.c)
    be_list(ptermapp pterm)
    target_link_libraries(ptermapp
      guiterminal eventloop settings utils ptermxpms
      ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  endif()

  add_executable(putty
    putty.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
  be_list(putty PuTTY SSH SERIAL OTHERBACKENDS)
  target_link_libraries(putty
    guiterminal eventloop sshclient otherbackends settings
    network crypto utils puttyxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  set_target_properties(putty
    PROPERTIES LINK_INTERFACE_MULTIPLICITY 2)
  installed_program(putty)

  if(GTK_VERSION GREATER_EQUAL 3)
    add_executable(puttyapp
      putty.c
      main-gtk-application.c
      ${CMAKE_SOURCE_DIR}/stubs/no-cmdline.c
      ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
    be_list(puttyapp PuTTY SSH SERIAL OTHERBACKENDS)
    target_link_libraries(puttyapp
      guiterminal eventloop sshclient otherbackends settings
      network crypto utils puttyxpms
      ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  endif()

  # Not built by default, because it's a diagnostic rather than a
  # program anybody wants: it builds the real configuration dialog and
  # prints the strings it would put in its tree, its tabs and its
  # controls, so that translation coverage can be checked without
  # looking at a screen. Enable with -DWITH_CONFIG_DUMP=ON.
  option(WITH_CONFIG_DUMP "Build the config-dialog text dump" OFF)
  if(WITH_CONFIG_DUMP)
    add_executable(cfgdump
      cfgdump.c
      putty.c
      ${CMAKE_SOURCE_DIR}/stubs/no-cmdline.c
      ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
    be_list(cfgdump PuTTY SSH SERIAL OTHERBACKENDS)
    target_link_libraries(cfgdump
      guiterminal eventloop sshclient otherbackends settings
      network crypto utils puttyxpms
      ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  endif()

  add_executable(puttytel
    putty.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nocproxy.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c)
  be_list(puttytel PuTTYtel SERIAL OTHERBACKENDS)
  target_link_libraries(puttytel
    guiterminal eventloop otherbackends settings network utils
    puttyxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})

  add_executable(test_lineedit
    ${CMAKE_SOURCE_DIR}/test/test_lineedit.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
    ${CMAKE_SOURCE_DIR}/stubs/no-timing.c)
  target_link_libraries(test_lineedit
    guiterminal settings eventloop utils ${platform_libraries})

  add_executable(test_terminal
    ${CMAKE_SOURCE_DIR}/test/test_terminal.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
    ${CMAKE_SOURCE_DIR}/stubs/no-timing.c)
  target_link_libraries(test_terminal
    guiterminal settings eventloop utils ${platform_libraries})
endif()

# Pageant is built whether we have GTK or not; in its absence we
# degrade to a version that doesn't provide the GTK askpass.
if(GTK_FOUND)
  set(pageant_conditional_sources askpass.c)
  set(pageant_libs ${GTK_LIBRARIES})
else()
  set(pageant_conditional_sources noaskpass.c no-gtk.c)
  set(pageant_libs)
endif()
add_executable(pageant
  pageant.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  x11.c
  noise.c
  ${CMAKE_SOURCE_DIR}/ssh/x11fwd.c
  ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
  ${pageant_conditional_sources})
be_list(pageant Pageant)
target_link_libraries(pageant
  eventloop console agent settings network crypto utils
  ${pageant_libs})
installed_program(pageant)

add_sources_from_current_dir(test_conf unicode.c stubs/no-uxsel.c)
target_link_libraries(test_conf iso2022)

# ---------------------------------------------------------------------
# macOS application bundles.
#
# These are built into the "macos" subdirectory of the build directory,
# and are not installed, because their whole purpose is to be a
# self-contained thing you can double-click in the Finder.
#
# They are not self-contained as far as GTK is concerned: the shared
# libraries stay where they are in the system, so a bundle works for as
# long as the GTK it was built against remains installed. What _is_
# inside the bundle is the binary, the Info.plist, an application icon,
# and the language files, which the programs look for inside
# Contents/Resources/lang.

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # The language-selection code asks CoreFoundation for the user's
  # locale, because a bundle launched by the Finder has no locale
  # environment variables at all.
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  if(COREFOUNDATION_LIBRARY)
    target_link_libraries(utils ${COREFOUNDATION_LIBRARY})
  endif()

  # The first argument is the CMake target the binary is taken from, and
  # the second is the name that binary gets inside the bundle - which is
  # also the name used to look up its language file, so it must be the
  # plain lower-case program name.
  #
  # The terminal front ends are bundled using their "…app" variants,
  # i.e. the versions built on GtkApplication instead of plain
  # gtk_init(). Upstream's own bundle specifications use those too;
  # they're the ones that come to the foreground and get a real
  # application menu when started from the Finder.
  function(add_macos_bundle target prog name identifier icon)
    add_custom_target(${name}-app ALL
      COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/make-macos-bundle.sh
        --app ${CMAKE_BINARY_DIR}/macos/${name}.app
        --prog ${prog}
        --binary $<TARGET_FILE:${target}>
        --name ${name}
        --identifier ${identifier}
        --icon ${icon}
        --lang ${CMAKE_SOURCE_DIR}/windows/lang
        --version-from ${CMAKE_SOURCE_DIR}/version.h
      DEPENDS ${target} ${CMAKE_SOURCE_DIR}/version.h
              ${CMAKE_CURRENT_SOURCE_DIR}/make-macos-bundle.sh
              ${CMAKE_CURRENT_SOURCE_DIR}/macos-embed-deps.sh
      COMMENT "Building macOS application bundle ${name}.app")

    # make-macos-bundle.sh starts by deleting the bundle it's been told
    # to build, so by the time this runs the bundle always contains
    # nothing but what was just assembled. Copying in the libraries the
    # program links against is a separate step so that the bundles can
    # also be made self-contained after the fact, by running the second
    # script by hand.
    add_custom_command(TARGET ${name}-app POST_BUILD
      COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/macos-embed-deps.sh
        --app ${CMAKE_BINARY_DIR}/macos/${name}.app
      COMMENT "Embedding shared libraries in ${name}.app")
  endfunction()

  add_macos_bundle(puttyapp putty    PuTTY
                   org.tartarus.projects.putty.macputty
                   ${CMAKE_SOURCE_DIR}/windows/putty.ico)
  add_macos_bundle(ptermapp pterm    PTerm
                   org.tartarus.projects.putty.macpterm
                   ${CMAKE_SOURCE_DIR}/windows/pterm.ico)
  add_macos_bundle(puttygen-gtk puttygen PuTTYgen
                   org.tartarus.projects.putty.macputtygen
                   ${CMAKE_SOURCE_DIR}/windows/puttygen.ico)
  # Pageant deliberately isn't bundled. It's a background agent
  # process with no window of its own, and the Mac has no system tray
  # in which to show one, so double-clicking it would appear to do
  # nothing at all. Use the command-line pageant if you need it.
endif()
