qt6 = import('qt6')

qt6_dep = dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'Quick'])

qt6_resources = qt6.compile_resources(name: 'aegi_qrc', sources: 'resources.qrc')

qt6_moc = qt6.preprocess(
    moc_headers: files(
        'viewmodels/app_info_vm.h',
        'viewmodels/about_vm.h',
        'viewmodels/translation_service.h',
        'viewmodels/preferences_vm.h',
        'viewmodels/properties_vm.h',
    ),
    include_directories: [include_directories('.')],
    dependencies: qt6_dep,
)

# The wxWidgets build compiles these JSON blobs into libresrc. The Qt target
# reads them at startup for agi::Options, so copy them next to the executable.
default_config = configure_file(
    input: '../libresrc/default_config.json',
    output: 'default_config.json',
    copy: true,
)
default_config_platform = configure_file(
    input: host_machine.system() == 'windows'
        ? '../libresrc/default_config_win.json'
        : '../libresrc/default_config_platform.json.in',
    output: 'default_config_platform.json',
    copy: true,
)

# Reuse Aegisub's own version machinery (src/version.cpp + build/git_version.h)
# and link the very same core library/deps the wxWidgets frontend uses.
# The ass_* sources are the app-layer ASS data model (AssFile/AssInfo/AssStyle/
# AssDialogue); they are shared with the wx frontend and only compiled here.
# They need the wx precompiled header (src/utils.h uses wx types), hence the
# separate static library with cpp_pch.
ass_qt_core = static_library('ass_qt_core',
    files(
        '../ass_file.cpp',
        '../ass_entry.cpp',
        '../ass_dialogue.cpp',
        '../ass_style.cpp',
        '../ass_attachment.cpp',
        '../ass_override.cpp',
        '../ass_style_storage.cpp',
        '../utils.cpp',
        '../compat.cpp',
    ),
    include_directories: [libaegisub_inc, version_inc, deps_inc, include_directories('..')],
    dependencies: deps,
    cpp_pch: ['../include/agi_pre.h'],
    cpp_args: ['/utf-8'],
)

aegisub_qt = executable('aegisub-qt',
    files(
        'main.cpp',
        'aegisub_core.cpp',
        'viewmodels/app_info_vm.cpp',
        'viewmodels/about_vm.cpp',
        'viewmodels/translation_service.cpp',
        'viewmodels/preferences_vm.cpp',
        'viewmodels/properties_vm.cpp',
        '../version.cpp',
    ),
    qt6_moc,
    qt6_resources,
    version_h,
    include_directories: [libaegisub_inc, version_inc, deps_inc, include_directories('.'), include_directories('..')],
    link_with: [libaegisub, ass_qt_core],
    dependencies: [qt6_dep] + deps,
    cpp_args: ['/utf-8'],
)
