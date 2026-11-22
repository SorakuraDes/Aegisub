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
        'viewmodels/subtitle_grid_model.h',
        'viewmodels/edit_box_vm.h',
        'viewmodels/document_vm.h',
        'viewmodels/undo_manager.h',
        'viewmodels/search_vm.h',
        'viewmodels/styles_vm.h',
        'viewmodels/video_vm.h',
        'video_viewport_item.h',
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
        '../ass_parser.cpp',
        '../subtitle_format_ass.cpp',
        '../text_file_reader.cpp',
        '../text_file_writer.cpp',
        '../string_codec.cpp',
        'subtitle_format_shim.cpp',
        # AssFile::Commit / AddUndoManager take wxString; this bridge does the
        # conversion so the Qt sources never include a wx header.
        'commit_bridge.cpp',
    ),
    include_directories: [libaegisub_inc, version_inc, deps_inc, include_directories('..')],
    dependencies: deps,
    cpp_pch: ['../include/agi_pre.h'],
    cpp_args: ['/utf-8'],
)

# av_qt_core: the wxWidgets frontend's video/subtitle provider sources, compiled
# for the Qt process. They are unchanged originals (src/ is off limits); the
# only Qt-specific pieces are the wx-free bridge and the synchronous background
# runner. A wx precompiled header is required because the originals still use wx
# helpers (wxString formatting, the multi-track selection dialog).
av_qt_core = static_library('av_qt_core',
    files(
        '../ffmpegsource_common.cpp',
        '../video_provider_ffmpegsource.cpp',
        '../video_provider_dummy.cpp',
        '../video_provider_cache.cpp',
        '../colorspace.cpp',
        '../subtitles_provider.cpp',
        '../subtitles_provider_csri.cpp',
        '../subtitles_provider_libass.cpp',
        # ColorMatrix helpers extracted from video_provider_manager.cpp (which
        # also contains wx dialogs and must not be linked here).
        'color_matrix.cpp',
        'qt_background_runner.cpp',
        'video_bridge.cpp',
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
        'viewmodels/subtitle_grid_model.cpp',
        'viewmodels/edit_box_vm.cpp',
        'viewmodels/document_vm.cpp',
        'viewmodels/undo_manager.cpp',
        'viewmodels/search_vm.cpp',
        'viewmodels/styles_vm.cpp',
        'viewmodels/video_vm.cpp',
        'video_viewport_item.cpp',
        '../version.cpp',
    ),
    qt6_moc,
    qt6_resources,
    version_h,
    include_directories: [libaegisub_inc, version_inc, deps_inc, include_directories('.'), include_directories('..')],
    link_with: [libaegisub, ass_qt_core, av_qt_core],
    dependencies: [qt6_dep] + deps,
    cpp_args: ['/utf-8'],
)
